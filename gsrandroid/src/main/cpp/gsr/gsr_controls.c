#include "gsr_controls.h"
#include "gsr_settings.h"
#include "gsr_text.h"
#include "descore.h"
#include "touch/descore_touch.h"

#include <GLES3/gl3.h>
#include <android/log.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Android KeyEvent codes */
enum {
	K_DPAD_UP = 19, K_DPAD_DOWN = 20, K_DPAD_LEFT = 21, K_DPAD_RIGHT = 22,
	K_BTN_A = 96, K_BTN_B = 97, K_BTN_X = 99, K_BTN_Y = 100, K_BTN_L1 = 102, K_BTN_R1 = 103,
	K_BACK = 4, K_BTN_L2 = 104, K_BTN_R2 = 105, K_BTN_START = 108, K_BTN_SELECT = 109, K_BTN_MODE = 110, K_BTN_THUMBR = 107
};
/* Android MotionEvent axes */
enum { AX_X = 0, AX_Y = 1, AX_HAT_X = 15, AX_HAT_Y = 16, AX_LT = 17, AX_RT = 18, AX_GAS = 22, AX_BRAKE = 23 };


static int g_bind[GSR_ACT_COUNT][GSR_BIND_SLOTS];
static atomic_uint g_slot_held;   /* bit act*2+slot */
static atomic_uint g_stick_dirs;  /* left stick, as action bits */
static atomic_uint g_hat_dirs;
static atomic_uint g_touch_mask;  /* touch buttons, action bits */
static atomic_uint g_touch_dirs;  /* touch d-pad circle, action bits */
static atomic_int g_ff_latch, g_ff_prev, g_menu_prev;
static char g_path[1024];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_w, g_h;

/* ---- bindings ------------------------------------------------------------------------------------- */

static void defaults(void) {
	memset(g_bind, 0, sizeof g_bind);
	/* The south button is A and the east button is B (the way Android pads are labelled); the other two
	 * face buttons repeat them. */
	g_bind[GSR_ACT_A][0] = K_BTN_A;       g_bind[GSR_ACT_A][1] = K_BTN_X;
	g_bind[GSR_ACT_B][0] = K_BTN_B;       g_bind[GSR_ACT_B][1] = K_BTN_Y;
	g_bind[GSR_ACT_L][0] = K_BTN_L1;      g_bind[GSR_ACT_R][0] = K_BTN_R1;
	g_bind[GSR_ACT_START][0] = K_BTN_START;
	g_bind[GSR_ACT_SELECT][0] = K_BTN_SELECT;
	g_bind[GSR_ACT_UP][0] = K_DPAD_UP;    g_bind[GSR_ACT_DOWN][0] = K_DPAD_DOWN;
	g_bind[GSR_ACT_LEFT][0] = K_DPAD_LEFT; g_bind[GSR_ACT_RIGHT][0] = K_DPAD_RIGHT;
	g_bind[GSR_ACT_FF][0] = K_BTN_R2;
	g_bind[GSR_ACT_MENU][0] = K_BTN_THUMBR; g_bind[GSR_ACT_MENU][1] = K_BTN_MODE;
}

static void save_binds(void) {
	char tmp[1100];
	FILE *f;
	if (!g_path[0]) return;
	snprintf(tmp, sizeof tmp, "%s.tmp", g_path);
	f = fopen(tmp, "w");
	if (!f) return;
	for (int a = 0; a < GSR_ACT_COUNT; a++) fprintf(f, "%d=%d,%d\n", a, g_bind[a][0], g_bind[a][1]);
	fclose(f);
	rename(tmp, g_path);
}

static void load_binds(void) {
	char line[64];
	FILE *f = fopen(g_path, "r");
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		int a, k0, k1;
		if (sscanf(line, "%d=%d,%d", &a, &k0, &k1) == 3 && a >= 0 && a < GSR_ACT_COUNT) {
			g_bind[a][0] = k0; g_bind[a][1] = k1;
		}
	}
	fclose(f);
}

int gsr_controls_bind_get(GsrAction a, int slot) {
	return (a >= 0 && a < GSR_ACT_COUNT && slot >= 0 && slot < GSR_BIND_SLOTS) ? g_bind[a][slot] : 0;
}

void gsr_controls_bind_set(GsrAction a, int slot, int key) {
	if (a < 0 || a >= GSR_ACT_COUNT || slot < 0 || slot >= GSR_BIND_SLOTS) return;
	pthread_mutex_lock(&g_mu);
	if (key != 0)
		for (int i = 0; i < GSR_ACT_COUNT; i++)
			for (int s = 0; s < GSR_BIND_SLOTS; s++)
				if (g_bind[i][s] == key) g_bind[i][s] = 0;
	g_bind[a][slot] = key;
	atomic_store(&g_slot_held, 0);
	save_binds();
	pthread_mutex_unlock(&g_mu);
}

void gsr_controls_bind_reset(void) {
	pthread_mutex_lock(&g_mu);
	defaults();
	atomic_store(&g_slot_held, 0);
	save_binds();
	pthread_mutex_unlock(&g_mu);
}

/* ---- state -> engine -------------------------------------------------------------------------------- */

static unsigned held_actions(void) {
	unsigned slots = atomic_load(&g_slot_held), m = 0;
	for (int a = 0; a < GSR_ACT_COUNT; a++)
		if (slots & (3u << (a * 2))) m |= 1u << a;
	return m | atomic_load(&g_stick_dirs) | atomic_load(&g_hat_dirs) | atomic_load(&g_touch_mask) | atomic_load(&g_touch_dirs);
}

/* Fast forward as a toggle latches on the press edge, wherever the press came from. */
static void ff_changed(void) {
	int now = (held_actions() >> GSR_ACT_FF) & 1;
	int prev = atomic_exchange(&g_ff_prev, now);
	if (now && !prev && gsr_settings_get(GSR_S_FF_MODE) == 1) atomic_fetch_xor(&g_ff_latch, 1);
}

/* The menu action (a gamepad button, the touch MENU button, or Back) asks the Java side to open the settings. */
static void menu_changed(void) {
	int now = (held_actions() >> GSR_ACT_MENU) & 1;
	int prev = atomic_exchange(&g_menu_prev, now);
	if (now && !prev) descore_request_menu();
}

static void edges(void) {
	ff_changed();
	menu_changed();
}

void gsr_controls_release_all(void) {
	atomic_store(&g_slot_held, 0);
	atomic_store(&g_stick_dirs, 0);
	atomic_store(&g_hat_dirs, 0);
	atomic_store(&g_touch_mask, 0);
	atomic_store(&g_touch_dirs, 0);
	atomic_store(&g_ff_prev, 0);
	atomic_store(&g_menu_prev, 0);
}

uint16_t gsr_controls_keyinput(void) {
	unsigned m = held_actions();
	if ((m & (1u << GSR_ACT_LEFT)) && (m & (1u << GSR_ACT_RIGHT))) m &= ~((1u << GSR_ACT_LEFT) | (1u << GSR_ACT_RIGHT));
	if ((m & (1u << GSR_ACT_UP)) && (m & (1u << GSR_ACT_DOWN))) m &= ~((1u << GSR_ACT_UP) | (1u << GSR_ACT_DOWN));
	return (uint16_t) (~m & 0x3FFu);
}

int gsr_controls_fast_forward(void) {
	if (gsr_settings_get(GSR_S_FF_MODE) == 1) return atomic_load(&g_ff_latch);
	atomic_store(&g_ff_latch, 0);
	return (held_actions() >> GSR_ACT_FF) & 1;
}

/* ---- gamepad ---------------------------------------------------------------------------------------- */

static void key_internal(int code, int down) {
	if (code == K_BACK) {
		if (down) descore_request_menu();
		return;
	}
	int matched = 0;
	for (int a = 0; a < GSR_ACT_COUNT; a++)
		for (int s = 0; s < GSR_BIND_SLOTS; s++)
			if (g_bind[a][s] == code) {
				unsigned bit = 1u << (a * 2 + s);
				matched = 1;
				if (down) atomic_fetch_or(&g_slot_held, bit); else atomic_fetch_and(&g_slot_held, ~bit);
			}
	if (down && !matched) __android_log_print(ANDROID_LOG_INFO, "gsr-input", "key %d is not bound to anything", code);
	edges();
}

void gsr_controls_key(int code, int down) { key_internal(code, down); }

static void axis_trigger(int code, float v, atomic_int *state) {
	int now = v > 0.5f ? 1 : (v < 0.3f ? 0 : atomic_load(state));
	if (now != atomic_exchange(state, now)) key_internal(code, now);
}

void gsr_controls_axis(int axis, float v) {
	static atomic_int lt, rt;
	unsigned d;
	switch (axis) {
	case AX_X: case AX_Y: {
		static float sx, sy;
		if (axis == AX_X) sx = v; else sy = v;
		d = 0;
		if (sx < -0.5f) d |= 1u << GSR_ACT_LEFT;
		if (sx > 0.5f) d |= 1u << GSR_ACT_RIGHT;
		if (sy < -0.5f) d |= 1u << GSR_ACT_UP;
		if (sy > 0.5f) d |= 1u << GSR_ACT_DOWN;
		atomic_store(&g_stick_dirs, d);
		break;
	}
	case AX_HAT_X: case AX_HAT_Y: {
		static float hx, hy;
		if (axis == AX_HAT_X) hx = v; else hy = v;
		d = 0;
		if (hx < -0.5f) d |= 1u << GSR_ACT_LEFT;
		if (hx > 0.5f) d |= 1u << GSR_ACT_RIGHT;
		if (hy < -0.5f) d |= 1u << GSR_ACT_UP;
		if (hy > 0.5f) d |= 1u << GSR_ACT_DOWN;
		atomic_store(&g_hat_dirs, d);
		break;
	}
	case AX_LT: case AX_BRAKE: axis_trigger(K_BTN_L2, v, &lt); break;
	case AX_RT: case AX_GAS: axis_trigger(K_BTN_R2, v, &rt); break;
	default: return;
	}
	edges();
}

/* ---- touch overlay ---------------------------------------------------------------------------------- */

enum { TB_A, TB_B, TB_L, TB_R, TB_START, TB_SELECT, TB_FF, TB_MENU, TB_COUNT };
static const unsigned char k_tb_action[TB_COUNT] = { GSR_ACT_A, GSR_ACT_B, GSR_ACT_L, GSR_ACT_R, GSR_ACT_START, GSR_ACT_SELECT, GSR_ACT_FF, GSR_ACT_MENU };
static const char *k_tb_label[TB_COUNT] = { "A", "B", "L", "R", "START", "SELECT", "FF", "MENU" };
static float g_tb_rect[TB_COUNT][4];

static void touch_key(unsigned char act, int down) {
	if (down) atomic_fetch_or(&g_touch_mask, 1u << act); else atomic_fetch_and(&g_touch_mask, ~(1u << act));
	edges();
}

static void touch_stick(int idx, float x, float y) {
	unsigned d = 0;
	(void) idx;
	if (gsr_settings_get(GSR_S_TOUCH_DPAD)) {
		unsigned p = descore_touch_dpad_dirs(x, y);
		if (p & DESCORE_DPAD_LEFT) d |= 1u << GSR_ACT_LEFT;
		if (p & DESCORE_DPAD_RIGHT) d |= 1u << GSR_ACT_RIGHT;
		if (p & DESCORE_DPAD_UP) d |= 1u << GSR_ACT_UP;
		if (p & DESCORE_DPAD_DOWN) d |= 1u << GSR_ACT_DOWN;
		atomic_store(&g_touch_dirs, d);
		return;
	}
	if (x < -0.35f) d |= 1u << GSR_ACT_LEFT;
	if (x > 0.35f) d |= 1u << GSR_ACT_RIGHT;
	if (y < -0.35f) d |= 1u << GSR_ACT_UP;
	if (y > 0.35f) d |= 1u << GSR_ACT_DOWN;
	atomic_store(&g_touch_dirs, d);
}

void gsr_controls_layout(int w, int h) {
	float cell, m, r, sb;
	float scale = descore_touch_scale_value(gsr_settings_get(GSR_S_TOUCH_SCALE));
	g_w = w; g_h = h;
	descore_touch_set_screen_size(w, h);
	cell = (w < h ? w : h) * 0.13f * scale;
	if (cell < descore_touch_min_cell_px()) cell = descore_touch_min_cell_px();
	m = cell * 0.35f;
	r = cell * 1.45f;
	sb = cell * 1.25f;
	descore_touch_set_stick(0, m + r * 1.05f, h - m - r * 1.05f, r, touch_stick);
	descore_touch_set_stick_style(0, gsr_settings_get(GSR_S_TOUCH_DPAD) ? DESCORE_TOUCH_STICK_DPAD : DESCORE_TOUCH_STICK_ANALOG);
	{
		float rx = w - m - sb * 2.4f, ry = h - m - sb * 1.9f;
		float *a = g_tb_rect[TB_A], *b = g_tb_rect[TB_B], *l = g_tb_rect[TB_L], *rr = g_tb_rect[TB_R];
		float *st = g_tb_rect[TB_START], *se = g_tb_rect[TB_SELECT], *ff = g_tb_rect[TB_FF], *mn = g_tb_rect[TB_MENU];
		a[0] = rx + sb * 1.15f; a[1] = ry; a[2] = sb; a[3] = sb;
		b[0] = rx; b[1] = ry + sb * 0.85f; b[2] = sb; b[3] = sb;
		l[0] = m; l[1] = m; l[2] = cell * 1.8f; l[3] = cell * 0.95f;
		rr[0] = w - m - cell * 1.8f; rr[1] = m; rr[2] = cell * 1.8f; rr[3] = cell * 0.95f;
		se[0] = w * 0.5f - cell * 1.9f; se[1] = h - m - cell * 0.8f; se[2] = cell * 1.8f; se[3] = cell * 0.8f;
		st[0] = w * 0.5f + cell * 0.1f; st[1] = se[1]; st[2] = cell * 1.8f; st[3] = cell * 0.8f;
		ff[0] = w * 0.5f - cell * 1.9f; ff[1] = m; ff[2] = cell * 1.8f; ff[3] = cell * 0.8f;
		mn[0] = w * 0.5f + cell * 0.1f; mn[1] = m; mn[2] = cell * 1.8f; mn[3] = cell * 0.8f;
	}
	for (int i = 0; i < TB_COUNT; i++) {
		unsigned char key = k_tb_action[i];
		descore_touch_set_button(i, g_tb_rect[i][0], g_tb_rect[i][1], g_tb_rect[i][2], g_tb_rect[i][3], &key, 1, false);
	}
}

void gsr_controls_touch_settings_changed(void) {
	descore_touch_set_scale_step(gsr_settings_get(GSR_S_TOUCH_SCALE));
	descore_touch_set_opacity(gsr_settings_get(GSR_S_TOUCH_OPACITY) / 100.0f);
	if (g_w > 0) gsr_controls_layout(g_w, g_h);
}

void gsr_controls_init(const char *dir) {
	snprintf(g_path, sizeof g_path, "%s/controls.ini", dir);
	defaults();
	load_binds();
	descore_touch_init(TB_COUNT, touch_key, NULL);
	descore_touch_set_scale_step(gsr_settings_get(GSR_S_TOUCH_SCALE));
	descore_touch_set_opacity(gsr_settings_get(GSR_S_TOUCH_OPACITY) / 100.0f);
}

/* ---- captions ---------------------------------------------------------------------------------------
 * Descore draws the buttons as flat translucent quads. The captions are rasterised once into a small
 * texture from the built-in 5x7 font and drawn as textured quads, so there is no per-frame text cost. */

#define ATLAS_W 256
#define ATLAS_H 64
#define ATLAS_SCALE 3
static GLuint g_atlas_tex, g_atlas_prog, g_atlas_vbo;
static GLint g_atlas_u_screen, g_atlas_u_alpha;
static struct { int x, y, w, h; } g_label_px[TB_COUNT];

static void build_atlas(void) {
	static uint8_t px[ATLAS_W * ATLAS_H];
	int x = 0, y = 0;
	memset(px, 0, sizeof px);
	for (int i = 0; i < TB_COUNT; i++) {
		int w = (int) strlen(k_tb_label[i]) * 6 * ATLAS_SCALE, h = 8 * ATLAS_SCALE;
		if (x + w > ATLAS_W) { x = 0; y += h + 2; }
		gsr_text_blit(k_tb_label[i], ATLAS_SCALE, px, ATLAS_W, x, y);
		g_label_px[i].x = x; g_label_px[i].y = y; g_label_px[i].w = w - ATLAS_SCALE; g_label_px[i].h = 7 * ATLAS_SCALE;
		x += w + 2;
	}
	glGenTextures(1, &g_atlas_tex);
	glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_W, ATLAS_H, 0, GL_RED, GL_UNSIGNED_BYTE, px);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void build_prog(void) {
	static const char *vs = "#version 300 es\nlayout(location=0) in vec2 p; layout(location=1) in vec2 t; uniform vec2 screen; out vec2 uv;\n"
		"void main(){ uv=t; gl_Position=vec4(p.x/screen.x*2.0-1.0, 1.0-p.y/screen.y*2.0, 0.0, 1.0); }\n";
	static const char *fs = "#version 300 es\nprecision mediump float; in vec2 uv; uniform sampler2D tex; uniform float alpha; out vec4 o;\n"
		"void main(){ o = vec4(1.0,1.0,1.0, texture(tex, uv).r * alpha); }\n";
	GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
	glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
	g_atlas_prog = glCreateProgram();
	glAttachShader(g_atlas_prog, v); glAttachShader(g_atlas_prog, f);
	glLinkProgram(g_atlas_prog);
	glDeleteShader(v); glDeleteShader(f);
	g_atlas_u_screen = glGetUniformLocation(g_atlas_prog, "screen");
	g_atlas_u_alpha = glGetUniformLocation(g_atlas_prog, "alpha");
	glGenBuffers(1, &g_atlas_vbo);
}

void gsr_controls_draw_labels(int sw, int sh) {
	float alpha;
	if (descore_gamepad_connected() || g_w <= 0) return;
	if (!g_atlas_prog || !glIsProgram(g_atlas_prog)) build_prog();
	if (!g_atlas_tex || !glIsTexture(g_atlas_tex)) build_atlas();
	alpha = gsr_settings_get(GSR_S_TOUCH_OPACITY) / 100.0f + 0.35f;
	if (alpha > 1.0f) alpha = 1.0f;
	glBindVertexArray(0);
	glUseProgram(g_atlas_prog);
	glUniform2f(g_atlas_u_screen, (float) sw, (float) sh);
	glUniform1f(g_atlas_u_alpha, alpha);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
	glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, g_atlas_vbo);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	for (int i = 0; i < TB_COUNT; i++) {
		const float *r = g_tb_rect[i];
		float lw = (float) g_label_px[i].w, lh = (float) g_label_px[i].h;
		float k = r[3] * 0.38f / lh;                  /* caption height is ~38% of the button */
		float maxk = r[2] * 0.75f / lw;
		float dw, dh, x0, y0, u0, v0, u1, v1;
		GLfloat v[16];
		if (k > maxk) k = maxk;
		dw = lw * k; dh = lh * k;
		x0 = r[0] + (r[2] - dw) * 0.5f; y0 = r[1] + (r[3] - dh) * 0.5f;
		u0 = g_label_px[i].x / (float) ATLAS_W; v0 = g_label_px[i].y / (float) ATLAS_H;
		u1 = (g_label_px[i].x + g_label_px[i].w) / (float) ATLAS_W; v1 = (g_label_px[i].y + g_label_px[i].h) / (float) ATLAS_H;
		v[0] = x0;      v[1] = y0;      v[2] = u0; v[3] = v0;
		v[4] = x0 + dw; v[5] = y0;      v[6] = u1; v[7] = v0;
		v[8] = x0;      v[9] = y0 + dh; v[10] = u0; v[11] = v1;
		v[12] = x0 + dw; v[13] = y0 + dh; v[14] = u1; v[15] = v1;
		glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (const void *) 0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (const void *) 8);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDisable(GL_BLEND);
	glUseProgram(0);
}
