/* gsr_prepare.h -- first-run (and rebuild) of the game code on the device, cached afterwards.
 *
 * The player's own ROM goes through GSRecomp's translator and our on-device compiler and linker; the
 * result is a handful of libraries in the app's private folder. Nothing derived from the game ships
 * in the APK. A stamp file (ROM SHA-1 + builder version) lets later launches skip the build and just
 * load the libraries. */
#ifndef GSR_PREPARE_H
#define GSR_PREPARE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
	GSR_PREP_IDLE = 0,    /* nothing started (no verified ROM yet) */
	GSR_PREP_RUNNING = 1, /* translating / compiling / loading */
	GSR_PREP_READY = 2,   /* game libraries are loaded */
	GSR_PREP_FAILED = 3
};

/* Bump when the builder, its data, or what the game code expects from the engine changes: every
 * install then rebuilds once. */
#define GSR_BUILD_VERSION "gsr-android-2"

/* Start preparing in a background thread. data_dir is Descore's game data folder
 * (<files>/gsr/game); rom_sha1 is the verified SHA-1 of the ROM stored there. */
void gsr_prepare_start(const char *data_dir, const char *rom_sha1);

int gsr_prepare_state(void);
int gsr_prepare_progress(void);  /* 0..1000 */
int gsr_prepare_cached(void);    /* 1 if this run only loaded an existing build */
int gsr_prepare_elapsed_s(void);
void gsr_prepare_message(char *buf, size_t n); /* current step, e.g. "Compiling the game on this device" */
void gsr_prepare_error(char *buf, size_t n);   /* why it failed (empty otherwise) */
void gsr_prepare_log_path(char *buf, size_t n);

#ifdef __cplusplus
}
#endif
#endif
