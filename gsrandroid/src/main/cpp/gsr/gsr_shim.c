#include "gsr_shim.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

/* Append src (len bytes) to out at *o, replacing the few C++-only spellings. */
static void emit_line(char *out, size_t *o, const char *l, size_t len) {
    size_t i = 0;
    while (i < len) {
        if (l[i] == 'e' && i + 11 <= len && memcmp(l + i, "extern \"C\" ", 11) == 0) {
            memcpy(out + *o, "extern ", 7); *o += 7; i += 11; continue;
        }
        if (l[i] == '#' && i + 18 <= len && memcmp(l + i, "#include <cstdint>", 18) == 0) {
            memcpy(out + *o, "#include <stdint.h>", 19); *o += 19; i += 18; continue;
        }
        if (l[i] == 's' && i + 5 <= len && memcmp(l + i, "std::", 5) == 0 &&
            (i == 0 || !((l[i-1] >= 'a' && l[i-1] <= 'z') || (l[i-1] >= 'A' && l[i-1] <= 'Z') || l[i-1] == '_'))) {
            i += 5; continue;
        }
        out[(*o)++] = l[i++];
    }
}

char *gsr_shim_text(const char *in, size_t n, size_t *out_len) {
    char *out = (char *)malloc(n + 8192);
    if (!out) return NULL;
    size_t o = 0, i = 0;
    char pending[96]; pending[0] = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && in[e] != '\n') e++;
        const char *l = in + i; size_t len = e - i;
        char tmp[512];
        int handled = 0;
        if (len < sizeof tmp) {
            memcpy(tmp, l, len); tmp[len] = 0;
            if (starts(tmp, "extern \"C\" {")) {
                handled = 1; /* drop the block opener, keep line numbering */
            } else if (tmp[0] == '}' && strstr(tmp, "/* extern")) {
                handled = 1;
            } else if (starts(tmp, "struct ") && strchr(tmp, '{')) {
                /* struct Name { ... };  or a multi-line struct Name {  */
                char name[96]; size_t k = 0; const char *p = tmp + 7;
                while (*p && *p != ' ' && *p != '{' && k < sizeof name - 1) name[k++] = *p++;
                name[k] = 0;
                if (len >= 2 && tmp[len-1] == ';' && tmp[len-2] == '}') {
                    memcpy(out + o, "typedef ", 8); o += 8;
                    memcpy(out + o, tmp, len - 1); o += len - 1;
                    o += (size_t)sprintf(out + o, " %s;", name);
                    handled = 1;
                } else if (tmp[len-1] == '{') {
                    memcpy(out + o, "typedef ", 8); o += 8;
                    memcpy(out + o, tmp, len); o += len;
                    strcpy(pending, name);
                    handled = 1;
                }
            } else if (pending[0] && strcmp(tmp, "};") == 0) {
                o += (size_t)sprintf(out + o, "} %s;", pending);
                pending[0] = 0;
                handled = 1;
            }
        }
        if (!handled) emit_line(out, &o, l, len);
        if (e < n) out[o++] = '\n';
        i = e + 1;
    }
    out[o] = 0;
    if (out_len) *out_len = o;
    return out;
}
