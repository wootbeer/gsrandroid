/* gsr_controls.h -- gamepad / touch input for the engine, with remappable gamepad buttons.
 *
 * Sources feeding one set of "actions": Android key events (gamepad buttons, d-pad, triggers reported as
 * axes), the left stick and hat, and the on-screen touch overlay (Descore's touch module, drawn only
 * while no gamepad is connected). The engine polls the result as a GBA KEYINPUT word.
 *
 * Remapping only changes which physical button triggers a GBA action while the game is running. Menus
 * (the Android settings screens) never go through this table, so a controller's A and B always confirm
 * and cancel there, whatever they are mapped to in the game. */
#ifndef GSR_CONTROLS_H
#define GSR_CONTROLS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* The first ten values are the GBA KEYINPUT bit numbers. */
typedef enum {
	GSR_ACT_A, GSR_ACT_B, GSR_ACT_SELECT, GSR_ACT_START, GSR_ACT_RIGHT,
	GSR_ACT_LEFT, GSR_ACT_UP, GSR_ACT_DOWN, GSR_ACT_R, GSR_ACT_L,
	GSR_ACT_FF,
	GSR_ACT_MENU,
	GSR_ACT_COUNT
} GsrAction;
#define GSR_BIND_SLOTS 2

void gsr_controls_init(const char *dir);
void gsr_controls_key(int android_key_code, int down);
void gsr_controls_axis(int android_axis_id, float value);

/* Place the touch buttons for a surface of this size using the current Scaling / Opacity settings. */
void gsr_controls_layout(int surface_w, int surface_h);
/* Re-read the touch Scaling / Opacity settings and re-layout. */
void gsr_controls_touch_settings_changed(void);
/* Draw the button captions (call just before descore_swap_buffers, which draws the buttons). */
void gsr_controls_draw_labels(int surface_w, int surface_h);

uint16_t gsr_controls_keyinput(void); /* active low, GBA layout */
int gsr_controls_fast_forward(void);

/* Remapping. keycode 0 = unbound. Binding a key that another action already uses moves it. */
int gsr_controls_bind_get(GsrAction a, int slot);
void gsr_controls_bind_set(GsrAction a, int slot, int android_key_code);
void gsr_controls_bind_reset(void);
/* Forget every held button (the settings menu is about to take the key events). */
void gsr_controls_release_all(void);

#ifdef __cplusplus
}
#endif
#endif
