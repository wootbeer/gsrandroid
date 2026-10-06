/* descore_touch.c -- see descore_touch.h for the full "what lives here vs. what stays per-game"
 * story. */

#include "descore_touch.h"

#ifdef DESCORE_GLES3
#include <GLES3/gl3.h>
#else
#include <GLES/gl.h>
#endif
#include <string.h>
#include <math.h>

typedef struct {
    float x, y, w, h; /* screen pixel space */
    unsigned char keys[DESCORE_TOUCH_MAX_KEYS_PER_BUTTON];
    int num_keys; /* 0 means a discrete tap-toggle button -- see DescoreTouchTapFn */
    bool custom_visibility;
    bool visible;
} DescoreTouchButtonState;

static DescoreTouchButtonState s_buttons[DESCORE_TOUCH_MAX_BUTTONS];
static int s_num_buttons = 0;
static int s_touch_pointer_button[DESCORE_TOUCH_MAX_POINTERS]; /* -1 = not over a button */
static bool s_initialized = false;
static int s_screen_w = 0, s_screen_h = 0;
static bool s_gamepad_connected = false;
static DescoreTouchKeyFn s_key_fn = NULL;
static DescoreTouchTapFn s_tap_fn = NULL;

/* analog sticks + look pad */
typedef struct { float cx, cy, r, vx, vy; DescoreTouchStickFn fn; bool set; int pointer; } StickState;
static StickState s_sticks[DESCORE_TOUCH_MAX_STICKS];
static float s_look_x, s_look_y, s_look_w, s_look_h;
static DescoreTouchLookFn s_look_fn = NULL;
static int s_look_pointer = -1;
static float s_look_last_x, s_look_last_y;
static bool s_sticks_visible = true;

/* A fraction-of-screen cell alone can in principle shrink below
 * a comfortable physical touch target on an unusually small/dense screen combined with a low
 * Scaling setting; this is the floor that stops it. */
#define DESCORE_TOUCH_MIN_CELL_DP 44.0f
static float s_display_density = 1.0f;

const float kDescoreTouchScaleValues[DESCORE_TOUCH_SCALE_NUM_STEPS] = {
    0.90f, 0.95f, 1.00f, 1.10f, 1.20f, 1.35f, 1.50f, 1.65f, 1.80f,
};
static int s_scale_step = DESCORE_TOUCH_SCALE_DEFAULT_STEP;
static float s_opacity = DESCORE_TOUCH_OPACITY_DEFAULT;

void descore_touch_init(int num_buttons, DescoreTouchKeyFn key_fn, DescoreTouchTapFn tap_fn) {
    int i;
    if (num_buttons > DESCORE_TOUCH_MAX_BUTTONS) {
        num_buttons = DESCORE_TOUCH_MAX_BUTTONS;
    }
    s_num_buttons = num_buttons;
    s_key_fn = key_fn;
    s_tap_fn = tap_fn;
    memset(s_buttons, 0, sizeof(s_buttons));
    for (i = 0; i < DESCORE_TOUCH_MAX_POINTERS; ++i) {
        s_touch_pointer_button[i] = -1;
    }
    s_initialized = true;
}

void descore_touch_set_stick(int index, float cx, float cy, float radius, DescoreTouchStickFn fn) {
    if (index < 0 || index >= DESCORE_TOUCH_MAX_STICKS) return;
    s_sticks[index].cx = cx; s_sticks[index].cy = cy; s_sticks[index].r = radius; s_sticks[index].fn = fn;
    s_sticks[index].set = true; s_sticks[index].vx = s_sticks[index].vy = 0; s_sticks[index].pointer = -1;
}

void descore_touch_set_lookpad(float x, float y, float w, float h, DescoreTouchLookFn fn) {
    s_look_x = x; s_look_y = y; s_look_w = w; s_look_h = h; s_look_fn = fn; s_look_pointer = -1;
}

static bool sticks_or_pad_active(void) {
    int i;
    if (!s_sticks_visible) return false;
    for (i = 0; i < DESCORE_TOUCH_MAX_STICKS; ++i) if (s_sticks[i].set) return true;
    return s_look_fn != NULL;
}

static void stick_update(int i, float x, float y) {
    StickState *st = &s_sticks[i];
    float dx = (x - st->cx) / st->r, dy = (y - st->cy) / st->r, len = sqrtf(dx * dx + dy * dy);
    if (len > 1.0f) { dx /= len; dy /= len; }
    st->vx = dx; st->vy = dy;
    if (st->fn) st->fn(i, dx, dy);
}

static void stick_release(int i) {
    StickState *st = &s_sticks[i];
    st->pointer = -1; st->vx = st->vy = 0;
    if (st->fn) st->fn(i, 0.0f, 0.0f);
}

/* returns true if the touch was taken by a stick or the look pad */
static bool sticks_down(int pointer_id, float x, float y) {
    int i;
    if (!s_sticks_visible) return false;
    for (i = 0; i < DESCORE_TOUCH_MAX_STICKS; ++i) {
        StickState *st = &s_sticks[i];
        float dx, dy;
        if (!st->set || st->pointer != -1) continue;
        dx = x - st->cx; dy = y - st->cy;
        if (dx * dx + dy * dy <= st->r * st->r * 1.69f) { st->pointer = pointer_id; stick_update(i, x, y); return true; }
    }
    if (s_look_fn && s_look_pointer == -1 && x >= s_look_x && x < s_look_x + s_look_w && y >= s_look_y && y < s_look_y + s_look_h) {
        s_look_pointer = pointer_id; s_look_last_x = x; s_look_last_y = y; return true;
    }
    return false;
}

static bool sticks_move(int pointer_id, float x, float y) {
    int i;
    for (i = 0; i < DESCORE_TOUCH_MAX_STICKS; ++i)
        if (s_sticks[i].set && s_sticks[i].pointer == pointer_id) { stick_update(i, x, y); return true; }
    if (s_look_pointer == pointer_id) {
        if (s_look_fn) s_look_fn(x - s_look_last_x, y - s_look_last_y);
        s_look_last_x = x; s_look_last_y = y; return true;
    }
    return false;
}

static bool sticks_up(int pointer_id) {
    int i;
    for (i = 0; i < DESCORE_TOUCH_MAX_STICKS; ++i)
        if (s_sticks[i].set && s_sticks[i].pointer == pointer_id) { stick_release(i); return true; }
    if (s_look_pointer == pointer_id) { s_look_pointer = -1; return true; }
    return false;
}

void descore_touch_set_screen_size(int width, int height) {
    s_screen_w = width;
    s_screen_h = height;
}

void descore_touch_set_display_density(float density) {
    if (density > 0.0f) {
        s_display_density = density;
    }
}

float descore_touch_min_cell_px(void) {
    return DESCORE_TOUCH_MIN_CELL_DP * s_display_density;
}

void descore_touch_set_button(int index, float x, float y, float w, float h,
                               const unsigned char *keys, int num_keys, bool custom_visibility) {
    DescoreTouchButtonState *b;
    int i;
    if (index < 0 || index >= s_num_buttons) {
        return;
    }
    b = &s_buttons[index];
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    if (num_keys > DESCORE_TOUCH_MAX_KEYS_PER_BUTTON) {
        num_keys = DESCORE_TOUCH_MAX_KEYS_PER_BUTTON;
    }
    b->num_keys = num_keys;
    for (i = 0; i < num_keys; ++i) {
        b->keys[i] = keys[i];
    }
    b->custom_visibility = custom_visibility;
    if (!custom_visibility) {
        b->visible = !s_gamepad_connected;
    }
    /* A custom_visibility button keeps whatever descore_touch_set_button_visible() last set (or
     * the zeroed/false default from descore_touch_init()'s memset, before the game has called
     * that even once) -- this function only ever touches geometry/keys for it, never `visible`,
     * so a game re-laying-out button positions (a live Scaling-slider adjustment, e.g.) never
     * accidentally clobbers a custom button's own visibility state. */
}

void descore_touch_set_button_visible(int index, bool visible) {
    if (index < 0 || index >= s_num_buttons) {
        return;
    }
    s_buttons[index].visible = visible;
}

bool descore_touch_gamepad_connected(void) {
    return s_gamepad_connected;
}

static void release_button(int idx) {
    int i;
    if (s_buttons[idx].num_keys == 0 || s_key_fn == NULL) {
        return;
    }
    for (i = 0; i < s_buttons[idx].num_keys; ++i) {
        s_key_fn(s_buttons[idx].keys[i], 0);
    }
}

static void press_button(int idx) {
    int i;
    if (s_buttons[idx].num_keys == 0 || s_key_fn == NULL) {
        return;
    }
    for (i = 0; i < s_buttons[idx].num_keys; ++i) {
        s_key_fn(s_buttons[idx].keys[i], 1);
    }
}

/* Same "release anything held" transition-guard every descore-shell port's own
 * setGamepadConnected()-equivalent already established -- a finger left resting on screen when a
 * gamepad takes over shouldn't leave a direction/action stuck held. */
void descore_touch_set_gamepad_connected(bool connected) {
    int i;
    if (connected && !s_gamepad_connected) {
        for (i = 0; i < DESCORE_TOUCH_MAX_POINTERS; ++i) {
            if (s_touch_pointer_button[i] != -1) {
                release_button(s_touch_pointer_button[i]);
                s_touch_pointer_button[i] = -1;
            }
        }
    }
    if (connected) {
        for (i = 0; i < DESCORE_TOUCH_MAX_STICKS; ++i) if (s_sticks[i].set && s_sticks[i].pointer != -1) stick_release(i);
        s_look_pointer = -1;
    }
    s_sticks_visible = !connected;
    s_gamepad_connected = connected;
    for (i = 0; i < s_num_buttons; ++i) {
        if (!s_buttons[i].custom_visibility) {
            s_buttons[i].visible = !connected;
        }
    }
}

static bool point_in_button(float x, float y, const DescoreTouchButtonState *b) {
    return x >= b->x && y >= b->y && x <= b->x + b->w && y <= b->y + b->h;
}

static int button_at(float x, float y) {
    int i;
    for (i = 0; i < s_num_buttons; ++i) {
        if (!s_buttons[i].visible) {
            continue;
        }
        if (point_in_button(x, y, &s_buttons[i])) {
            return i;
        }
    }
    return -1;
}

/* True if `key` appears in both buttons' key lists -- so a drag from one button straight into a
 * neighbor that shares a key (e.g. a D-pad UP cell dragging into an UP-RIGHT diagonal cell) keeps
 * that key continuously held, only pressing the newly-entered button's OTHER key(s) fresh, rather
 * than a spurious one-frame release-then-press stutter. */
static bool key_in_button(unsigned char key, int idx) {
    int i;
    for (i = 0; i < s_buttons[idx].num_keys; ++i) {
        if (s_buttons[idx].keys[i] == key) {
            return true;
        }
    }
    return false;
}

static void handle_down(int pointer_id, float x, float y) {
    int idx;
    if (pointer_id < 0 || pointer_id >= DESCORE_TOUCH_MAX_POINTERS) {
        return;
    }
    idx = button_at(x, y);
    s_touch_pointer_button[pointer_id] = idx;
    if (idx == -1) {
        return;
    }
    if (s_buttons[idx].num_keys == 0) {
        /* A single discrete tap-to-toggle, not a held button -- see DescoreTouchTapFn's own
         * comment. handle_up()/handle_move() need no matching special case: releasing or dragging
         * off a 0-key button already does nothing through the ordinary press/release path. */
        if (s_tap_fn != NULL) {
            s_tap_fn(idx);
        }
    } else {
        press_button(idx);
    }
}

static void handle_up(int pointer_id) {
    int idx;
    if (pointer_id < 0 || pointer_id >= DESCORE_TOUCH_MAX_POINTERS) {
        return;
    }
    idx = s_touch_pointer_button[pointer_id];
    if (idx != -1) {
        release_button(idx);
    }
    s_touch_pointer_button[pointer_id] = -1;
}

static void handle_move(int pointer_id, float x, float y) {
    int prev_idx, cur_idx, i;
    if (pointer_id < 0 || pointer_id >= DESCORE_TOUCH_MAX_POINTERS) {
        return;
    }
    prev_idx = s_touch_pointer_button[pointer_id];
    cur_idx = button_at(x, y);
    if (cur_idx == prev_idx) {
        return; /* still over the same button (or still over nothing) -- nothing changed */
    }

    if (prev_idx != -1 && s_key_fn != NULL) {
        for (i = 0; i < s_buttons[prev_idx].num_keys; ++i) {
            unsigned char k = s_buttons[prev_idx].keys[i];
            if (cur_idx == -1 || !key_in_button(k, cur_idx)) {
                s_key_fn(k, 0);
            }
        }
    }
    if (cur_idx != -1 && s_key_fn != NULL) {
        for (i = 0; i < s_buttons[cur_idx].num_keys; ++i) {
            unsigned char k = s_buttons[cur_idx].keys[i];
            if (prev_idx == -1 || !key_in_button(k, prev_idx)) {
                s_key_fn(k, 1);
            }
        }
    }
    s_touch_pointer_button[pointer_id] = cur_idx;
}

bool descore_touch_handle(int action, int pointer_id, float x, float y) {
    bool any_visible;
    int i;

    if (!s_initialized) {
        return false;
    }

    any_visible = sticks_or_pad_active();
    for (i = 0; i < s_num_buttons; ++i) {
        if (s_buttons[i].visible) {
            any_visible = true;
            break;
        }
    }
    if (!any_visible) {
        return false;
    }

    switch (action) {
        case DESCORE_TOUCH_ACTION_DOWN:
        case DESCORE_TOUCH_ACTION_POINTER_DOWN:
            if (button_at(x, y) == -1 && sticks_down(pointer_id, x, y)) return true;
            handle_down(pointer_id, x, y);
            return true;
        case DESCORE_TOUCH_ACTION_UP:
        case DESCORE_TOUCH_ACTION_POINTER_UP:
            if (sticks_up(pointer_id)) return true;
            handle_up(pointer_id);
            return true;
        case DESCORE_TOUCH_ACTION_MOVE:
            if (sticks_move(pointer_id, x, y)) return true;
            handle_move(pointer_id, x, y);
            return true;
        default:
            return false;
    }
}

void descore_touch_set_scale_step(int step) {
    if (step < 0) {
        step = 0;
    } else if (step >= DESCORE_TOUCH_SCALE_NUM_STEPS) {
        step = DESCORE_TOUCH_SCALE_NUM_STEPS - 1;
    }
    s_scale_step = step;
}

int descore_touch_scale_step(void) {
    return s_scale_step;
}

float descore_touch_scale_value(int step) {
    if (step < 0) {
        step = 0;
    } else if (step >= DESCORE_TOUCH_SCALE_NUM_STEPS) {
        step = DESCORE_TOUCH_SCALE_NUM_STEPS - 1;
    }
    return kDescoreTouchScaleValues[step];
}

void descore_touch_set_opacity(float alpha) {
    if (alpha < DESCORE_TOUCH_OPACITY_DEFAULT) {
        alpha = DESCORE_TOUCH_OPACITY_DEFAULT; /* defensive floor -- the primary clamp lives in
                                                 * each game's own Touch Options setter */
    } else if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    s_opacity = alpha;
}

float descore_touch_opacity(void) {
    return s_opacity;
}

/* Drawn as a separate screen-space GL overlay on top of whatever the game's own frame already
 * drew -- simple flat translucent gray quads, no textures. */
void descore_touch_draw(void) {
    int i;
    bool any_visible;

    if (!s_initialized) {
        return;
    }

    any_visible = sticks_or_pad_active();
    for (i = 0; i < s_num_buttons; ++i) {
        if (s_buttons[i].visible) {
            any_visible = true;
            break;
        }
    }
    if (!any_visible) {
        return;
    }

#ifdef DESCORE_GLES3
    /* GLES 3 path: one tiny flat-colour program, screen-pixel coordinates, no fixed function.
     * Program/VAO/buffer are created lazily and recreated if the context was rebuilt (the program
     * name then fails glIsProgram). All state touched here is put back to "off" states the 3D
     * renderer sets itself each frame; the renderer must invalidate its state cache after
     * descore_swap_buffers(). */
    {
        static GLuint prog, vbo;
        static GLint u_screen, u_color;
        static const char *vs = "#version 300 es\nlayout(location=0) in vec2 p; uniform vec2 screen;\n"
            "void main(){ gl_Position = vec4(p.x/screen.x*2.0-1.0, 1.0-p.y/screen.y*2.0, 0.0, 1.0); }\n";
        static const char *fs = "#version 300 es\nprecision mediump float; uniform vec4 color; out vec4 o;\n"
            "void main(){ o = color; }\n";
        if (prog == 0 || !glIsProgram(prog)) {
            GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
            glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
            prog = glCreateProgram(); glAttachShader(prog, v); glAttachShader(prog, f);
            glLinkProgram(prog); glDeleteShader(v); glDeleteShader(f);
            u_screen = glGetUniformLocation(prog, "screen");
            u_color = glGetUniformLocation(prog, "color");
            glGenBuffers(1, &vbo);
        }
        glBindVertexArray(0);
        glUseProgram(prog);
        glUniform2f(u_screen, (float) s_screen_w, (float) s_screen_h);
        glUniform4f(u_color, 0.7f, 0.7f, 0.7f, s_opacity);
        glViewport(0, 0, s_screen_w, s_screen_h);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        for (i = 0; i < s_num_buttons; ++i) {
            const DescoreTouchButtonState *b;
            GLfloat vertices[8];
            if (!s_buttons[i].visible) continue;
            b = &s_buttons[i];
            vertices[0] = b->x;         vertices[1] = b->y;
            vertices[2] = b->x + b->w;  vertices[3] = b->y;
            vertices[4] = b->x;         vertices[5] = b->y + b->h;
            vertices[6] = b->x + b->w;  vertices[7] = b->y + b->h;
            glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STREAM_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void *) 0);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        if (s_sticks_visible) {
            int k;
            for (k = 0; k < DESCORE_TOUCH_MAX_STICKS; ++k) {
                const StickState *st = &s_sticks[k];
                int seg, ring;
                if (!st->set) continue;
                /* base disc (faint) then thumb disc (stronger), as triangle fans */
                for (ring = 0; ring < 2; ++ring) {
                    GLfloat fan[(34) * 2];
                    float cx = ring ? st->cx + st->vx * st->r * 0.7f : st->cx;
                    float cy = ring ? st->cy + st->vy * st->r * 0.7f : st->cy;
                    float r = ring ? st->r * 0.38f : st->r;
                    fan[0] = cx; fan[1] = cy;
                    for (seg = 0; seg <= 32; ++seg) {
                        float a = seg * 6.2831853f / 32.0f;
                        fan[2 + seg * 2] = cx + cosf(a) * r; fan[3 + seg * 2] = cy + sinf(a) * r;
                    }
                    glUniform4f(u_color, 0.7f, 0.7f, 0.7f, ring ? s_opacity : s_opacity * 0.5f);
                    glBufferData(GL_ARRAY_BUFFER, sizeof fan, fan, GL_STREAM_DRAW);
                    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void *) 0);
                    glDrawArrays(GL_TRIANGLE_FAN, 0, 34);
                }
            }
        }
        glDisableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDisable(GL_BLEND);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(0);
    }
#else
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    /* Orthographic projection in real screen pixels, origin top-left (matching Android's own
     * touch-coordinate convention, and the button rects above), y flipped so pixel row 0 is at the
     * top of the screen the way glOrtho's default bottom-left-origin otherwise wouldn't be. */
    glOrthof(0.0f, (float) s_screen_w, (float) s_screen_h, 0.0f, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableClientState(GL_VERTEX_ARRAY);
    glColor4f(0.7f, 0.7f, 0.7f, s_opacity); /* translucent light gray -- visible over any game
                                              * art, not opaque. Every button draws through this
                                              * same glColor4f() call, so all of them move
                                              * together when the Opacity slider changes. */

    for (i = 0; i < s_num_buttons; ++i) {
        const DescoreTouchButtonState *b;
        GLfloat vertices[8];
        if (!s_buttons[i].visible) {
            continue;
        }
        b = &s_buttons[i];
        vertices[0] = b->x;         vertices[1] = b->y;
        vertices[2] = b->x + b->w;  vertices[3] = b->y;
        vertices[4] = b->x;         vertices[5] = b->y + b->h;
        vertices[6] = b->x + b->w;  vertices[7] = b->y + b->h;
        glVertexPointer(2, GL_FLOAT, 0, vertices);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f); /* restore the default color state the game's own present
                                        * call expects */
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);

    /* Restore GL_PROJECTION/GL_MODELVIEW back to identity before returning. This function switches
     * to its own screen-pixel orthographic projection above (glOrthof) to draw the touch buttons;
     * if it were left in place, a game whose own present call does not reset those matrices would
     * draw its next frame under the wrong projection (a black screen). Kept unconditional: it is
     * cheap, and harmless for a present call that resets them itself. */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
#endif /* DESCORE_GLES3 */
}
