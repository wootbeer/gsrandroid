#define _GNU_SOURCE
#include "gsr_build.h"
#include "gsr_cc.h"

#include <dirent.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct { char src[600]; char inc1[600]; char obj[600]; char dir[200]; int part; } Task;
typedef struct { char name[200]; long bytes; int part; } DirInfo;

/* arm64 direct branches reach +-128 MiB, so one library's code must stay well below that. The generated
 * folders never call each other (only the engine), so they can be packed into libraries freely. */
#define GSR_PART_CAP_BYTES (120L * 1024 * 1024) /* object bytes per library (code is about half) */
#define GSR_MAX_PARTS 16

typedef struct {
	atomic_int next, done, fails;
	char errs[6][400];
	int ms[1]; /* ntasks entries follow (allocated bigger) */
} Shared;

static double now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void logf_(const GsrBuildParams *p, const char *fmt, ...) {
	char line[900];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	if (p->log) p->log(line);
}

static void mkdir_p(const char *dir) {
	char tmp[1024];
	snprintf(tmp, sizeof tmp, "%s", dir);
	for (char *q = tmp + 1; *q; q++) {
		if (*q == '/') { *q = 0; mkdir(tmp, 0755); *q = '/'; }
	}
	mkdir(tmp, 0755);
}

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static int ends_with(const char *s, const char *suf) {
	size_t a = strlen(s), b = strlen(suf);
	return a >= b && strcmp(s + a - b, suf) == 0;
}

static int cmp_task(const void *a, const void *b) { return strcmp(((const Task *)a)->src, ((const Task *)b)->src); }
static int cmp_dir_desc(const void *a, const void *b) {
	long x = ((const DirInfo *)a)->bytes, y = ((const DirInfo *)b)->bytes;
	return x < y ? 1 : (x > y ? -1 : 0);
}

static void fail_msg(char *err, size_t errlen, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	if (err && errlen) vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

int gsr_build_game(const GsrBuildParams *p, char *err, size_t errlen) {
	char hdr[700], inc[700], path[1300];
	int workers = p->workers < 1 ? 1 : (p->workers > 16 ? 16 : p->workers);
	Task *tasks = NULL;
	int nt = 0, cap = 0, nparts = 0, ret = -1;
	DirInfo *dirs = NULL;
	Shared *sh = MAP_FAILED;
	size_t shsize = 0;

	snprintf(hdr, sizeof hdr, "%s/hdr", p->work_dir);
	snprintf(inc, sizeof inc, "%s/inc", p->work_dir);
	mkdir_p(p->work_dir);
	mkdir_p(p->out_dir);
	if (gsr_cc_write_headers(hdr) != 0) { fail_msg(err, errlen, "cannot write headers to %s", hdr); return -1; }

	/* ---- enumerate shards ---- */
	DIR *gd = opendir(p->gen_dir);
	if (!gd) { fail_msg(err, errlen, "no translated code at %s", p->gen_dir); return -1; }
	struct dirent *de;
	while ((de = readdir(gd))) {
		if (de->d_name[0] == '.') continue;
		char sub[900], subinc[900], rh[900];
		snprintf(sub, sizeof sub, "%s/%s", p->gen_dir, de->d_name);
		DIR *sd = opendir(sub);
		if (!sd) continue;
		snprintf(subinc, sizeof subinc, "%s/%s", inc, de->d_name);
		mkdir_p(subinc);
		snprintf(rh, sizeof rh, "%s/recompiled.h", sub);
		if (exists(rh)) {
			char oh[1000];
			snprintf(oh, sizeof oh, "%s/recompiled.h", subinc);
			if (gsr_cc_shim_header(rh, oh) != 0) logf_(p, "warn: could not shim %s", rh);
		}
		struct dirent *fe;
		while ((fe = readdir(sd))) {
			const char *n = fe->d_name;
			int take = (!strncmp(n, "recompiled", 10) && ends_with(n, ".cpp")) ||
			           !strcmp(n, "dispatch_table.cpp") || !strcmp(n, "stamp_registry.cpp");
			if (!take) continue;
			if (nt == cap) { cap = cap ? cap * 2 : 1024; tasks = realloc(tasks, (size_t)cap * sizeof(Task)); }
			Task *t = &tasks[nt++];
			snprintf(t->src, sizeof t->src, "%s/%s", sub, n);
			snprintf(t->inc1, sizeof t->inc1, "%s", subinc);
			snprintf(t->dir, sizeof t->dir, "%s", de->d_name);
			char base[300];
			snprintf(base, sizeof base, "%s", n);
			base[strlen(base) - 4] = 0;
			snprintf(t->obj, sizeof t->obj, "%s/obj_%s__%s.o", p->work_dir, de->d_name, base);
			t->part = 0;
		}
		closedir(sd);
	}
	closedir(gd);
	if (nt == 0) { fail_msg(err, errlen, "no translated shards under %s", p->gen_dir); return -1; }
	qsort(tasks, (size_t)nt, sizeof(Task), cmp_task);
	logf_(p, "shards: %d, workers: %d", nt, workers);

	/* ---- compile in worker processes (libtcc keeps global state, so processes, not threads) ---- */
	shsize = sizeof(Shared) + (size_t)nt * sizeof(int);
	sh = mmap(NULL, shsize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (sh == MAP_FAILED) { fail_msg(err, errlen, "mmap failed"); goto done; }
	memset(sh, 0, shsize);
	double t_comp = now_ms();
	pid_t pids[16];
	for (int w = 0; w < workers; w++) {
		pid_t pid = fork();
		if (pid == 0) {
			for (;;) {
				int i = atomic_fetch_add(&sh->next, 1);
				if (i >= nt) break;
				char e2[400];
				double t0 = now_ms();
				int rc = gsr_cc_compile(tasks[i].src, tasks[i].inc1, hdr, tasks[i].obj, e2, sizeof e2);
				sh->ms[i] = rc == 0 ? (int)(now_ms() - t0) : -1;
				if (rc == 0 && p->free_sources) unlink(tasks[i].src); /* keeps peak storage low */
				if (rc != 0) {
					int k = atomic_fetch_add(&sh->fails, 1);
					if (k < 6) snprintf(sh->errs[k], sizeof sh->errs[k], "%s: %s", tasks[i].src, e2);
				}
				atomic_fetch_add(&sh->done, 1);
			}
			_exit(0);
		}
		pids[w] = pid;
	}
	long max_rss_kb = 0;
	int alive = workers;
	while (alive > 0) {
		usleep(100000);
		if (p->progress) p->progress(atomic_load(&sh->done) * 880 / nt);
		for (int w = 0; w < workers; w++) {
			if (pids[w] <= 0) continue;
			int st; struct rusage ru;
			pid_t r = wait4(pids[w], &st, WNOHANG, &ru);
			if (r == pids[w]) {
				pids[w] = 0; alive--;
				if (ru.ru_maxrss > max_rss_kb) max_rss_kb = ru.ru_maxrss;
			}
		}
	}
	double comp_ms = now_ms() - t_comp;
	long sum_ms = 0; int maxi = 0, okn = 0;
	for (int i = 0; i < nt; i++) {
		if (sh->ms[i] >= 0) { okn++; sum_ms += sh->ms[i]; if (sh->ms[i] > sh->ms[maxi]) maxi = i; }
	}
	logf_(p, "compile: %d ok, %d failed, wall %.1f s, sum of per-file %.1f s, slowest %d ms",
		okn, nt - okn, comp_ms / 1000.0, sum_ms / 1000.0, sh->ms[maxi]);
	logf_(p, "compile: peak worker RSS %ld MB", max_rss_kb / 1024);
	for (int k = 0; k < 6 && k < sh->fails; k++) logf_(p, "  error: %s", sh->errs[k]);
	if (okn != nt) { fail_msg(err, errlen, "%d shards failed to compile: %s", nt - okn, sh->errs[0]); goto done; }

	/* ---- pack folders into libraries (arm64 branch range), then link each in its own child ---- */
	char host[700] = "";
	if (p->host_so && p->host_so[0]) snprintf(host, sizeof host, "%s", p->host_so);
	else {
		Dl_info di;
		if (dladdr((void *)gsr_build_game, &di) && di.dli_fname) snprintf(host, sizeof host, "%s", di.dli_fname);
	}
	logf_(p, "host library for DT_NEEDED: %s", host);

	dirs = calloc((size_t)nt, sizeof(DirInfo));
	int nd = 0;
	for (int i = 0; i < nt; i++) {
		int k = 0;
		while (k < nd && strcmp(dirs[k].name, tasks[i].dir) != 0) k++;
		if (k == nd) { snprintf(dirs[nd].name, sizeof dirs[nd].name, "%s", tasks[i].dir); nd++; }
		struct stat os_;
		if (stat(tasks[i].obj, &os_) == 0) dirs[k].bytes += (long)os_.st_size;
	}
	qsort(dirs, (size_t)nd, sizeof(DirInfo), cmp_dir_desc);
	long load[GSR_MAX_PARTS] = {0};
	for (int k = 0; k < nd; k++) {
		int pidx = -1;
		for (int q = 0; q < nparts; q++) if (load[q] + dirs[k].bytes <= GSR_PART_CAP_BYTES) { pidx = q; break; }
		if (pidx < 0) {
			if (nparts >= GSR_MAX_PARTS) { fail_msg(err, errlen, "too many libraries"); goto done; }
			pidx = nparts++;
		}
		dirs[k].part = pidx; load[pidx] += dirs[k].bytes;
	}
	for (int i = 0; i < nt; i++)
		for (int k = 0; k < nd; k++) if (strcmp(dirs[k].name, tasks[i].dir) == 0) { tasks[i].part = dirs[k].part; break; }
	logf_(p, "folders: %d, packed into %d libraries (cap %ld MB of objects each)", nd, nparts, GSR_PART_CAP_BYTES / (1024 * 1024));

	/* old libraries from a previous build must not survive */
	snprintf(path, sizeof path, "%s/gsgame_libs.txt", p->out_dir);
	remove(path);
	double link_total_ms = 0;
	long link_max_rss = 0;
	for (int q = 0; q < nparts; q++) {
		const char **objs = malloc(sizeof(char *) * (size_t)nt);
		int no = 0;
		for (int i = 0; i < nt; i++) if (tasks[i].part == q) objs[no++] = tasks[i].obj;
		char so[800];
		snprintf(so, sizeof so, "%s/libgsgame%d.so", p->out_dir, q);
		if (p->progress) p->progress(880 + 110 * q / nparts);
		double t_link = now_ms();
		pid_t lp = fork();
		if (lp == 0) {
			char e2[400];
			int rc = gsr_cc_link(objs, no, host, so, e2, sizeof e2);
			if (rc != 0) snprintf(sh->errs[5], sizeof sh->errs[5], "link: %s", e2);
			_exit(rc == 0 ? 0 : 1);
		}
		int lst = 0; struct rusage lru;
		wait4(lp, &lst, 0, &lru);
		double ms = now_ms() - t_link;
		link_total_ms += ms;
		if (lru.ru_maxrss > link_max_rss) link_max_rss = lru.ru_maxrss;
		struct stat sst;
		long so_mb = stat(so, &sst) == 0 ? (long)(sst.st_size / (1024 * 1024)) : -1;
		int lok = WIFEXITED(lst) && WEXITSTATUS(lst) == 0;
		logf_(p, "link %d: %s, %d objects, %.1f s, peak RSS %ld MB, library %ld MB", q, lok ? "ok" : "FAILED", no,
			ms / 1000.0, lru.ru_maxrss / 1024, so_mb);
		if (lok) for (int i = 0; i < no; i++) unlink(objs[i]); /* objects are not needed once linked */
		free(objs);
		if (!lok) { fail_msg(err, errlen, "%s", sh->errs[5]); goto done; }
	}
	logf_(p, "link total %.1f s, worst peak RSS %ld MB", link_total_ms / 1000.0, link_max_rss / 1024);

	/* manifest last: its presence means the libraries are complete */
	{
		FILE *mf = fopen(path, "w");
		if (!mf) { fail_msg(err, errlen, "cannot write %s", path); goto done; }
		for (int q = 0; q < nparts; q++) fprintf(mf, "libgsgame%d.so\n", q);
		fclose(mf);
	}
	if (p->progress) p->progress(1000);
	ret = nparts;
done:
	if (sh != MAP_FAILED) munmap(sh, shsize);
	free(dirs);
	free(tasks);
	return ret;
}

static void *g_handles[GSR_MAX_PARTS];
static int g_nhandles;

void *gsr_game_sym(const char *name) {
	for (int i = 0; i < g_nhandles; i++) {
		void *s = dlsym(g_handles[i], name);
		if (s) return s;
	}
	return NULL;
}

int gsr_load_game(const char *out_dir, char *err, size_t errlen) {
	g_nhandles = 0;
	char path[800], name[300];
	snprintf(path, sizeof path, "%s/gsgame_libs.txt", out_dir);
	FILE *mf = fopen(path, "r");
	if (!mf) { fail_msg(err, errlen, "no manifest at %s", path); return -1; }
	int n = 0;
	char libs[GSR_MAX_PARTS][1200];
	const char *lib_ptrs[GSR_MAX_PARTS];
	while (fgets(name, sizeof name, mf)) {
		name[strcspn(name, "\r\n")] = 0;
		if (!name[0] || !strcmp(name, "libgsgamebridge.so")) continue;
		if (n >= GSR_MAX_PARTS) break;
		snprintf(libs[n], sizeof libs[n], "%s/%s", out_dir, name);
		void *h = dlopen(libs[n], RTLD_LAZY | RTLD_LOCAL);
		if (!h) {
			fail_msg(err, errlen, "dlopen %s: %s", name, dlerror());
			fclose(mf);
			return -1;
		}
		lib_ptrs[n] = libs[n];
		g_handles[g_nhandles++] = h;
		n++;
	}
	fclose(mf);
	/* The bridge (see gsr_cc_link_bridge) is cheap to make, so it is made here on every load rather than
	 * stored: that also keeps builds from before the engine was attached usable. */
	char bridge[900];
	snprintf(bridge, sizeof bridge, "%s/libgsgamebridge.so", out_dir);
	if (gsr_cc_link_bridge(lib_ptrs, n, bridge, err, errlen) != 0) return -2; /* -2: the build itself is fine */
	void *bh = dlopen(bridge, RTLD_NOW | RTLD_GLOBAL);
	if (!bh) { fail_msg(err, errlen, "dlopen bridge: %s", dlerror()); return -2; }
	return n;
}
