/* gsr_build.h -- turn a folder of translated game code into loadable libraries, on the device.
 *
 *   gen/<folder>/recompiled*.cpp, dispatch_table.cpp, stamp_registry.cpp, recompiled.h
 *     --compile (libtcc, parallel worker processes)--> objects
 *     --link (libtcc, folders packed into <=120 MB-of-objects libraries)--> libgsgame<N>.so
 *
 * The libraries and a manifest (gsgame_libs.txt, one file name per line) go to out_dir. */
#ifndef GSR_BUILD_H
#define GSR_BUILD_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*GsrLogFn)(const char *line);
typedef void (*GsrProgressFn)(int permille); /* 0..1000 over the whole compile+link */

typedef struct {
	const char *gen_dir;   /* translated code */
	const char *work_dir;  /* scratch (objects, shimmed headers); may be deleted afterwards */
	const char *out_dir;   /* libraries + manifest */
	const char *host_so;   /* library exporting the engine symbols, or NULL/"" to detect (dladdr) */
	int workers;           /* parallel compile processes, 1..16 */
	int free_sources;      /* delete each shard's source once it compiled (lower peak storage; no retry from gen_dir) */
	GsrLogFn log;
	GsrProgressFn progress; /* may be NULL */
} GsrBuildParams;

/* Returns the number of libraries written (>0), or -1 with a message in err. */
int gsr_build_game(const GsrBuildParams *p, char *err, size_t errlen);

/* dlopen every library in out_dir's manifest. Returns the count loaded, -1 if a game library will not load
 * (the build is bad), or -2 if only the bridge to the engine failed (the build is fine). Message in err. */
int gsr_load_game(const char *out_dir, char *err, size_t errlen);

/* Look a symbol up in the loaded game libraries (NULL if none has it). */
void *gsr_game_sym(const char *name);

#ifdef __cplusplus
}
#endif
#endif
