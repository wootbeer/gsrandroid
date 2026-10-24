/* gsr_game.h -- which of the two games this launch plays.
 *
 * The launcher has one unit per game (GsrConfig.java): Golden Sun ("The Broken Seal") keeps its data in
 * <files>/gsr/game, as before The Lost Age was added, and The Lost Age in <files>/gsr/tla. The native side
 * tells them apart by that folder and takes every per-game name from here. */
#ifndef GSR_GAME_H
#define GSR_GAME_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int tla;                  /* 0 Golden Sun (The Broken Seal), 1 The Lost Age */
	const char *title;        /* for the log */
	const char *rom_name;     /* stored ROM in the data folder (GsrConfig) */
	const char *rom_sha1;     /* the only ROM each builder accepts */
	long rom_size;
	const char *builder_data; /* folder under <files>/gsr: the builder's data (from the APK's assets) */
	const char *built;        /* folder under <files>/gsr: the game libraries built on the device */
	const char *run;          /* folder under <files>/gsr: the engine's working folder (options, states) */
	const char *engine_lib;   /* the game layer, dlopen()ed after the game libraries */
	const char *engine_entry; /* its main() under a C name */
} GsrGame;

/* Picks the game from Descore's data folder (call once, before anything else asks). */
void gsr_game_select(const char *data_dir);
const GsrGame *gsr_game(void);

#ifdef __cplusplus
}
#endif
#endif
