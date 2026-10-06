/* Text shims that turn the generated C++-flavoured shards into plain C for tcc.
 * The shards contain no C++ logic; only declaration syntax differs. Nothing
 * here is Golden Sun specific: it only rewrites declaration syntax. */
#ifndef GSR_SHIM_H
#define GSR_SHIM_H
#include <stddef.h>
/* Returns a malloc'd, NUL-terminated buffer (caller frees); *out_len optional. */
char *gsr_shim_text(const char *in, size_t n, size_t *out_len);
#endif
