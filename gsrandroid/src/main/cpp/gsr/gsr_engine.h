/* gsr_engine.h -- attach the GSRecomp engine to the game libraries built on the device. */
#ifndef GSR_ENGINE_H
#define GSR_ENGINE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* After gsr_load_game(): point the engine core at the main dispatch table in the game libraries.
 * Returns 0, or -1 with a message in err. */
int gsr_engine_bind(char *err, size_t errlen);

/* Run the engine (libgsrengine.so, which references the game libraries' symbols and so can only be
 * loaded after them) as if it were its command line. Returns its exit code, or -1 with err set. */
int gsr_engine_run(int argc, char **argv, char *err, size_t errlen);

/* Start the engine on a thread of its own and return at once (the shell presents its frames). */
int gsr_engine_start(int argc, char **argv, char *err, size_t errlen);
/* 1 once the engine's main has returned (its exit code in *rc). */
int gsr_engine_finished(int *rc);

#ifdef __cplusplus
}
#endif
#endif
