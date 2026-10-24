#include "gsr_game.h"

#include <string.h>

static const GsrGame k_games[2] = {
	{ 0, "Golden Sun", "golden_sun.gba", "5c4695205413df7db52b9a184815a07783999971", 8388608L,
	  "builder_data", "built", "run", "libgsrengine.so", "gsr_engine_main" },
	{ 1, "Golden Sun: The Lost Age", "lost_age.gba", "b500663220cb9bf56b9f8e8c0c544f1d6fa3a824", 16777216L,
	  "builder_data_tla", "built_tla", "run_tla", "libgsrtlaengine.so", "gsr_tla_engine_main" },
};
static int g_which;

void gsr_game_select(const char *data_dir) {
	size_t n = data_dir ? strlen(data_dir) : 0;
	while (n > 0 && data_dir[n - 1] == '/') n--;
	g_which = n >= 4 && !strncmp(data_dir + n - 4, "/tla", 4);
}

const GsrGame *gsr_game(void) { return &k_games[g_which]; }
