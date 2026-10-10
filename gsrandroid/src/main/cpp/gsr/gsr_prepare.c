#define _GNU_SOURCE
#include "gsr_prepare.h"
#include "gsr_build.h"
#include "gsr_engine.h"

#include <android/log.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "gsr-prepare", __VA_ARGS__)

/* Peak storage while building is about 550 MB (translated source, shrinking as it compiles, plus the
 * objects); the finished libraries are about 240 MB. Ask for headroom on top. */
#define GSR_NEED_FREE_MB 800

int gsr_run_builder(int argc, char **argv); /* gsr_android.cpp: GSRecomp's gsr_builder, in-process */

static atomic_int g_state = GSR_PREP_IDLE;
static atomic_int g_progress = 0;
static atomic_int g_cached = 0;
static atomic_long g_t0_s = 0;
static atomic_long g_t_end_s = 0;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static char g_msg[160] = "";
static char g_err[400] = "";

static char g_data_dir[512], g_root[512], g_files[512], g_pkg[160];
static char g_builder_data[600], g_built[600], g_tmp[600], g_status[600], g_log[700], g_rom_sha1[64];

static long now_s(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec;
}

static void set_msg(const char *m) {
	pthread_mutex_lock(&g_mu);
	snprintf(g_msg, sizeof g_msg, "%s", m);
	pthread_mutex_unlock(&g_mu);
}

static void set_err(const char *fmt, ...) {
	va_list ap;
	pthread_mutex_lock(&g_mu);
	va_start(ap, fmt);
	vsnprintf(g_err, sizeof g_err, fmt, ap);
	va_end(ap);
	pthread_mutex_unlock(&g_mu);
}

void gsr_prepare_message(char *buf, size_t n) {
	pthread_mutex_lock(&g_mu);
	snprintf(buf, n, "%s", g_msg);
	pthread_mutex_unlock(&g_mu);
}

void gsr_prepare_error(char *buf, size_t n) {
	pthread_mutex_lock(&g_mu);
	snprintf(buf, n, "%s", g_err);
	pthread_mutex_unlock(&g_mu);
}

void gsr_prepare_log_path(char *buf, size_t n) { snprintf(buf, n, "%s", g_log); }
int gsr_prepare_state(void) { return atomic_load(&g_state); }
int gsr_prepare_progress(void) { return atomic_load(&g_progress); }
int gsr_prepare_cached(void) { return atomic_load(&g_cached); }
int gsr_prepare_elapsed_s(void) {
	long t0 = atomic_load(&g_t0_s);
	if (!t0) return 0;
	long end = atomic_load(&g_t_end_s);
	return (int)((end ? end : now_s()) - t0);
}

static void rep(const char *fmt, ...) {
	char line[900];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	LOG("%s", line);
	FILE *f = fopen(g_log, "a");
	if (f) { fprintf(f, "%s\n", line); fclose(f); }
}

static void write_status(const char *s) {
	FILE *f = fopen(g_status, "w");
	if (f) { fputs(s, f); fclose(f); }
}

static void mkdir_p(const char *dir) {
	char tmp[1024];
	snprintf(tmp, sizeof tmp, "%s", dir);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
	}
	mkdir(tmp, 0755);
}

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

/* rm -rf (files are few hundred; recursion depth is tiny) */
static void rm_rf(const char *path) {
	struct stat st;
	if (lstat(path, &st) != 0) return;
	if (S_ISDIR(st.st_mode)) {
		DIR *d = opendir(path);
		if (d) {
			struct dirent *e;
			while ((e = readdir(d))) {
				if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
				char sub[1400];
				snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
				rm_rf(sub);
			}
			closedir(d);
		}
		rmdir(path);
	} else {
		unlink(path);
	}
}

/* data_dir = <files>/gsr/game  ->  files dir, package name, our folders. */
static int derive_dirs(const char *data_dir) {
	const char *files = strstr(data_dir, "/files");
	if (!files) return -1;
	snprintf(g_files, sizeof g_files, "%.*s", (int)(files - data_dir) + 6, data_dir);
	char head[512];
	snprintf(head, sizeof head, "%.*s", (int)(files - data_dir), data_dir);
	const char *pkg = strrchr(head, '/');
	snprintf(g_pkg, sizeof g_pkg, "%s", pkg ? pkg + 1 : head);
	snprintf(g_root, sizeof g_root, "%s/gsr", g_files);
	snprintf(g_builder_data, sizeof g_builder_data, "%s/builder_data", g_root);
	snprintf(g_built, sizeof g_built, "%s/built", g_root);
	snprintf(g_tmp, sizeof g_tmp, "%s/build_tmp", g_root);
	snprintf(g_status, sizeof g_status, "%s/build_status.txt", g_root);
	snprintf(g_log, sizeof g_log, "/sdcard/Android/data/%s/files/gsr_build_log.txt", g_pkg);
	return 0;
}

/* ---- builder output -> progress ------------------------------------------------------------ */

typedef struct { long off; int stage; int said_last; char partial[600]; size_t plen; } Poll;

static void poll_line(Poll *p, const char *line) {
	if (!strncmp(line, "@stage ", 7)) {
		p->stage = strstr(line, "Translating") ? 1 : strstr(line, "Preparing") ? 2 : strstr(line, "Compiling") ? 3 : p->stage;
		set_msg(line + 7);
		if (p->stage == 2 && atomic_load(&g_progress) < 620) atomic_store(&g_progress, 620);
	} else if (!strncmp(line, "@progress ", 10)) {
		int done = 0, total = 0, v = -1;
		if (sscanf(line + 10, "%d %d", &done, &total) == 2 && total > 0) {
			if (p->stage == 1) {
				v = done * 600 / total;
				/* The main program is by far the largest piece and finishes last, so the bar sits near
				 * 59% while it does. Say so, or it looks stuck. */
				if (total >= 10 && done < total && done * 100 >= total * 97 && !p->said_last) {
					p->said_last = 1;
					set_msg("Translating the main game code. This is the longest step and can take a while on slower devices.");
				}
			}
			else if (p->stage == 3) v = 650 + done * 330 / total;
		}
		if (v > atomic_load(&g_progress)) atomic_store(&g_progress, v);
	} else if (!strncmp(line, "@error ", 7)) {
		set_err("%s", line + 7);
	}
}

/* Read what the builder appended since the last call (it prints one line at a time). */
static void poll_builder_output(Poll *p, const char *file) {
	FILE *f = fopen(file, "r");
	if (!f) return;
	fseek(f, p->off, SEEK_SET);
	char buf[2048];
	size_t got;
	while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
		p->off += (long)got;
		for (size_t i = 0; i < got; i++) {
			if (buf[i] == '\n') {
				p->partial[p->plen] = 0;
				poll_line(p, p->partial);
				p->plen = 0;
			} else if (p->plen + 1 < sizeof p->partial) {
				p->partial[p->plen++] = buf[i];
			}
		}
	}
	fclose(f);
}

static int pick_workers(void) {
	int w = 4; /* the setting measured on a Retroid Pocket 6 (QCS8550) */
	char path[700];
	snprintf(path, sizeof path, "%s/workers.txt", g_root);
	FILE *f = fopen(path, "r");
	if (f) { int v = 0; if (fscanf(f, "%d", &v) == 1 && v >= 1 && v <= 16) w = v; fclose(f); }
	return w;
}

static int run_builder(int workers, double *wall_s) {
	char rom[600], jobs[16], out[700], stdout_file[700];
	snprintf(rom, sizeof rom, "%s/golden_sun.gba", g_data_dir);
	snprintf(jobs, sizeof jobs, "%d", workers);
	snprintf(stdout_file, sizeof stdout_file, "%s/builder_stdout.txt", g_root);
	snprintf(out, sizeof out, "%s", g_built);
	char wk[16];
	snprintf(wk, sizeof wk, "%d", workers);
	setenv("GSR_WORKERS", wk, 1);
	remove(stdout_file);
	long t0 = now_s();
	pid_t pid = fork();
	if (pid == 0) {
		int fd = open(stdout_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
		char *argv[] = { "gsr_builder", "--rom", rom, "--data", g_builder_data, "--work", g_tmp, "--out", out,
		                 "--jobs", jobs, NULL };
		int rc = gsr_run_builder(11, argv);
		fflush(NULL);
		_exit(rc);
	}
	if (pid < 0) { set_err("could not start the builder"); return -1; }
	Poll poll; memset(&poll, 0, sizeof poll);
	int st = 0; struct rusage ru;
	for (;;) {
		usleep(250000);
		poll_builder_output(&poll, stdout_file);
		pid_t r = wait4(pid, &st, WNOHANG, &ru);
		if (r == pid) break;
		if (r < 0) { set_err("lost track of the builder"); return -1; }
	}
	poll_builder_output(&poll, stdout_file);
	*wall_s = (double)(now_s() - t0);
	rep("builder: %.0f s, peak RSS %ld MB, workers %d", *wall_s, ru.ru_maxrss / 1024, workers);
	FILE *f = fopen(stdout_file, "r");
	if (f) {
		char line[500]; int shown = 0;
		while (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\r\n")] = 0;
			if (!strncmp(line, "@stage", 6) || !strncmp(line, "@log", 4) || !strncmp(line, "@error", 6) || !strncmp(line, "@done", 5)) {
				if (shown++ < 80) rep("  | %s", line);
			}
		}
		fclose(f);
	}
	int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
	if (!ok) {
		char e[400]; gsr_prepare_error(e, sizeof e);
		if (!e[0]) set_err("the builder stopped unexpectedly (exit %d)", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
	}
	return ok ? 0 : -1;
}

static void expected_stamp(char *buf, size_t n) {
	snprintf(buf, n, "rom_sha1=%s\nbuilder=%s\n", g_rom_sha1, GSR_BUILD_VERSION);
}

static int stamp_matches(void) {
	char want[200], have[200] = "", path[700];
	expected_stamp(want, sizeof want);
	snprintf(path, sizeof path, "%s/build.stamp", g_built);
	FILE *f = fopen(path, "r");
	if (!f) return 0;
	size_t n = fread(have, 1, sizeof have - 1, f);
	have[n] = 0;
	fclose(f);
	snprintf(path, sizeof path, "%s/gsgame_libs.txt", g_built);
	return strcmp(have, want) == 0 && exists(path);
}

static long free_mb(const char *dir) {
	struct statvfs sv;
	if (statvfs(dir, &sv) != 0) return -1;
	return (long)((unsigned long long)sv.f_bavail * sv.f_frsize / (1024 * 1024));
}


/* ---- headless engine test (developer aid) ------------------------------------------------------
 * If files/gsr/HEADLESS_TEST exists (its first number is the frame count, default 600), run the
 * engine without a window on the freshly loaded game, in a child process, and report speed and a
 * screenshot to the log: that is what proves the game code runs under the real engine on this CPU. */
static void run_headless_test(void) {
	char path[800], run_dir[800], rom[800], png[800], outf[800], frames_s[32];
	snprintf(path, sizeof path, "%s/HEADLESS_TEST", g_root);
	FILE *tf = fopen(path, "r");
	if (!tf) return;
	int frames = 600;
	if (fscanf(tf, "%d", &frames) != 1 || frames < 1) frames = 600;
	fclose(tf);
	char err[400];
	if (gsr_engine_bind(err, sizeof err) != 0) { rep("headless test: bind failed: %s", err); return; }
	snprintf(run_dir, sizeof run_dir, "%s/run", g_root);
	mkdir_p(run_dir);
	snprintf(rom, sizeof rom, "%s/golden_sun.gba", g_data_dir);
	{
		char ext[700];
		snprintf(ext, sizeof ext, "/sdcard/Android/data/%s/files", g_pkg);
		snprintf(png, sizeof png, "%s/gsr_headless.png", ext);
	}
	snprintf(outf, sizeof outf, "%s/engine_stdout.txt", run_dir);
	snprintf(frames_s, sizeof frames_s, "%d", frames);
	char replay[800];
	snprintf(replay, sizeof replay, "%s/replay.txt", g_root);
	if (exists(replay)) setenv("GBARECOMP_INPUT_REPLAY", replay, 1);
	rep("headless test: %d frames, rom %s", frames, rom);
	long t0 = now_s();
	pid_t pid = fork();
	if (pid == 0) {
		int fd = open(outf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
		if (chdir(run_dir) != 0) _exit(120);
		char *argv[] = { "gsr", "--rom", rom, "--no-window", "--no-bios", "--frames", frames_s,
		                 "--dump-png", png, NULL };
		char e2[400] = "";
		int rc = gsr_engine_run(9, argv, e2, sizeof e2);
		if (rc == -1 && e2[0]) dprintf(2, "ENGINE-LOAD-ERROR: %s\n", e2);
		fflush(NULL);
		_exit(rc < 0 ? 121 : rc);
	}
	int st = 0; struct rusage ru;
	while (wait4(pid, &st, 0, &ru) < 0) {}
	rep("headless test: finished in %ld s, exit %d (signal %d), peak RSS %ld MB", now_s() - t0,
		WIFEXITED(st) ? WEXITSTATUS(st) : -1, WIFSIGNALED(st) ? WTERMSIG(st) : 0, ru.ru_maxrss / 1024);
	FILE *f = fopen(outf, "r");
	if (f) {
		char line[400]; int shown = 0;
		while (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\r\n")] = 0;
			if (strstr(line, "ENGINE-LOAD-ERROR") || strstr(line, "ppu_render") || strstr(line, "error") ||
			    strstr(line, "wall") || strstr(line, "frames") || strstr(line, "Frame") || strstr(line, "dispatch")) {
				if (shown++ < 40) rep("  engine: %s", line);
			}
		}
		fclose(f);
	}
	if (exists(png)) rep("headless test: screenshot written to %s", png);
}

static void *prepare_thread(void *arg) {
	(void)arg;
	char err[700];
	atomic_store(&g_t0_s, now_s());
	write_status("running");
	rep("== Golden Sun Recompiled: prepare (builder %s) ==", GSR_BUILD_VERSION);

	if (stamp_matches()) {
		rep("cached build found, skipping translate/compile");
		atomic_store(&g_cached, 1);
		set_msg("Loading the game");
		atomic_store(&g_progress, 990);
	} else {
		if (!exists(g_builder_data)) { set_err("builder data missing at %s", g_builder_data); goto fail; }
		mkdir_p(g_root);
		long fm = free_mb(g_root);
		rep("free space: %ld MB (need %d)", fm, GSR_NEED_FREE_MB);
		if (fm >= 0 && fm < GSR_NEED_FREE_MB) {
			set_err("Not enough free storage: %ld MB free, %d MB needed while building (about 250 MB are kept afterwards).", fm, GSR_NEED_FREE_MB);
			goto fail;
		}
		rm_rf(g_built);
		rm_rf(g_tmp);
		mkdir_p(g_built);
		int workers = pick_workers();
		rep("building from the ROM (workers %d)", workers);
		set_msg("Checking your ROM");
		double wall = 0;
		int rc = run_builder(workers, &wall);
		rm_rf(g_tmp);
		if (rc != 0) goto fail;
		char stamp[200], path[700];
		expected_stamp(stamp, sizeof stamp);
		snprintf(path, sizeof path, "%s/build.stamp", g_built);
		FILE *f = fopen(path, "w");
		if (!f) { set_err("cannot write %s", path); goto fail; }
		fputs(stamp, f);
		fclose(f);
		set_msg("Loading the game");
		atomic_store(&g_progress, 990);
	}

	int n = gsr_load_game(g_built, err, sizeof err);
	if (n <= 0) {
		set_err("loading the game failed: %s", err);
		if (n == -1) rm_rf(g_built); /* a bad build is rebuilt next launch; a bridge problem is not the build's fault */
		goto fail;
	}
	void *f1 = gsr_game_sym("gf_irq_handler_03000000");
	void *f2 = gsr_game_sym("kDispatchTable");
	unsigned *vc = (unsigned *)gsr_game_sym("gsr_stamps_kVariantCount");
	if (!f1 || !f2 || !vc || *vc != 256u) {
		set_err("the built game is incomplete (missing symbols)");
		rm_rf(g_built);
		goto fail;
	}
	rep("loaded %d libraries, total %d s%s", n, gsr_prepare_elapsed_s(), atomic_load(&g_cached) ? " (cached)" : "");
	atomic_store(&g_progress, 1000);
	atomic_store(&g_t_end_s, now_s());
	set_msg("Ready");
	write_status("done");
	atomic_store(&g_state, GSR_PREP_READY);
	run_headless_test();
	return NULL;
fail: {
		char e[400]; gsr_prepare_error(e, sizeof e);
		rep("FAILED: %s", e);
		atomic_store(&g_t_end_s, now_s());
		write_status("failed");
		atomic_store(&g_state, GSR_PREP_FAILED);
	}
	return NULL;
}

void gsr_prepare_start(const char *data_dir, const char *rom_sha1) {
	if (atomic_load(&g_state) != GSR_PREP_IDLE) return;
	snprintf(g_data_dir, sizeof g_data_dir, "%s", data_dir ? data_dir : "");
	snprintf(g_rom_sha1, sizeof g_rom_sha1, "%s", rom_sha1 ? rom_sha1 : "");
	if (derive_dirs(g_data_dir) != 0) { LOG("cannot derive dirs from %s", g_data_dir); return; }
	mkdir_p(g_root);
	{ /* external log folder (may fail on exotic setups; the log then only goes to logcat) */
		char d[700];
		snprintf(d, sizeof d, "/sdcard/Android/data/%s/files", g_pkg);
		mkdir_p(d);
	}
	{ /* keep the log across launches (a cached launch must not erase the real build's report); trim when large */
		struct stat ls;
		if (stat(g_log, &ls) == 0 && ls.st_size > 200000) remove(g_log);
	}
	atomic_store(&g_state, GSR_PREP_RUNNING);
	pthread_t th;
	pthread_create(&th, NULL, prepare_thread, NULL);
	pthread_detach(th);
}
