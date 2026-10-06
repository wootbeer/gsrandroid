#ifndef GSR_HEADERS_H
#define GSR_HEADERS_H
#include <stddef.h>
typedef struct { const char *name; const unsigned char *data; size_t len; } GsrHeader;
extern const GsrHeader gsr_headers[]; /* terminated by name == 0 */
#endif
