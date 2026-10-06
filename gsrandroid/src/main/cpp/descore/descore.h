/* descore.h -- the native half of the shared "descore" Android shell.
 *
 * A game links the descore .c files into its own shared library, implements the three descore_game_*
 * entry points below, and gets: the Android surface/EGL lifecycle (pause and resume across screen
 * lock without restarting the game), an indexed-color video present with aspect handling and the
 * on-screen touch overlay, a small real-time audio mixer, raw keyboard/gamepad/touch delivery, and
 * quit/keyboard bridges. The matching Java side is wootbeer.descore.DescoreView.
 *
 * Threading model (same as every descore port): descore_game_main() runs on the render thread,
 * the one thread that owns the EGL context. The game's main loop must call descore_pump() at
 * least once per frame; that is the only place the render thread ever parks for pause/resume.
 * Key/axis/touch callbacks arrive on the Android UI thread; audio is pulled from Java's audio
 * thread. Anything shared between them is the game's responsibility to make safe (the descore
 * mixer takes its own lock).
 */
#ifndef DESCORE_H
#define DESCORE_H

#include <jni.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DESCORE_AUDIO_RATE 44100 /* keep in sync with DescoreView.SAMPLE_RATE */

/* ---- What the game implements -------------------------------------------------------------- */

typedef struct DescoreGameParams {
	const char *data_dir; /* absolute path of the picked unit's private data folder */
	int unit_id;          /* DescoreGameConfig.Unit.id chosen in the launcher */
	int surface_w;        /* initial surface size in pixels */
	int surface_h;
} DescoreGameParams;

/* Runs the whole game on the render thread. Return when the game ends; descore then asks Java to
 * quit the app. */
int descore_game_main(const DescoreGameParams *params);

/* Raw Android KeyEvent key codes (KEYCODE_*), UI thread. unicode is the character the key
 * produces, or 0. */
void descore_game_key(int android_key_code, int down, int unicode);

/* Raw Android MotionEvent axis id (AXIS_X, AXIS_HAT_X, ...) and its new value, UI thread. */
void descore_game_axis(int android_axis_id, float value);

/* ---- What descore provides -------------------------------------------------------------- */

/* Per-frame checkpoint. Parks the render thread while the app is paused or its Surface is gone,
 * and rebuilds EGL against the new Surface afterwards. Returns 0 once a stop was requested. */
int descore_pump(void);

/* Ask the Java side to finish the app (also what happens automatically when descore_game_main
 * returns). */
void descore_request_exit(void);

int descore_surface_width(void);
int descore_surface_height(void);
int descore_gamepad_connected(void);
/* Non-zero while the render thread is parked in descore_pump() because the app is in the background or locked. */
int descore_is_paused(void);

/* Optional game hook (weak in descore): the surface's real pixel size changed. */
void descore_game_surface_resized(int width, int height);

/* Ask the Java side to open the game's settings menu (a no-op unless the activity set a listener). Any thread. */
void descore_request_menu(void);

void descore_show_keyboard(void);
void descore_hide_keyboard(void);

/* ---- Video ---------------------------------------------------------------------------------- */

typedef enum DescoreAspect {
	DESCORE_ASPECT_STRETCH = 0, /* fill the whole surface */
	DESCORE_ASPECT_FIT = 1      /* letter/pillar-box to dar_num:dar_den, black bars elsewhere */
} DescoreAspect;

typedef struct DescoreVideoFormat {
	int width, height;         /* size of the indexed framebuffer */
	int palette_bits;          /* 6 for VGA DAC values (0-63), 8 for 0-255 */
	DescoreAspect aspect;
	int dar_num, dar_den;      /* display aspect ratio for DESCORE_ASPECT_FIT, e.g. 4 and 3 */
} DescoreVideoFormat;

/* Converts the 8-bit indexed framebuffer through the 256*3 byte palette, draws it as one quad,
 * draws the touch overlay, and swaps. Render thread only. */
void descore_video_present(const uint8_t *pixels, const uint8_t *palette,
		const DescoreVideoFormat *fmt);

/* GLES 3 games draw with their own GL calls and finish a frame with this: draws the touch overlay
 * (unless a gamepad is connected) and swaps. Render thread only. The overlay changes GL state (program,
 * blend, vertex arrays): the caller must treat its cached GL state as invalid afterwards. */
void descore_swap_buffers(void);

/* ---- Audio ---------------------------------------------------------------------------------- */

/* A mixer source: fill up to `frames` interleaved stereo S16 frames at DESCORE_AUDIO_RATE into
 * out (contents on entry are undefined - overwrite what you produce) and return how many frames were
 * produced; the mixer treats the rest as silence. Called from the Java audio thread WITH the mixer
 * lock held: a source may take its own locks but must not call descore_audio_add/remove/set_*.
 * Return 0 when idle. */
typedef size_t (*DescoreAudioSourceFn)(void *user, int16_t *out, size_t frames);

/* Returns a handle (>= 0) or -1 when the fixed source table is full. */
int descore_audio_add_source(DescoreAudioSourceFn fn, void *user);
/* A barrier: once this returns the audio thread is not inside that source's callback, so the
 * caller may free the source's state. */
void descore_audio_remove_source(int handle);
/* 0.0 .. 1.0 (values above 1.0 amplify, with hard clipping). Applied to that source only. */
void descore_audio_set_source_gain(int handle, float gain);
void descore_audio_set_master_gain(float gain);

#ifdef __cplusplus
}
#endif

#endif
