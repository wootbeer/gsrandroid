/* gsr_game.h -- which game, and which language of it, this launch plays.
 *
 * The launcher has one unit per game and language (GsrConfig.java). Each keeps its ROM, save and save states
 * in its own folder under <files>/gsr: Golden Sun in game (USA/Europe, as before languages were added) or
 * game_<code>, The Lost Age in tla or tla_<code>, where <code> is es, it, fr or de (upstream's
 * rom_revisions.inc order). The native side tells them apart by that folder and takes every per-game name
 * from here. */
#ifndef GSR_GAME_H
#define GSR_GAME_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int tla;                  /* 0 Golden Sun (The Broken Seal), 1 The Lost Age */
	const char *title;        /* for the log */
	const char *rom_name;     /* stored ROM in the data folder (GsrConfig) */
	const char *rom_sha1;     /* the ROM this game and language needs (upstream's revision list) */
	long rom_size;
	const char *builder_data; /* folder under <files>/gsr: the builder's data (from the APK's assets); the
	                           * builder itself picks the language's sub-folder by the ROM */
	char built[32];           /* folder under <files>/gsr: the game libraries built on the device */
	char run[32];             /* folder under <files>/gsr: the engine's working folder (options, states) */
	const char *engine_lib;   /* the game layer, dlopen()ed after the game libraries */
	const char *engine_entry; /* its main() under a C name */
	const char *lang;         /* "" (USA/Europe), "es", "it", "fr" or "de" */
} GsrGame;

/* Picks the game from Descore's data folder (call once, before anything else asks). */
void gsr_game_select(const char *data_dir);
const GsrGame *gsr_game(void);
/* The data folder name of the other game in the same language ("tla_es" for "game_es", ...). */
void gsr_game_other_dir(char *out, unsigned long n);

#ifdef __cplusplus
}
#endif
#endif
