/* recompiled.h -- the engine's view of the generated game code.
 * The real header (27,000 function declarations) is produced from the player's ROM; the engine only
 * names these three entry points, so this stand-in is all it needs at build time. They are resolved
 * when libgsrengine.so is loaded, after the game libraries built on the device. */
#pragma once
extern "C" {
void gf_irq_handler_03000000(void);
void gf_afunc_03000088(void);
void gf_tfunc_03000658(void);
}
