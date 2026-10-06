/* gsr_text.h -- tiny 5x7 bitmap text drawn with scissored clears (no shaders, no textures, no fonts
 * to ship). Used for the first-run build screen only; the game draws its own UI. */
#ifndef GSR_TEXT_H
#define GSR_TEXT_H
#ifdef __cplusplus
extern "C" {
#endif
/* Pixel size of one glyph cell at `scale` (6 columns x 8 rows including spacing). */
int gsr_text_cell_w(int scale);
int gsr_text_cell_h(int scale);
/* Draw one line. x,y = top-left in surface pixels with y measured from the TOP of the screen.
 * Letters are upper-cased. Leaves GL_SCISSOR_TEST disabled. */
void gsr_text_draw(int surface_h, int x, int y, int scale, const char *s, float r, float g, float b);
/* Word-wrap into lines of at most max_chars and draw them; returns the number of lines drawn. */
int gsr_text_paragraph(int surface_h, int x, int y, int scale, int max_chars, const char *s, float r, float g, float b);
/* Rasterise one line into an 8-bit coverage buffer (stride bytes per row), no GL needed. The caller makes
 * sure the text fits: it covers strlen(s)*6*scale columns and 7*scale rows from (x, y). */
void gsr_text_blit(const char *s, int scale, unsigned char *dst, int stride, int x, int y);
#ifdef __cplusplus
}
#endif
#endif
