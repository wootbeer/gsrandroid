/* gsr_host.h -- the seam between the engine's HostWindow (running on the engine thread, in the engine core)
 * and the Android shell (render thread, UI-thread input, Java audio thread).
 *
 *   engine thread  --frames-->  mailbox  --> render thread (GLES3 present + touch overlay)
 *   engine thread  --audio--->  resampler -> Descore mixer source (Java audio thread pulls)
 *   UI thread      --keys/touch--> gsr_controls --> bitmask  --> engine thread polls it each pump()
 */
#ifndef GSR_HOST_H
#define GSR_HOST_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ---- engine side (called from host_window_android.cpp) ---- */
void gsr_host_frame_submit(const uint8_t *rgb888, int w, int h);
void gsr_host_audio_open(int channels);
void gsr_host_audio_push(const int16_t *samples, int frames); /* interleaved, 65536 Hz */
void gsr_host_audio_reset(void);
void gsr_host_audio_close(void);
void gsr_host_surface_size(int *w, int *h);

typedef struct {
	uint16_t keyinput;      /* GBA KEYINPUT, active low */
	int fast_forward;
	int paused;             /* app is in the background / locked */
	int quit;
} GsrHostInput;
void gsr_host_poll(GsrHostInput *out);
/* Called by the engine once it is running / finished, so the render loop knows. */
void gsr_host_engine_state(int running);
int gsr_host_frame_count(void);

/* ---- render side (called from gsr_main.c on the Descore render thread) ---- */
/* Wait up to timeout_ms for a new frame. Returns 1 if there is one. */
int gsr_host_wait_frame(int timeout_ms);
/* Draw the newest frame (and the touch labels) to the current surface. Does not swap. */
void gsr_host_render(int surface_w, int surface_h);
void gsr_host_request_quit(void);
/* The settings menu is open over the game: the guest is paused while it is. */
void gsr_host_set_menu_open(int open);
/* The display's current refresh rate, set by the Java side (changes when the 120 Hz mode is requested). */
void gsr_host_set_display_hz(float hz);
void gsr_host_set_debug(int on);  /* diagnostics (logcat statistics, engine log forwarding): debuggable builds only */
int gsr_host_debug(void);
float gsr_host_display_hz(void);
/* The engine is presenting two frames per game frame: keep a few frames queued instead of only the newest. */
void gsr_host_set_interpolating(int on);

/* ---- GPU field renderer (engine side asks, menu side reads) ---- */
/* 1 when the engine should draw field scenes with the GPU: the Renderer setting, and for Auto the expanded
 * view. Read every frame, so changing the setting takes effect live. */
int gsr_host_gpu_wanted(void);
/* The engine reports whether the GPU renderer could be started (1 ok, 2 failed) ... */
void gsr_host_gpu_report_init(int ok);
/* ... and, each frame it is asked to draw, whether the GPU actually drew it. */
void gsr_host_gpu_report_frame(int drew);
int gsr_host_gpu_status(void); /* 0 not tried yet, 1 available, 2 unavailable */
int gsr_host_gpu_active(void); /* 1 if the last frame was drawn by the GPU */

#ifdef __cplusplus
}
#endif
#endif
