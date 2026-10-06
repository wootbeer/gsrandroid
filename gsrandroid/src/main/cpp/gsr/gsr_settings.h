/* gsr_settings.h -- the player's video / audio / speed / touch options, kept as a small table of integers
 * that is loaded from and saved to <files>/gsr/settings.ini. Thread safe: the UI thread writes, the render
 * and engine threads read. Every write bumps a generation counter so readers can notice changes cheaply. */
#ifndef GSR_SETTINGS_H
#define GSR_SETTINGS_H
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	GSR_S_VIEW_MODE,     /* 0 native 240x160, 1 expanded view 360x240 */
	GSR_S_SCALE_MODE,    /* 0 fit (exact aspect), 1 integer, 2 stretch, 3 zoom to fill (crops) */
	GSR_S_FILTER,        /* 0 sharp pixels, 1 smooth */
	GSR_S_SCREEN,        /* 0 raw, 1 unlit, 2 frontlit, 3 backlit, 4 classic */
	GSR_S_BLEND,         /* flicker reduction: 0 off, 1 light, 2 medium, 3 strong */
	GSR_S_INTERP,        /* 2x frame interpolation (needs a 120 Hz display) */
	GSR_S_ENH_TIMING,
	GSR_S_NATIVE_RENDER, /* enhanced (GPU) renderer */
	GSR_S_NATIVE_SCALE,  /* 1..10 */
	GSR_S_VOLUME,        /* 0..100 */
	GSR_S_MUTE,
	GSR_S_FF_MULT10,     /* fast-forward speed times ten (20 = 2.0x) */
	GSR_S_FF_MODE,       /* 0 hold, 1 toggle */
	GSR_S_FF_UNCAPPED,
	GSR_S_FF_MUTE,       /* mute while fast forwarding */
	GSR_S_SHOW_FPS,
	GSR_S_TOUCH_SCALE,   /* descore touch scale step 0..8 */
	GSR_S_TOUCH_OPACITY, /* percent, 30..100 */
	GSR_S_RENDERER,      /* 0 auto (GPU for the expanded view), 1 CPU, 2 GPU */
	GSR_S_AUTO_CPU,      /* hidden: Auto found the GPU path too slow on this device (remembered; reset by re-picking Auto) */
	/* Everything from here on is a cheat: kept for this play session only (gsr_settings.c neither saves nor loads
	 * them), so new cheats must be added at the end. */
	GSR_S_CHEAT_HP,      /* cheats: infinite HP */
	GSR_S_CHEAT_PP,      /* cheats: infinite PP */
	GSR_S_CHEAT_EXP,     /* cheats: experience multiplier step 0..8 (0x 0.5x 1x 2x 5x 10x 25x 50x 100x), 2 = normal */
	GSR_S_CHEAT_COIN,    /* cheats: coins multiplier step, same steps */
	GSR_S_CHEAT_DROP,    /* cheats: item drop chance step 0..5 (0x .. 10x), 2 = normal */
	GSR_S_COUNT
} GsrSetting;

/* Load the saved file (if any) and apply defaults for the rest. dir is the folder to keep it in. */
void gsr_settings_init(const char *dir);
int gsr_settings_get(GsrSetting id);
/* Clamps to the setting's range, saves the file, bumps the generation. */
void gsr_settings_set(GsrSetting id, int value);
unsigned gsr_settings_generation(void);

#ifdef __cplusplus
}
#endif
#endif
