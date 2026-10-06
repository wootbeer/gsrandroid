/* gsr_cc.h -- on-device C compiler (libtcc) wrapper for the translated game code.
 *
 * The translated game code is generated on the player's device from their own ROM and is never
 * shipped. These helpers compile one shard to an object file and link all objects into a shared
 * library that the app dlopen()s. */
#ifndef GSR_CC_H
#define GSR_CC_H
#include <stddef.h>

/* Write the embedded freestanding headers into dir (created if needed). 0 on success. */
int gsr_cc_write_headers(const char *dir);

/* Shim a shard (C++-flavoured declaration syntax -> C) and compile it to an object file.
 * inc1/inc2 are include directories (the shard folder's recompiled.h copy, and the header dir).
 * Returns 0 on success, otherwise -1 with a message in err. */
int gsr_cc_compile(const char *src_path, const char *inc1, const char *inc2,
                   const char *out_obj, char *err, size_t errlen);

/* Shim a header (recompiled.h) into out_path. 0 on success. */
int gsr_cc_shim_header(const char *in_path, const char *out_path);

/* Link objects into a shared library. host_so is the path of the library that exports the
 * engine symbols (the new library gets a DT_NEEDED entry for it). 0 on success. */
int gsr_cc_link(const char *const *objs, int nobjs, const char *host_so,
                const char *out_so, char *err, size_t errlen);
/* Make the bridge library: a tiny shared library (soname libgsgamebridge.so) that lists the game
 * libraries as DT_NEEDED. libgsrengine.so is linked against an empty stand-in of the same name, so once
 * the real bridge is loaded first the engine's lookups for game symbols reach every game library through
 * it (Android resolves a library's undefined symbols only through its own dependencies). */
int gsr_cc_link_bridge(const char *const *game_libs, int n, const char *out_so, char *err, size_t errlen);
#endif
