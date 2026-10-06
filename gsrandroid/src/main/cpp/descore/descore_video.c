/* descore_video.c -- indexed-color framebuffer presentation for descore-shell ports.
 *
 * Every DOS-era game descore hosts draws into an 8-bit indexed framebuffer plus a 256-entry
 * palette. Presenting is: convert through the palette to RGB on the CPU, upload the frame into a
 * power-of-two texture, draw it as one quad (stretched, or pillar/letter-boxed to a target display
 * aspect), draw the touch overlay, swap. GLES 1.x only, no shaders, so it runs everywhere the
 * older ports already do.
 *
 * The lessons baked in below were each paid for on a real device:
 *  - GLES 1.1 has no GL_UNPACK_ROW_LENGTH: glTexSubImage2D reads its source tightly packed at the
 *    width it is GIVEN, so the CPU-side RGB buffer must be packed at the frame width, not at the
 *    (larger, power-of-two) texture width. Getting this wrong smears/duplicates rows.
 *  - The Surface is not guaranteed EGL_BUFFER_PRESERVED, so any bars outside a letter/pillar-boxed
 *    quad must be cleared to black every frame.
 *  - Texture names do not survive an EGL context rebuild; descore_video_reset_gl() zeroes ours.
 *  - Swap with whatever context/surface is current on this thread, so the same code works after
 *    the Java side has rebuilt EGL.
 */

#include "descore.h"
#include "descore_internal.h"
#include "touch/descore_touch.h"

#include <EGL/egl.h>
#ifdef DESCORE_GLES3
#include <GLES3/gl3.h>
#else
#include <GLES/gl.h>
#endif
#include <stdlib.h>

static GLuint g_texture;
static int g_tex_w, g_tex_h;      /* allocated power-of-two texture size */
static uint8_t *g_rgb;            /* width*height*3, tightly packed */
static size_t g_rgb_capacity;

#ifndef DESCORE_GLES3
static int next_pow2(int v)
{
	int p = 1;
	while (p < v) p <<= 1;
	return p;
}

void descore_video_reset_gl(void)
{
	g_texture = 0;
	g_tex_w = g_tex_h = 0;
}

static void ensure_texture(int width, int height)
{
	int tw = next_pow2(width);
	int th = next_pow2(height);
	if (g_texture != 0 && tw == g_tex_w && th == g_tex_h) return;
	if (g_texture != 0) glDeleteTextures(1, &g_texture);
	glGenTextures(1, &g_texture);
	glBindTexture(GL_TEXTURE_2D, g_texture);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	/* Storage only; every visible texel is uploaded by the glTexSubImage2D below before it is
	 * ever sampled (texcoords stop at width/tw, height/th). */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tw, th, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	g_tex_w = tw;
	g_tex_h = th;
}

void descore_video_present(const uint8_t *pixels, const uint8_t *palette,
		const DescoreVideoFormat *fmt)
{
	int x, y;
	int w, h;
	int surf_w = descore_surface_width();
	int surf_h = descore_surface_height();
	float half_x = 1.0f, half_y = 1.0f;
	float u, v;
	GLfloat vertices[8];
	GLfloat texcoords[8];
	unsigned scale_num, scale_den;
	size_t need;

	if (pixels == NULL || palette == NULL || fmt == NULL) return;
	w = fmt->width;
	h = fmt->height;
	if (w <= 0 || h <= 0 || surf_w <= 0 || surf_h <= 0) return;

	need = (size_t)w * (size_t)h * 3;
	if (need > g_rgb_capacity) {
		uint8_t *grown = (uint8_t *)realloc(g_rgb, need);
		if (grown == NULL) return;
		g_rgb = grown;
		g_rgb_capacity = need;
	}

	/* VGA DAC values are 0-63; scale to a full byte. 8-bit palettes pass straight through. */
	scale_num = (fmt->palette_bits == 6) ? 255u : 1u;
	scale_den = (fmt->palette_bits == 6) ? 63u : 1u;
	for (y = 0; y < h; ++y) {
		const uint8_t *src = pixels + (size_t)y * (size_t)w;
		uint8_t *dst = g_rgb + (size_t)y * (size_t)w * 3;
		for (x = 0; x < w; ++x) {
			unsigned c = src[x];
			dst[x * 3 + 0] = (uint8_t)((unsigned)palette[c * 3 + 0] * scale_num / scale_den);
			dst[x * 3 + 1] = (uint8_t)((unsigned)palette[c * 3 + 1] * scale_num / scale_den);
			dst[x * 3 + 2] = (uint8_t)((unsigned)palette[c * 3 + 2] * scale_num / scale_den);
		}
	}

	ensure_texture(w, h);
	glViewport(0, 0, surf_w, surf_h);

	if (fmt->aspect == DESCORE_ASPECT_FIT && fmt->dar_num > 0 && fmt->dar_den > 0) {
		float target = (float)fmt->dar_num / (float)fmt->dar_den;
		float actual = (float)surf_w / (float)surf_h;
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		if (actual > target) {
			half_x = target / actual; /* wider than target: pillarbox */
		} else {
			half_y = actual / target; /* taller than target: letterbox */
		}
	}

	u = (float)w / (float)g_tex_w;
	v = (float)h / (float)g_tex_h;
	vertices[0] = -half_x; vertices[1] = -half_y;
	vertices[2] = half_x;  vertices[3] = -half_y;
	vertices[4] = -half_x; vertices[5] = half_y;
	vertices[6] = half_x;  vertices[7] = half_y;
	texcoords[0] = 0.0f; texcoords[1] = v;
	texcoords[2] = u;    texcoords[3] = v;
	texcoords[4] = 0.0f; texcoords[5] = 0.0f;
	texcoords[6] = u;    texcoords[7] = 0.0f;

	glBindTexture(GL_TEXTURE_2D, g_texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1); /* rows are w*3 bytes, not necessarily 4-aligned */
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, g_rgb);

	glEnable(GL_TEXTURE_2D);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisable(GL_TEXTURE_2D);

	/* On-screen buttons, drawn as a separate screen-space overlay on top of the frame. No-ops
	 * while a gamepad is connected or before the layout exists. */
	descore_touch_draw();

	eglSwapBuffers(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW));
}
#else
void descore_video_reset_gl(void) {}
void descore_video_present(const uint8_t *pixels, const uint8_t *palette, const DescoreVideoFormat *fmt)
{
	(void) pixels; (void) palette; (void) fmt; /* indexed-colour present is GLES 1.x only */
}
#endif

void descore_swap_buffers(void)
{
	descore_touch_draw();
	eglSwapBuffers(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW));
}

