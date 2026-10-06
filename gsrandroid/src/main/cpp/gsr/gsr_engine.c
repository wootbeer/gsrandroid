#define _GNU_SOURCE
#include "gsr_engine.h"
#include "gsr_build.h"
#include "gsr_host.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* defined in runtime_arm.cpp under GSR_ANDROID_LATEBIND */
extern const void *gsr_late_dispatch_table;
extern unsigned gsr_late_dispatch_len;

int gsr_engine_bind(char *err, size_t errlen) {
	const void *tab = gsr_game_sym("kDispatchTable");
	const unsigned *len = (const unsigned *)gsr_game_sym("kDispatchTableLen");
	if (!tab || !len) {
		snprintf(err, errlen, "game libraries lack kDispatchTable / kDispatchTableLen");
		return -1;
	}
	gsr_late_dispatch_table = tab;
	gsr_late_dispatch_len = *len;
	return 0;
}

/* run_game() has a very large stack frame (the PC build runs it on a main thread with a big stack), so the
 * engine always runs on a thread of its own with a generous one. The memory is only reserved address space. */
#define GSR_ENGINE_STACK_BYTES (128u * 1024 * 1024)

typedef struct { int (*fn)(int, char **); int argc; char **argv; int rc; } EngineCall;

static void *engine_thread(void *p) {
	EngineCall *c = (EngineCall *)p;
	c->rc = c->fn(c->argc, c->argv);
	return NULL;
}

int gsr_engine_run(int argc, char **argv, char *err, size_t errlen) {
	void *h = dlopen("libgsrengine.so", RTLD_NOW | RTLD_GLOBAL);
	if (!h) {
		snprintf(err, errlen, "dlopen libgsrengine.so: %s", dlerror());
		return -1;
	}
	int (*fn)(int, char **) = (int (*)(int, char **))dlsym(h, "gsr_engine_main");
	if (!fn) {
		snprintf(err, errlen, "gsr_engine_main missing in libgsrengine.so");
		return -1;
	}
	EngineCall call = { fn, argc, argv, 0 };
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, GSR_ENGINE_STACK_BYTES);
	pthread_t th;
	if (pthread_create(&th, &attr, engine_thread, &call) != 0) {
		snprintf(err, errlen, "cannot create the engine thread (%u MB stack)", GSR_ENGINE_STACK_BYTES >> 20);
		return -1;
	}
	pthread_join(th, NULL);
	return call.rc;
}

/* ---- in-process, non-blocking start (the Android shell presents the engine's frames itself) ------------- */

static EngineCall g_async_call;
static char *g_async_args[32];
static volatile int g_async_done;

static void *engine_thread_async(void *p) {
	EngineCall *c = (EngineCall *)p;
	gsr_host_engine_state(1);
	c->rc = c->fn(c->argc, c->argv);
	g_async_done = 1;
	gsr_host_engine_state(0);
	return NULL;
}

int gsr_engine_start(int argc, char **argv, char *err, size_t errlen) {
	void *h = dlopen("libgsrengine.so", RTLD_NOW | RTLD_GLOBAL);
	if (!h) {
		snprintf(err, errlen, "dlopen libgsrengine.so: %s", dlerror());
		return -1;
	}
	int (*fn)(int, char **) = (int (*)(int, char **))dlsym(h, "gsr_engine_main");
	if (!fn) {
		snprintf(err, errlen, "gsr_engine_main missing in libgsrengine.so");
		return -1;
	}
	if (argc > 30) argc = 30;
	for (int i = 0; i < argc; i++) g_async_args[i] = strdup(argv[i]);
	g_async_args[argc] = NULL;
	g_async_call.fn = fn; g_async_call.argc = argc; g_async_call.argv = g_async_args; g_async_call.rc = 0;
	g_async_done = 0;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, GSR_ENGINE_STACK_BYTES);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_t th;
	if (pthread_create(&th, &attr, engine_thread_async, &g_async_call) != 0) {
		snprintf(err, errlen, "cannot create the engine thread (%u MB stack)", GSR_ENGINE_STACK_BYTES >> 20);
		return -1;
	}
	return 0;
}

int gsr_engine_finished(int *rc) {
	if (!g_async_done) return 0;
	if (rc) *rc = g_async_call.rc;
	return 1;
}
