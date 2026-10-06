// gsr_host.cpp -- frame mailbox + GLES3 presenter, audio bridge into Descore's mixer, input poll.
#include "gsr_host.h"
#include "gsr_controls.h"
#include "gsr_settings.h"
#include "gsr_text.h"
#include "descore.h"

#define RECOMP_AUDIO_DRC_IMPL
#include "recomp_audio_drc.h"

#include <GLES3/gl3.h>
#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

// Engine core (libgsrhost.so): the infinite HP / PP switches used by the Cheats page.
extern "C" void runtime_set_infinite_hp(int on);
extern "C" void runtime_set_infinite_pp(int on);

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "gsr-host", __VA_ARGS__)

namespace {

// ---- frame mailbox ------------------------------------------------------------------------------------
std::mutex g_fmu;
std::condition_variable g_fcv;
std::vector<uint8_t> g_fbuf;          // the frame currently on screen
int g_fw = 0, g_fh = 0;
std::deque<std::vector<uint8_t>> g_queue;  // frames waiting to be shown (depth 1, or 3 while interpolating)
int g_qw = 0, g_qh = 0;
bool g_upload = false;
std::atomic<int> g_interp_on{0};
std::atomic<int> g_debug{0};  // diagnostics on (debuggable builds only)
std::atomic<long long> g_stat_idle_ns{0};
std::atomic<int> g_gpu_status{0}, g_gpu_active{0};
std::atomic<int> g_stat_submitted{0}, g_stat_dropped{0}, g_stat_repeated{0};  // per 2 s window
std::atomic<float> g_display_hz{60.0f};
std::atomic<int> g_running{0};
std::atomic<int> g_quit{0};
std::atomic<int> g_frames{0};
std::atomic<int> g_menu_open{0};

// ---- GL ------------------------------------------------------------------------------------------------
GLuint g_prog = 0, g_tex = 0;
GLint g_u_rect = -1, g_u_uv = -1;
int g_tex_w = 0, g_tex_h = 0;
int g_tex_filter = -1;

void build_gl() {
    static const char* vs =
        "#version 300 es\n"
        "uniform vec4 rect; uniform vec4 uvr; out vec2 uv;\n"
        "void main(){ vec2 c = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
        "  gl_Position = vec4(mix(rect.xy, rect.zw, c), 0.0, 1.0);\n"
        "  uv = vec2(mix(uvr.x, uvr.z, c.x), mix(uvr.w, uvr.y, c.y)); }\n";
    static const char* fs =
        "#version 300 es\nprecision mediump float; in vec2 uv; uniform sampler2D tex; out vec4 o;\n"
        "void main(){ o = vec4(texture(tex, uv).rgb, 1.0); }\n";
    GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v, 1, &vs, nullptr); glCompileShader(v);
    glShaderSource(f, 1, &fs, nullptr); glCompileShader(f);
    g_prog = glCreateProgram();
    glAttachShader(g_prog, v); glAttachShader(g_prog, f);
    glLinkProgram(g_prog);
    glDeleteShader(v); glDeleteShader(f);
    g_u_rect = glGetUniformLocation(g_prog, "rect");
    g_u_uv = glGetUniformLocation(g_prog, "uvr");
    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) LOG("presenter shader failed to link");
}

struct Layout { float x, y, w, h; float u0, v0, u1, v1; };

Layout compute_layout(int dw, int dh, int fw, int fh, int mode) {
    Layout l{0, 0, (float)dw, (float)dh, 0, 0, 1, 1};
    float sx = (float)dw / fw, sy = (float)dh / fh;
    float s = std::min(sx, sy);
    if (mode == 1 && s >= 1.0f) s = std::floor(s);       // integer
    if (mode == 2) return l;                              // stretch
    if (mode == 3) {                                      // zoom: fill, crop the overflow
        float z = std::max(sx, sy);
        float vx = dw / (fw * z), vy = dh / (fh * z);
        l.u0 = (1.0f - vx) * 0.5f; l.u1 = 1.0f - l.u0;
        l.v0 = (1.0f - vy) * 0.5f; l.v1 = 1.0f - l.v0;
        return l;
    }
    l.w = fw * s; l.h = fh * s;
    l.x = (dw - l.w) * 0.5f; l.y = (dh - l.h) * 0.5f;
    return l;
}

// ---- audio ---------------------------------------------------------------------------------------------
std::mutex g_amu;
rab_bridge g_bridge;
bool g_abridge_ready = false;
int g_ach = 1;
int g_asrc = -1;
std::vector<int16_t> g_vol;

size_t audio_cb(void*, int16_t* out, size_t frames) {
    std::lock_guard<std::mutex> l(g_amu);
    if (!g_abridge_ready) return 0;
    if (g_ach == 2) {
        rab_pull(&g_bridge, out, (int)frames);
    } else {
        int16_t mono[2048];
        size_t done = 0;
        while (done < frames) {
            size_t n = std::min<size_t>(frames - done, 2048);
            rab_pull(&g_bridge, mono, (int)n);
            for (size_t i = 0; i < n; i++) out[(done + i) * 2] = out[(done + i) * 2 + 1] = mono[i];
            done += n;
        }
    }
    return frames;
}

}  // namespace

extern "C" {

// ---- engine side ---------------------------------------------------------------------------------------

void gsr_host_frame_submit(const uint8_t* rgb, int w, int h) {
    if (!rgb || w <= 0 || h <= 0) return;
    {
        std::lock_guard<std::mutex> l(g_fmu);
        const size_t depth = g_interp_on.load() ? 3 : 1;
        if (g_qw != w || g_qh != h) { g_queue.clear(); g_qw = w; g_qh = h; }
        while (g_queue.size() >= depth) { g_queue.pop_front(); g_stat_dropped.fetch_add(1); }  // never fall behind: drop the oldest
        g_stat_submitted.fetch_add(1);
        g_queue.emplace_back(rgb, rgb + (size_t)w * h * 3);
    }
    g_frames.fetch_add(1);
    g_fcv.notify_one();
}

void gsr_host_audio_open(int channels) {
    std::lock_guard<std::mutex> l(g_amu);
    if (g_abridge_ready) return;
    g_ach = channels == 2 ? 2 : 1;
    rab_config cfg;
    rab_config_defaults(&cfg);
    cfg.channels = g_ach;
    cfg.source_rate = 65536.0;
    cfg.host_rate = DESCORE_AUDIO_RATE;
    cfg.target_ms = 60.0;
    cfg.preroll_ms = 250.0;
    if (rab_init(&g_bridge, &cfg) == 0) g_abridge_ready = true;
    else LOG("audio bridge init failed");
}

void gsr_host_audio_push(const int16_t* s, int frames) {
    if (!s || frames <= 0) return;
    int vol = gsr_settings_get(GSR_S_MUTE) ? 0 : gsr_settings_get(GSR_S_VOLUME);
    std::lock_guard<std::mutex> l(g_amu);
    if (!g_abridge_ready) return;
    if (vol != 100) {
        size_t n = (size_t)frames * g_ach;
        g_vol.resize(n);
        for (size_t i = 0; i < n; i++) g_vol[i] = (int16_t)((int32_t)s[i] * vol / 100);
        s = g_vol.data();
    }
    rab_push(&g_bridge, s, frames);
    if (g_asrc < 0) g_asrc = descore_audio_add_source(audio_cb, nullptr);
}

void gsr_host_audio_reset(void) {
    std::lock_guard<std::mutex> l(g_amu);
    if (g_abridge_ready) rab_reset(&g_bridge);
}

void gsr_host_audio_close(void) {
    int h;
    {
        std::lock_guard<std::mutex> l(g_amu);
        h = g_asrc;
        g_asrc = -1;
    }
    if (h >= 0) descore_audio_remove_source(h);
    std::lock_guard<std::mutex> l(g_amu);
    if (g_abridge_ready) { rab_free(&g_bridge); g_abridge_ready = false; }
}

void gsr_host_surface_size(int* w, int* h) {
    if (w) *w = descore_surface_width();
    if (h) *h = descore_surface_height();
}

void gsr_host_poll(GsrHostInput* o) {
    // Cheats page: infinite HP / PP are the engine's own switches; keep them equal to the saved settings.
    runtime_set_infinite_hp(gsr_settings_get(GSR_S_CHEAT_HP));
    runtime_set_infinite_pp(gsr_settings_get(GSR_S_CHEAT_PP));
    o->keyinput = gsr_controls_keyinput();
    o->fast_forward = gsr_controls_fast_forward();
    o->paused = descore_is_paused() || g_menu_open.load();
    o->quit = g_quit.load();

    // Once a second: what the engine is doing, so a slow or stuck fast-forward shows up in logcat.
    static auto last = std::chrono::steady_clock::now();
    static int last_frames = 0;
    auto now = std::chrono::steady_clock::now();
    if (now - last >= std::chrono::seconds(2)) {
        int f = g_frames.load();
        double secs = std::chrono::duration<double>(now - last).count();
        // Smart Auto: if Auto is on the GPU path and the engine has no spare time for two windows in a row,
        // this device is too slow for it (readback cost) - use the CPU renderer from now on.
        {
            static int slow_windows = 0;
            double headroom = (double)g_stat_idle_ns.load() / (secs * 1e9);
            bool judge = gsr_settings_get(GSR_S_RENDERER) == 0 && g_gpu_active.load() && !gsr_settings_get(GSR_S_AUTO_CPU) &&
                         !o->fast_forward && !o->paused;
            if (judge && headroom < 0.08) {
                if (++slow_windows >= 2) {
                    gsr_settings_set(GSR_S_AUTO_CPU, 1);  // saved: next launch starts on CPU
                    LOG("Auto renderer: GPU path too slow on this device, switching to CPU");
                }
            } else {
                slow_windows = 0;
            }
        }
        if (g_debug.load()) {
        LOG("presented %.1f fps, fast_forward=%d paused=%d keyinput=%04x", (f - last_frames) / secs,
            o->fast_forward, o->paused, o->keyinput);
        LOG("engine headroom: %.0f%% of the time spent waiting for the frame limiter (low = struggling to keep up)",
            100.0 * (double)g_stat_idle_ns.exchange(0) / (secs * 1e9));
        LOG("queue: submitted=%d dropped=%d screen-repeated-last-frame=%d (per %.1f s, interpolating=%d)",
            g_stat_submitted.exchange(0), g_stat_dropped.exchange(0), g_stat_repeated.exchange(0), secs, g_interp_on.load());
        } else {
            g_stat_idle_ns.store(0); g_stat_submitted.store(0); g_stat_dropped.store(0); g_stat_repeated.store(0);
        }
        last = now;
        last_frames = f;
    }
}

void gsr_host_engine_state(int running) {
    g_running.store(running);
    g_fcv.notify_all();
}
int gsr_host_frame_count(void) { return g_frames.load(); }
void gsr_host_request_quit(void) { g_quit.store(1); }
void gsr_host_set_menu_open(int open) { g_menu_open.store(open ? 1 : 0); }
void gsr_host_set_debug(int on) { g_debug.store(on ? 1 : 0); }
int gsr_host_debug(void) { return g_debug.load(); }
void gsr_host_set_display_hz(float hz) { g_display_hz.store(hz > 1.0f ? hz : 60.0f); }
float gsr_host_display_hz(void) { return g_display_hz.load(); }
void gsr_host_note_idle_ns(long long ns) { g_stat_idle_ns.fetch_add(ns); }
void gsr_host_set_interpolating(int on) { g_interp_on.store(on ? 1 : 0); }

/* Cheats page multiplier steps for the engine's reward hooks: 0 experience, 1 coins, 2 drop chance. */
int gsr_host_cheat_step(int which) {
    return gsr_settings_get(which == 0 ? GSR_S_CHEAT_EXP : which == 1 ? GSR_S_CHEAT_COIN : GSR_S_CHEAT_DROP);
}

int gsr_host_gpu_wanted(void) {
    if (g_gpu_status.load() == 2) return 0;
    switch (gsr_settings_get(GSR_S_RENDERER)) {
    case 1: return 0;
    case 2: return 1;
    default: return !gsr_settings_get(GSR_S_AUTO_CPU) && gsr_settings_get(GSR_S_VIEW_MODE) == 1;
    }
}
void gsr_host_gpu_report_init(int ok) {
    g_gpu_status.store(ok ? 1 : 2);
    LOG("GPU field renderer %s", ok ? "ready" : "unavailable; using the CPU renderer");
}
void gsr_host_gpu_report_frame(int drew) { g_gpu_active.store(drew ? 1 : 0); }
int gsr_host_gpu_status(void) { return g_gpu_status.load(); }
int gsr_host_gpu_active(void) { return g_gpu_active.load(); }

// ---- render side ---------------------------------------------------------------------------------------

int gsr_host_wait_frame(int timeout_ms) {
    std::unique_lock<std::mutex> l(g_fmu);
    g_fcv.wait_for(l, std::chrono::milliseconds(timeout_ms), [] { return !g_queue.empty(); });
    return !g_queue.empty();
}

void gsr_host_render(int sw, int sh) {
    if (!g_prog || !glIsProgram(g_prog)) { build_gl(); g_tex = 0; g_tex_w = g_tex_h = 0; }
    if (!g_tex || !glIsTexture(g_tex)) { glGenTextures(1, &g_tex); g_tex_w = g_tex_h = 0; g_tex_filter = -1; }

    glViewport(0, 0, sw, sh);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    int fw, fh;
    {
        std::lock_guard<std::mutex> l(g_fmu);
        if (g_queue.empty()) g_stat_repeated.fetch_add(1);
        if (!g_queue.empty()) {
            g_fbuf.swap(g_queue.front());
            g_queue.pop_front();
            g_fw = g_qw; g_fh = g_qh;
            g_upload = true;
        }
        if (g_fw <= 0) return;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_tex);
        if (g_upload) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            if (g_tex_w != g_fw || g_tex_h != g_fh) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, g_fw, g_fh, 0, GL_RGB, GL_UNSIGNED_BYTE, g_fbuf.data());
                g_tex_w = g_fw; g_tex_h = g_fh;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_fw, g_fh, GL_RGB, GL_UNSIGNED_BYTE, g_fbuf.data());
            }
            g_upload = false;
        }
        fw = g_fw; fh = g_fh;
    }
    int filter = gsr_settings_get(GSR_S_FILTER);
    if (filter != g_tex_filter) {
        GLint f = filter ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        g_tex_filter = filter;
    }

    Layout L = compute_layout(sw, sh, fw, fh, gsr_settings_get(GSR_S_SCALE_MODE));
    glBindVertexArray(0);
    glUseProgram(g_prog);
    glUniform4f(g_u_rect, L.x / sw * 2.0f - 1.0f, (sh - (L.y + L.h)) / sh * 2.0f - 1.0f,
                (L.x + L.w) / sw * 2.0f - 1.0f, (sh - L.y) / sh * 2.0f - 1.0f);
    glUniform4f(g_u_uv, L.u0, L.v0, L.u1, L.v1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glUseProgram(0);

    // Frames per second counter (Settings > Display), drawn over the corner of the picture.
    if (gsr_settings_get(GSR_S_SHOW_FPS)) {
        static auto last = std::chrono::steady_clock::now();
        static int last_frames = 0;
        static double fps = 0;
        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - last).count();
        if (secs >= 0.5) {
            int f = g_frames.load();
            fps = (f - last_frames) / secs;
            last_frames = f;
            last = now;
        }
        char buf[24];
        snprintf(buf, sizeof buf, "%d FPS", (int)(fps + 0.5));
        int scale = sw / 480 < 2 ? 2 : sw / 480;
        gsr_text_draw(sh, scale * 3, scale * 3, scale, buf, 1.0f, 1.0f, 0.2f);
    }
}

}  // extern "C"
