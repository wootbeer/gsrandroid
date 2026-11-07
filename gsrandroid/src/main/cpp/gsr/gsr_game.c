#include "gsr_game.h"

#include <stdio.h>
#include <string.h>

/* Upstream's revision lists, same order (tbs/src/rom_revisions.inc, tla/src/tla_rom_revisions.inc). */
static const char *const k_langs[5] = { "", "es", "it", "fr", "de" };
static const char *const k_tbs_sha1[5] = {
	"5c4695205413df7db52b9a184815a07783999971", "de0bdc889594b012cad213ca83707feb81ada28b",
	"ca45e04b26b2f040ac63e679a459d21869f5f28d", "42f3b262c16cfc5bde1fa2b25016fb74046de1b3",
	"c23ab91a39434623cf1a999f387d743ade1a22a6" };
static const char *const k_tla_sha1[5] = {
	"b500663220cb9bf56b9f8e8c0c544f1d6fa3a824", "a2e3e80036195343c2713410ac14f4e329e3f40d",
	"57f1d005666b81645e55412658427e27099c5907", "f14855c0f8c87a95cc52189e8e3b2bd9df186322",
	"7820b4c7ba9002b0e9be23a6f45a9ab204986438" };

static GsrGame g_game;
static int g_lang;

void gsr_game_select(const char *data_dir) {
	const char *base;
	size_t n = data_dir ? strlen(data_dir) : 0;
	char last[64] = "";
	while (n > 0 && data_dir[n - 1] == '/') n--;
	base = data_dir;
	for (size_t i = 0; i < n; i++) if (data_dir[i] == '/') base = data_dir + i + 1;
	snprintf(last, sizeof last, "%.*s", (int)(n - (size_t)(base - data_dir)), base);
	g_lang = 0;
	for (int i = 1; i < 5; i++) {
		size_t l = strlen(last), c = strlen(k_langs[i]);
		if (l > c + 1 && last[l - c - 1] == '_' && !strcmp(last + l - c, k_langs[i])) g_lang = i;
	}
	memset(&g_game, 0, sizeof g_game);
	g_game.tla = !strncmp(last, "tla", 3);
	g_game.lang = k_langs[g_lang];
	if (g_game.tla) {
		g_game.title = "Golden Sun: The Lost Age";
		g_game.rom_name = "lost_age.gba";
		g_game.rom_sha1 = k_tla_sha1[g_lang];
		g_game.rom_size = 16777216L;
		g_game.builder_data = "builder_data_tla";
		g_game.engine_lib = "libgsrtlaengine.so";
		g_game.engine_entry = "gsr_tla_engine_main";
		snprintf(g_game.built, sizeof g_game.built, "built_tla%s%s", g_lang ? "_" : "", k_langs[g_lang]);
		snprintf(g_game.run, sizeof g_game.run, "run_tla%s%s", g_lang ? "_" : "", k_langs[g_lang]);
	} else {
		g_game.title = "Golden Sun";
		g_game.rom_name = "golden_sun.gba";
		g_game.rom_sha1 = k_tbs_sha1[g_lang];
		g_game.rom_size = 8388608L;
		g_game.builder_data = "builder_data";
		g_game.engine_lib = "libgsrengine.so";
		g_game.engine_entry = "gsr_engine_main";
		snprintf(g_game.built, sizeof g_game.built, "built%s%s", g_lang ? "_" : "", k_langs[g_lang]);
		snprintf(g_game.run, sizeof g_game.run, "run%s%s", g_lang ? "_" : "", k_langs[g_lang]);
	}
}

const GsrGame *gsr_game(void) { return &g_game; }

void gsr_game_other_dir(char *out, unsigned long n) {
	snprintf(out, n, "%s%s%s", g_game.tla ? "game" : "tla", g_lang ? "_" : "", k_langs[g_lang]);
}
