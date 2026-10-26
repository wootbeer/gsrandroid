/* gsr_main.c -- milestone 0 smoke test for the Golden Sun Recompiled Android port.
 *
 * Proves the shell end to end before any of the real game is attached: the launcher picked a ROM
 * and stored it as golden_sun.gba, the native library loaded, a GLES 3 context is current, frames
 * are presented, and the app survives screen lock/unlock.
 *
 * Screen colour:
 *   green   the ROM is present and its SHA-1 matches the one GSRecomp expects
 *   yellow  the ROM is present and 8 MiB but its SHA-1 differs (different region/revision)
 *   red     no usable ROM file
 * A slow brightness pulse shows that frames keep coming.
 */
#include "descore.h"
#include "gsr_prepare.h"
#include "gsr_engine.h"
#include "gsr_controls.h"
#include "gsr_host.h"
#include "gsr_settings.h"
#include "gsr_text.h"
#include "gsr_game.h"

#include <GLES3/gl3.h>
#include <android/log.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "gsr", __VA_ARGS__)

/* The stored ROM's name, its SHA-1 and size: per game, see gsr_game.c (GsrConfig.java names them too). */
#define GSR_ROM_NAME (gsr_game()->rom_name)
#define GSR_ROM_SHA1 (gsr_game()->rom_sha1)
#define GSR_ROM_SIZE (gsr_game()->rom_size)

/* ---- Compact SHA-1 (FIPS 180-1) ------------------------------------------------------------- */

typedef struct {
	uint32_t h[5];
	uint64_t len;
	uint8_t buf[64];
	size_t fill;
} Sha1;

#define ROL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

static void sha1_block(Sha1 *c, const uint8_t *p)
{
	uint32_t w[80], a, b, cc, d, e, t;
	int i;
	for (i = 0; i < 16; i++)
		w[i] = ((uint32_t) p[4 * i] << 24) | ((uint32_t) p[4 * i + 1] << 16)
				| ((uint32_t) p[4 * i + 2] << 8) | p[4 * i + 3];
	for (i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4];
	for (i = 0; i < 80; i++) {
		uint32_t f, k;
		if (i < 20) { f = (b & cc) | (~b & d); k = 0x5A827999u; }
		else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1u; }
		else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDCu; }
		else { f = b ^ cc ^ d; k = 0xCA62C1D6u; }
		t = ROL(a, 5) + f + e + k + w[i];
		e = d; d = cc; cc = ROL(b, 30); b = a; a = t;
	}
	c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void sha1_init(Sha1 *c)
{
	c->h[0] = 0x67452301u; c->h[1] = 0xEFCDAB89u; c->h[2] = 0x98BADCFEu;
	c->h[3] = 0x10325476u; c->h[4] = 0xC3D2E1F0u;
	c->len = 0;
	c->fill = 0;
}

static void sha1_update(Sha1 *c, const uint8_t *p, size_t n)
{
	c->len += n;
	while (n > 0) {
		size_t take = 64 - c->fill;
		if (take > n) take = n;
		memcpy(c->buf + c->fill, p, take);
		c->fill += take; p += take; n -= take;
		if (c->fill == 64) { sha1_block(c, c->buf); c->fill = 0; }
	}
}

static void sha1_final(Sha1 *c, char out_hex[41])
{
	uint64_t bits = c->len * 8;
	uint8_t pad = 0x80, zero = 0, lenb[8];
	int i;
	sha1_update(c, &pad, 1);
	while (c->fill != 56) sha1_update(c, &zero, 1);
	for (i = 0; i < 8; i++) lenb[i] = (uint8_t) (bits >> (56 - 8 * i));
	sha1_update(c, lenb, 8);
	for (i = 0; i < 5; i++) snprintf(out_hex + 8 * i, 9, "%08x", c->h[i]);
}

/* ---- ROM check ------------------------------------------------------------------------------ */

/* 0 = missing/unreadable, 1 = present with the wrong hash, 2 = verified. */
static int check_rom(const char *data_dir, char hex[41])
{
	char path[1024];
	uint8_t chunk[64 * 1024];
	Sha1 sha;
	long total = 0;
	size_t n;
	FILE *f;

	snprintf(path, sizeof path, "%s/%s", data_dir, GSR_ROM_NAME);
	f = fopen(path, "rb");
	hex[0] = 0;
	if (f == NULL) {
		LOG("ROM not found: %s", path);
		return 0;
	}
	sha1_init(&sha);
	while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
		sha1_update(&sha, chunk, n);
		total += (long) n;
	}
	fclose(f);
	sha1_final(&sha, hex);
	LOG("ROM %s: %ld bytes, sha1 %s", path, total, hex);
	if (total != GSR_ROM_SIZE) return 0;
	if (strcmp(hex, GSR_ROM_SHA1) != 0) {
		LOG("SHA-1 differs from the expected %s", GSR_ROM_SHA1);
		return 1;
	}
	return 2;
}

/* ---- Build screen ---------------------------------------------------------------------------- */

static void fmt_time(char *out, size_t n, int sec)
{
	snprintf(out, n, "%d:%02d", sec / 60, sec % 60);
}

static const int g_native_build_text = 0;

static void draw_build_screen(int w, int h, int status)
{
	int scale = w / 380 < 2 ? 2 : w / 380;
	int cw = gsr_text_cell_w(scale), ch = gsr_text_cell_h(scale);
	int margin = w / 20, maxc = (w - 2 * margin) / cw;
	int y = h / 8, st = gsr_prepare_state(), lines;
	char msg[200], err[420], line[300], tm[16], logp[700];

	glDisable(GL_SCISSOR_TEST);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	/* The text and progress bar are an Android view (GsrBuildOverlay) on top of this black surface. */
	if (!g_native_build_text) return;

	if (status == 0) {
		gsr_text_paragraph(h, margin, y, scale, maxc, "NO USABLE ROM FOUND.\nRESTART THE APP AND PICK YOUR GOLDEN SUN (USA/EUROPE) .GBA FILE.", 1, 1, 1);
		return;
	}
	if (status == 1) {
		gsr_text_paragraph(h, margin, y, scale, maxc, "THIS IS NOT THE SUPPORTED ROM.\nGOLDEN SUN (USA/EUROPE) IS REQUIRED. CLEAR THE APP STORAGE TO PICK ANOTHER FILE.", 1, 1, 1);
		return;
	}
	gsr_prepare_message(msg, sizeof msg);
	gsr_prepare_error(err, sizeof err);
	fmt_time(tm, sizeof tm, gsr_prepare_elapsed_s());
	if (st == GSR_PREP_FAILED) {
		lines = gsr_text_paragraph(h, margin, y, scale, maxc, "BUILDING THE GAME FAILED", 1, 0.6f, 0.6f);
		lines += 1 + gsr_text_paragraph(h, margin, y + 2 * ch, scale, maxc, err, 1, 1, 1);
		gsr_prepare_log_path(logp, sizeof logp);
		snprintf(line, sizeof line, "LOG: %s", logp);
		gsr_text_paragraph(h, margin, y + (lines + 2) * ch, scale > 2 ? scale - 1 : scale, maxc * scale / (scale > 2 ? scale - 1 : scale), line, 0.8f, 0.8f, 0.8f);
		gsr_text_paragraph(h, margin, h - 3 * ch, scale, maxc, "CLOSE AND REOPEN THE APP TO TRY AGAIN.", 1, 1, 1);
		return;
	}
	if (st == GSR_PREP_READY) {
		gsr_text_paragraph(h, margin, y, scale, maxc, "GAME CODE IS BUILT AND LOADED.", 0.6f, 1, 1);
		snprintf(line, sizeof line, "%s IN %s. STARTING THE GAME.",
				gsr_prepare_cached() ? "LOADED FROM THE SAVED BUILD" : "BUILT", tm);
		gsr_text_paragraph(h, margin, y + 2 * ch, scale, maxc, line, 1, 1, 1);
		return;
	}
	gsr_text_paragraph(h, margin, y, scale, maxc, gsr_prepare_cached() ? "LOADING GOLDEN SUN" : "PREPARING GOLDEN SUN FOR THIS DEVICE", 1, 1, 1);
	gsr_text_paragraph(h, margin, y + 2 * ch, scale, maxc, msg, 0.7f, 0.85f, 1);
	snprintf(line, sizeof line, "%d%%   %s", gsr_prepare_progress() / 10, tm);
	gsr_text_draw(h, margin, y + 4 * ch, scale, line, 1, 1, 1);
	if (!gsr_prepare_cached())
		gsr_text_paragraph(h, margin, h - 5 * ch, scale, maxc,
				"THIS ONLY HAPPENS ONCE. IT CAN TAKE 10 MINUTES OR MORE. KEEP THE DEVICE AWAKE.", 0.8f, 0.8f, 0.8f);
	{ /* progress bar */
		int bw = w - 2 * margin, bh = ch;
		glEnable(GL_SCISSOR_TEST);
		glScissor(margin, h - (y + 6 * ch + bh), bw, bh);
		glClearColor(0.15f, 0.2f, 0.4f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glScissor(margin, h - (y + 6 * ch + bh), bw * gsr_prepare_progress() / 1000, bh);
		glClearColor(1, 1, 1, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);
	}
}

/* ---- Entry points the descore shell requires --------------------------------------------------- */

static char g_rom_path[1100], g_run_dir[1100];
static int g_engine_started;
static char g_engine_err[500];
static volatile int g_rom_status = -1;

/* What the Java build overlay shows (see gsr_jni.c, GsrBuildOverlay.java). */
int gsr_main_rom_status(void) { return g_rom_status; }
int gsr_main_engine_started(void) { return g_engine_started; }
const char *gsr_main_engine_error(void) { return g_engine_err; }

/* Once the game code is built and loaded: bind the engine to it and start it on a thread of its own. The render
 * thread then only presents what the engine produces. */
/* Debuggable builds only. The engine reports on stderr ("[frame-interpolation] DEGRADED ...", "[gsr] ..."). Android drops that, so
 * forward it to logcat (tag gsr-engine) one line at a time. */
static void *stderr_pump(void *arg)
{
	int fd = (int)(intptr_t)arg;
	char line[1024];
	size_t n = 0;
	char c;
	while (read(fd, &c, 1) == 1) {
		if (c == '\n' || n == sizeof line - 1) {
			line[n] = 0;
			if (n) __android_log_write(ANDROID_LOG_INFO, "gsr-engine", line);
			n = 0;
			if (c != '\n') line[n++] = c;
		} else {
			line[n++] = c;
		}
	}
	return NULL;
}

static void forward_stderr(void)
{
	static int done;
	int p[2];
	pthread_t t;
	if (done || pipe(p) != 0) return;
	done = 1;
	dup2(p[1], 2);
	close(p[1]);
	setvbuf(stderr, NULL, _IOLBF, 0);
	pthread_create(&t, NULL, stderr_pump, (void *)(intptr_t)p[0]);
	pthread_detach(t);
}

/* The other game's folder beside this one's (<files>/gsr/game and <files>/gsr/tla). */
static char g_other_dir[1100];

/* Golden Sun's Seamless mode hands over to The Lost Age, so it is only offered when that ROM is in the app. */
int gsr_host_tla_installed(void)
{
	char p[1200];
	struct stat st;
	if (gsr_game()->tla) return 1;
	snprintf(p, sizeof p, "%s/lost_age.gba", g_other_dir);
	return stat(p, &st) == 0 && st.st_size == 16777216;
}

/* The Lost Age gets Golden Sun's save (GSR_SEAMLESS_GS_SAVE, as upstream's launcher sets it) whenever there is
 * one, for "Load a Golden Sun save file?". After Golden Sun's Seamless ending the Java side leaves the chosen
 * slot in seamless_slot.txt; that is passed on once (GSR_SEAMLESS_GS_SLOT) and the file removed. */
static void seamless_environment(const char *data_dir)
{
	char save[1200], marker[1200];
	struct stat st;
	FILE *f;
	unsetenv("GSR_SEAMLESS_GS_SAVE");
	unsetenv("GSR_SEAMLESS_GS_SLOT");
	if (!gsr_game()->tla) return;
	snprintf(save, sizeof save, "%s/golden_sun.sav", g_other_dir);
	if (stat(save, &st) == 0 && st.st_size > 0) setenv("GSR_SEAMLESS_GS_SAVE", save, 1);
	snprintf(marker, sizeof marker, "%s/seamless_slot.txt", data_dir);
	f = fopen(marker, "r");
	if (f) {
		int slot = 0;
		if (fscanf(f, "%d", &slot) == 1 && slot >= 1 && slot <= 3 && getenv("GSR_SEAMLESS_GS_SAVE")) {
			char v[4];
			snprintf(v, sizeof v, "%d", slot);
			setenv("GSR_SEAMLESS_GS_SLOT", v, 1);
			LOG("seamless: continuing Golden Sun slot %d", slot);
		}
		fclose(f);
		remove(marker);
	}
}

static void start_engine(const char *data_dir)
{
	char err[400] = "";
	static char a0[] = "gsr", a1[] = "--rom", a3[] = "--window", a4[] = "--no-bios", a5[] = "--scale", a6[] = "1", a7[] = "--quiet";
	char *argv[] = { a0, a1, g_rom_path, a3, a4, a5, a6, a7, NULL };

	g_engine_started = 1;
	if (gsr_host_debug()) forward_stderr();
	/* The PC player's launcher starts the game with these set; the widened (Expanded) view draws its extra
	 * world from the room buffer and needs the first one, the sparks and spell effects need the second.
	 * overwrite=0 so a value set from outside still wins. */
	setenv("GSR_ROOM_BUFFER_RENDER", "1", 0);
	setenv("GSR_HOST_EFFECTS", "1", 0);
	setenv("GBARECOMP_EXPERIMENTAL_FIXES", "1", 0);
	/* The GBA's own left/right sound channels instead of the old mono route (the PC launcher sets this too). */
	setenv("GBARECOMP_AUDIO_STEREO", "1", 0);
	seamless_environment(data_dir);
	snprintf(g_rom_path, sizeof g_rom_path, "%s/%s", data_dir, GSR_ROM_NAME);
	snprintf(g_run_dir, sizeof g_run_dir, "%s/../%s", data_dir, gsr_game()->run);
	mkdir(g_run_dir, 0755);
	if (chdir(g_run_dir) != 0) LOG("chdir %s failed", g_run_dir);
	if (gsr_engine_bind(err, sizeof err) != 0 || gsr_engine_start(8, argv, err, sizeof err) != 0) {
		snprintf(g_engine_err, sizeof g_engine_err, "%s", err);
		LOG("engine start failed: %s", err);
		return;
	}
	LOG("engine started");
}

/* The error texts are shown by GsrBuildOverlay (Java); the game surface stays black under it. */
static void draw_black(void)
{
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

int descore_game_main(const DescoreGameParams *params)
{
	char hex[41], shared_dir[1100];
	gsr_game_select(params->data_dir);
	int status = check_rom(params->data_dir, hex);
	g_rom_status = status;
	unsigned frame = 0, settings_gen = 0;
	int top_inset = 0;
	GLubyte *version;

	LOG("descore_game_main: data_dir=%s surface=%dx%d status=%d", params->data_dir,
			params->surface_w, params->surface_h, status);
	version = (GLubyte *) glGetString(GL_VERSION);
	LOG("GL_VERSION %s", version ? (const char *) version : "(null)");

	/* Options and controls live next to the Golden Sun ROM in the app's private folder (<files>/gsr/game),
	 * and both games use the same ones. */
	snprintf(shared_dir, sizeof shared_dir, "%s", params->data_dir);
	if (gsr_game()->tla) {
		char *slash = strrchr(shared_dir, '/');
		if (slash) snprintf(slash + 1, sizeof shared_dir - (size_t)(slash + 1 - shared_dir), "game");
		mkdir(shared_dir, 0755);
	}
	snprintf(g_other_dir, sizeof g_other_dir, "%s", params->data_dir);
	{
		char *slash = strrchr(g_other_dir, '/');
		if (slash) snprintf(slash + 1, sizeof g_other_dir - (size_t)(slash + 1 - g_other_dir), "%s", gsr_game()->tla ? "game" : "tla");
	}
	gsr_settings_init(shared_dir);
	gsr_controls_init(shared_dir);
	gsr_controls_layout(descore_surface_width(), descore_surface_height());

	/* First launch builds the game code from the ROM (about ten minutes); later launches load the
	 * saved build. Either way this starts as soon as the ROM is verified. */
	if (status == 2) gsr_prepare_start(params->data_dir, hex);

	while (descore_pump()) {
		int w = descore_surface_width(), h = descore_surface_height(), fin_rc;
		/* Portrait: the camera cutout's inset arrives from Java after a turn; lay the controls out under it. */
		if (gsr_host_portrait_top(w, h) != top_inset) {
			top_inset = gsr_host_portrait_top(w, h);
			gsr_controls_layout(w, h);
		}
		if (gsr_settings_generation() != settings_gen) {
			settings_gen = gsr_settings_generation();
			gsr_controls_touch_settings_changed();
		}
		if (status == 2 && gsr_prepare_state() == GSR_PREP_READY && !g_engine_started) start_engine(params->data_dir);

		if (g_engine_started && g_engine_err[0] == 0) {
			if (gsr_engine_finished(&fin_rc)) {
				draw_black();
				descore_swap_buffers();
				usleep(100000);
				continue;
			}
			if (gsr_host_frame_count() > 0) {
				gsr_host_wait_frame(33);
				gsr_host_render(w, h);
				gsr_controls_draw_labels(w, h);
				descore_swap_buffers();
				frame++;
				continue;
			}
			/* engine is starting: fall through to show the status screen */
		} else if (g_engine_err[0]) {
			draw_black();
			descore_swap_buffers();
			usleep(100000);
			continue;
		}
		glViewport(0, 0, w, h);
		draw_build_screen(w, h, status);
		descore_swap_buffers();
		frame++;
		usleep(gsr_prepare_state() == GSR_PREP_RUNNING ? 100000 : 33000);
	}
	gsr_host_request_quit();
	LOG("descore_game_main: leaving after %u frames", frame);
	return 0;
}

void descore_game_surface_resized(int width, int height)
{
	gsr_controls_layout(width, height);
}

void descore_game_key(int android_key_code, int down, int unicode)
{
	(void) unicode;
	gsr_controls_key(android_key_code, down);
}

void descore_game_axis(int android_axis_id, float value)
{
	gsr_controls_axis(android_axis_id, value);
}
