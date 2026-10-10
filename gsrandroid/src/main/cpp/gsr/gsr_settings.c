#include "gsr_settings.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name; int def, lo, hi; } Spec;

static const Spec k_spec[GSR_S_COUNT] = {
	[GSR_S_VIEW_MODE]     = { "view_mode", 0, 0, 1 },
	[GSR_S_SCALE_MODE]    = { "scale_mode", 0, 0, 3 },
	[GSR_S_FILTER]        = { "filter", 0, 0, 1 },
	[GSR_S_SCREEN]        = { "screen", 0, 0, 10 },
	[GSR_S_BLEND]         = { "blend", 3, 0, 3 },
	[GSR_S_INTERP]        = { "interpolation", 0, 0, 1 },
	[GSR_S_ENH_TIMING]    = { "enhanced_timing", 0, 0, 1 },
	[GSR_S_NATIVE_RENDER] = { "native_render", 0, 0, 1 },
	[GSR_S_NATIVE_SCALE]  = { "native_scale", 2, 1, 10 },
	[GSR_S_VOLUME]        = { "volume", 100, 0, 100 },
	[GSR_S_MUTE]          = { "mute", 0, 0, 1 },
	[GSR_S_FF_MULT10]     = { "ff_speed", 40, 15, 160 },
	[GSR_S_FF_MODE]       = { "ff_mode", 0, 0, 1 },
	[GSR_S_FF_UNCAPPED]   = { "ff_uncapped", 0, 0, 1 },
	[GSR_S_FF_MUTE]       = { "ff_mute", 0, 0, 1 },
	[GSR_S_SHOW_FPS]      = { "show_fps", 0, 0, 1 },
	[GSR_S_TOUCH_SCALE]   = { "touch_scale", 2, 0, 8 },
	[GSR_S_TOUCH_OPACITY] = { "touch_opacity", 30, 30, 100 },
	[GSR_S_RENDERER]      = { "renderer", 0, 0, 2 },
	[GSR_S_AUTO_CPU]      = { "auto_cpu", 0, 0, 1 },
	[GSR_S_TOUCH_DPAD]    = { "touch_dpad", 0, 0, 1 },
	[GSR_S_SCREEN_FILTER] = { "screen_filter", 0, 0, 4 },
	[GSR_S_CHEAT_HP]      = { "cheat_hp", 0, 0, 1 },
	[GSR_S_CHEAT_PP]      = { "cheat_pp", 0, 0, 1 },
	[GSR_S_CHEAT_EXP]     = { "cheat_exp", 2, 0, 8 },
	[GSR_S_CHEAT_COIN]    = { "cheat_coin", 2, 0, 8 },
	[GSR_S_CHEAT_DROP]    = { "cheat_drop", 2, 0, 5 },
};

static atomic_int g_val[GSR_S_COUNT];
static atomic_uint g_gen;
static char g_path[1024];
static pthread_mutex_t g_save_mu = PTHREAD_MUTEX_INITIALIZER;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void save(void) {
	char tmp[1100];
	FILE *f;
	if (!g_path[0]) return;
	pthread_mutex_lock(&g_save_mu);
	snprintf(tmp, sizeof tmp, "%s.tmp", g_path);
	f = fopen(tmp, "w");
	if (f) {
		/* The cheats (the last entries) are for this play session only: never written, so they start off next time. */
		for (int i = 0; i < GSR_S_CHEAT_HP; i++) fprintf(f, "%s=%d\n", k_spec[i].name, atomic_load(&g_val[i]));
		fclose(f);
		rename(tmp, g_path);
	}
	pthread_mutex_unlock(&g_save_mu);
}

void gsr_settings_init(const char *dir) {
	char line[128];
	FILE *f;
	for (int i = 0; i < GSR_S_COUNT; i++) atomic_store(&g_val[i], k_spec[i].def);
	snprintf(g_path, sizeof g_path, "%s/settings.ini", dir);
	f = fopen(g_path, "r");
	if (f) {
		while (fgets(line, sizeof line, f)) {
			char *eq = strchr(line, '=');
			if (!eq) continue;
			*eq = 0;
			for (int i = 0; i < GSR_S_CHEAT_HP; i++)  /* cheats are not loaded: see save() */
				if (strcmp(line, k_spec[i].name) == 0)
					atomic_store(&g_val[i], clampi(atoi(eq + 1), k_spec[i].lo, k_spec[i].hi));
		}
		fclose(f);
	}
	atomic_fetch_add(&g_gen, 1);
}

int gsr_settings_get(GsrSetting id) {
	return (id >= 0 && id < GSR_S_COUNT) ? atomic_load(&g_val[id]) : 0;
}

void gsr_settings_set(GsrSetting id, int value) {
	if (id < 0 || id >= GSR_S_COUNT) return;
	value = clampi(value, k_spec[id].lo, k_spec[id].hi);
	if (atomic_exchange(&g_val[id], value) == value) return;
	atomic_fetch_add(&g_gen, 1);
	save();
}

unsigned gsr_settings_generation(void) { return atomic_load(&g_gen); }
