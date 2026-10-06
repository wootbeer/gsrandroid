#include "gsr_cc.h"
#include "gsr_headers.h"
#include "gsr_shim.h"
#include "libtcc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct { char *buf; size_t cap; } ErrBuf;

static void on_tcc_error(void *opaque, const char *msg) {
	ErrBuf *e = (ErrBuf *)opaque;
	if (!e || !e->buf) return;
	size_t have = strlen(e->buf);
	if (have + 2 >= e->cap) return; /* keep the first messages */
	snprintf(e->buf + have, e->cap - have, "%s%s", have ? "\n" : "", msg);
}

static void set_err(char *err, size_t errlen, const char *msg) {
	if (err && errlen) snprintf(err, errlen, "%s", msg);
}

static char *read_all(const char *path, size_t *n) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *b = (char *)malloc((size_t)len + 1);
	if (!b) { fclose(f); return NULL; }
	size_t got = fread(b, 1, (size_t)len, f);
	fclose(f);
	b[got] = 0;
	*n = got;
	return b;
}

static int write_all(const char *path, const void *data, size_t n) {
	FILE *f = fopen(path, "wb");
	if (!f) return -1;
	size_t put = fwrite(data, 1, n, f);
	int rc = fclose(f);
	return (put == n && rc == 0) ? 0 : -1;
}

static void mkdir_p(const char *dir) {
	char tmp[1024];
	snprintf(tmp, sizeof tmp, "%s", dir);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
	}
	mkdir(tmp, 0755);
}

int gsr_cc_write_headers(const char *dir) {
	mkdir_p(dir);
	for (const GsrHeader *h = gsr_headers; h->name; h++) {
		char path[1024];
		snprintf(path, sizeof path, "%s/%s", dir, h->name);
		if (write_all(path, h->data, h->len) != 0) return -1;
	}
	return 0;
}

int gsr_cc_shim_header(const char *in_path, const char *out_path) {
	size_t n = 0, m = 0;
	char *src = read_all(in_path, &n);
	if (!src) return -1;
	char *out = gsr_shim_text(src, n, &m);
	free(src);
	if (!out) return -1;
	int rc = write_all(out_path, out, m);
	free(out);
	return rc;
}

int gsr_cc_compile(const char *src_path, const char *inc1, const char *inc2,
                   const char *out_obj, char *err, size_t errlen) {
	if (err && errlen) err[0] = 0;
	size_t n = 0, m = 0;
	char *src = read_all(src_path, &n);
	if (!src) { set_err(err, errlen, "cannot read source"); return -1; }
	char *shimmed = gsr_shim_text(src, n, &m);
	free(src);
	if (!shimmed) { set_err(err, errlen, "out of memory"); return -1; }

	ErrBuf eb = { err, errlen };
	TCCState *s = tcc_new();
	if (!s) { free(shimmed); set_err(err, errlen, "tcc_new failed"); return -1; }
	tcc_set_error_func(s, &eb, on_tcc_error);
	tcc_set_options(s, "-nostdinc -nostdlib -w");
	tcc_define_symbol(s, "NDEBUG", "1");
	tcc_define_symbol(s, "GBARECOMP_OUTLINE_BUS", "1");
	if (inc1) tcc_add_include_path(s, inc1);
	if (inc2) tcc_add_include_path(s, inc2);
	tcc_set_output_type(s, TCC_OUTPUT_OBJ);
	int rc = tcc_compile_string(s, shimmed);
	free(shimmed);
	if (rc == 0) rc = tcc_output_file(s, out_obj);
	tcc_delete(s);
	return rc == 0 ? 0 : -1;
}

int gsr_cc_link(const char *const *objs, int nobjs, const char *host_so,
                const char *out_so, char *err, size_t errlen) {
	if (err && errlen) err[0] = 0;
	ErrBuf eb = { err, errlen };
	TCCState *s = tcc_new();
	if (!s) { set_err(err, errlen, "tcc_new failed"); return -1; }
	tcc_set_error_func(s, &eb, on_tcc_error);
	tcc_set_options(s, "-nostdlib -w");
	tcc_set_output_type(s, TCC_OUTPUT_DLL);
	int rc = 0;
	for (int i = 0; i < nobjs && rc == 0; i++) rc = tcc_add_file(s, objs[i]);
	if (rc == 0 && host_so && host_so[0]) rc = tcc_add_file(s, host_so);
	if (rc == 0) rc = tcc_output_file(s, out_so);
	tcc_delete(s);
	return rc == 0 ? 0 : -1;
}

int gsr_cc_link_bridge(const char *const *game_libs, int n, const char *out_so, char *err, size_t errlen) {
	if (err && errlen) err[0] = 0;
	ErrBuf eb = { err, errlen };
	TCCState *s = tcc_new();
	if (!s) { set_err(err, errlen, "tcc_new failed"); return -1; }
	tcc_set_error_func(s, &eb, on_tcc_error);
	tcc_set_options(s, "-nostdlib -w -soname libgsgamebridge.so");
	tcc_set_output_type(s, TCC_OUTPUT_DLL);
	int rc = tcc_compile_string(s, "int gsr_bridge_marker = 1;\n");
	for (int i = 0; i < n && rc == 0; i++) rc = tcc_add_file(s, game_libs[i]);
	if (rc == 0) rc = tcc_output_file(s, out_so);
	tcc_delete(s);
	return rc == 0 ? 0 : -1;
}
