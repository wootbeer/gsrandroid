/* descore_internal.h -- calls between descore's own .c files. Games include descore.h only. */
#ifndef DESCORE_INTERNAL_H
#define DESCORE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

/* Forget every GL object name: they died with the old EGL context. Called after EGL is rebuilt. */
void descore_video_reset_gl(void);

/* Mixes every registered source into out (interleaved stereo S16, DESCORE_AUDIO_RATE). Always
 * writes `frames` frames (silence when nothing plays) and returns `frames`. */
size_t descore_audio_render(int16_t *out, size_t frames);

#endif
