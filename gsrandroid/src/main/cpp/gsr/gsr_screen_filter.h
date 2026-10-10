/* gsr_screen_filter.h -- optional screen filters for the final picture (Display > Screen filter). */
#ifndef GSR_SCREEN_FILTER_H
#define GSR_SCREEN_FILTER_H
#ifdef __cplusplus
extern "C" {
#endif

/* CRT is CRT Lottes with its screen curvature (warpX/warpY) set to 0: a flat picture. */
/* SCANLINES is this port's own: scanlines only, no mask, glow or blur. */
enum { GSR_FILTER_OFF = 0, GSR_FILTER_LCD = 1, GSR_FILTER_CRT = 2, GSR_FILTER_XBR = 3, GSR_FILTER_SCANLINES = 4,
       GSR_FILTER_COUNT = 5 };

/* Draws the texture bound to GL_TEXTURE0 through `filter` into the current viewport. rect is the destination
 * in normalized device coordinates (x0, y0 bottom-left, x1, y1 top-right), uvr the part of the texture shown
 * (u0, v0 top-left, u1, v1 bottom-right; texture row 0 is the top of the picture). src_w x src_h is the picture's
 * size in original (1x) pixels, out_w x out_h the destination's size in screen pixels. Returns 0 when the filter
 * is off or its shader is unavailable on this device; the caller then draws its plain copy. */
int gsr_screen_filter_draw(int filter, const float rect[4], const float uvr[4],
                           float src_w, float src_h, float out_w, float out_h);

#ifdef __cplusplus
}
#endif
#endif
