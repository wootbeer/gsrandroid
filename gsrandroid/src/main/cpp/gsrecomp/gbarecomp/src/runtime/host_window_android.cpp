// host_window_android.cpp -- HostWindow backend for the Android port (GSR_ANDROID_HOST).
//
// The engine thread calls this exactly as it calls the SDL backend on the desktop; everything platform
// specific is behind gsr_host.h (frame mailbox + GLES3 presenter, audio into Descore's mixer, input).
// Settings come from gsr_settings.h; the Android settings screens change them while the game runs and
// pump() reports the changes to the runtime the same way the desktop menu does.

#include "host_window.h"

#include "color_lut.h"
#include "temporal_blend.h"
#include "gsr_host.h"
#include "gsr_settings.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace gbarecomp {

namespace {

struct State {
    int base_w = 240, base_h = 160;
    bool stereo = false;
    bool view_modes_on = false;
    int view_mode_count = 0;
    int last_view_reported = 0;
    bool last_paused = false;
    bool turbo_muted = false;
    bool native_available = false;
    bool interp_available = false;
    bool timing_available = false;
    bool interp_on = false;
    int lut_kind = -1;
    std::unique_ptr<runtime::ColorLut> lut;
    std::vector<uint8_t> graded;
    std::vector<uint8_t> blend_prev, blend_prev2, blend_out;
    int blend_history = 0;
    bool blend_did_last = false;       // the last real frame was blended (blend_prev = it, blend_out = result)
    std::vector<uint8_t> blend_mid;
    int slot = 0;                      // 1 while the engine presents a 2x-interpolation in-between frame
} g;

void refresh_lut() {
    const int kind = gsr_settings_get(GSR_S_SCREEN);
    if (kind == g.lut_kind && g.lut) return;
    runtime::ColorSettings cs;
    cs.screen = static_cast<runtime::ScreenKind>(std::clamp(kind, 0, 4));
    g.lut = std::make_unique<runtime::ColorLut>(cs);
    g.lut_kind = kind;
}

int clamped_view_mode() {
    if (!g.view_modes_on || g.view_mode_count <= 0) return 0;
    return std::clamp(gsr_settings_get(GSR_S_VIEW_MODE), 0, g.view_mode_count - 1);
}

}  // namespace

HostWindow::HostWindow() = default;
HostWindow::~HostWindow() { close(); }

bool HostWindow::is_available() { return true; }

bool HostWindow::open(int, int base_w, int base_h, const char*, const char*, bool, bool, bool stereo_audio) {
    if (open_) return true;
    g.base_w = base_w > 0 ? base_w : 240;
    g.base_h = base_h > 0 ? base_h : 160;
    g.stereo = stereo_audio;
    refresh_lut();
    gsr_host_audio_open(stereo_audio ? 2 : 1);
    impl_ = &g;
    open_ = true;
    return true;
}

void HostWindow::close() {
    if (!open_) return;
    gsr_host_audio_close();
    open_ = false;
    impl_ = nullptr;
}

bool HostWindow::set_surface_size(int base_w, int base_h) {
    if (base_w < 1 || base_h < 1) return false;
    g.base_w = base_w;
    g.base_h = base_h;
    return true;
}

bool HostWindow::drawable_size(int* width, int* height) const {
    int w = 0, h = 0;
    gsr_host_surface_size(&w, &h);
    if (width) *width = w;
    if (height) *height = h;
    return w > 0 && h > 0;
}

// Called by the engine (runtime.cpp, GSR_ANDROID_RUNTIME) around the in-between frame of 2x interpolation.
extern "C" void gsr_host_present_slot(int slot) { g.slot = slot; }

void HostWindow::present(const uint8_t* rgb888) {
    if (!open_ || !rgb888) return;
    refresh_lut();
    // Flicker reduction (the desktop menu's "Flicker reduction"): only pixels that alternate between two
    // colours on a two-frame period are blended, so normal motion is untouched.
    {
        const int level = std::clamp(gsr_settings_get(GSR_S_BLEND), 0, 3);
        const size_t bytes = static_cast<size_t>(g.base_w) * g.base_h * 3;
        if (level != 0) {
            if (g.blend_prev.size() != bytes || g.blend_prev2.size() != bytes) {
                g.blend_prev.assign(bytes, 0);
                g.blend_prev2.assign(bytes, 0);
                g.blend_history = 0;
                g.blend_did_last = false;
            }
            if (g.blend_out.size() != bytes) g.blend_out.resize(bytes);
            if (g.interp_on && g.slot == 1) {
                // An in-between frame (2x interpolation). Flicker reduction runs on the real frames only;
                // here the pixels it just smoothed in the last real frame get the same smoothed value, so
                // effects the game draws on alternate frames stay steady instead of showing in most frames.
                if (g.blend_did_last) {
                    g.blend_mid.assign(rgb888, rgb888 + bytes);
                    for (size_t i = 0; i + 3 <= bytes; i += 3) {
                        if (std::memcmp(&g.blend_out[i], &g.blend_prev[i], 3) != 0)
                            std::memcpy(&g.blend_mid[i], &g.blend_out[i], 3);
                    }
                    rgb888 = g.blend_mid.data();
                }
            } else {
                const bool did = g.blend_history >= 2;
                if (did)
                    temporal_blend_apply_selective(rgb888, g.blend_prev.data(), g.blend_prev2.data(),
                                                   g.blend_out.data(), bytes, kTemporalBlendWeights[level]);
                std::swap(g.blend_prev, g.blend_prev2);
                std::memcpy(g.blend_prev.data(), rgb888, bytes);
                if (g.blend_history < 2) ++g.blend_history;
                g.blend_did_last = did;
                if (did) rgb888 = g.blend_out.data();
            }
        } else {
            g.blend_history = 0;
        }
    }
    if (g.lut && !g.lut->is_passthrough()) {
        g.graded.resize(static_cast<size_t>(g.base_w) * g.base_h * 3);
        g.lut->map_rgb888(rgb888, g.graded.data(), g.base_w, g.base_h);
        rgb888 = g.graded.data();
    }
    gsr_host_frame_submit(rgb888, g.base_w, g.base_h);
}

// The enhanced (GPU) renderer arrives with the GLES3 port of the GPU surface; until then it is never offered.
// The engine's "native renderer": in the Original view it renders the picture supersampled (scale x the
// 240x160 grid) on the CPU and hands it over here. The frame goes through the same colour grading as normal
// frames; flicker reduction is skipped (the engine does the same on the desktop).
void HostWindow::present_native(const uint8_t* rgb888, int width, int height) {
    if (!open_ || !rgb888 || width <= 0 || height <= 0) return;
    const int scale = std::clamp(gsr_settings_get(GSR_S_NATIVE_SCALE), 1, 10);
    if (width != g.base_w * scale || height != g.base_h * scale) return;
    refresh_lut();
    if (g.lut && !g.lut->is_passthrough()) {
        g.graded.resize(static_cast<size_t>(width) * height * 3);
        g.lut->map_rgb888(rgb888, g.graded.data(), width, height);
        rgb888 = g.graded.data();
    }
    g.blend_history = 0;
    gsr_host_frame_submit(rgb888, width, height);
}

void HostWindow::load_input_config(const char*) {}
void HostWindow::set_fullscreen(bool) {}
bool HostWindow::fullscreen() const { return true; }
void HostWindow::adjust_scale(int) {}
void HostWindow::set_volume(int pct) { gsr_settings_set(GSR_S_VOLUME, pct); }
int HostWindow::volume() const { return gsr_settings_get(GSR_S_VOLUME); }
void HostWindow::set_fps_readout(bool on) { gsr_settings_set(GSR_S_SHOW_FPS, on ? 1 : 0); }
bool HostWindow::fps_readout() const { return gsr_settings_get(GSR_S_SHOW_FPS) != 0; }
double HostWindow::display_refresh_hz() const { return gsr_host_display_hz(); }

void HostWindow::set_frame_interpolation_available(bool on) { g.interp_available = on; }
void HostWindow::set_frame_interpolation_enabled(bool) {}
void HostWindow::set_enhanced_timing_available(bool on) { g.timing_available = on; }
void HostWindow::set_enhanced_timing_enabled(bool) {}
// Exact 60 Hz timing is always on where the game allows it (same as the desktop build); not a menu option.
bool HostWindow::saved_enhanced_timing() const { return open_; }
void HostWindow::set_native_renderer_available(bool on) { g.native_available = on; }
void HostWindow::set_native_renderer_enabled(bool) {}
void HostWindow::set_native_renderer_scale(int) {}
// Live from the menu setting; the engine itself only uses it in the Original view (never when the picture
// is widened or 2x frame interpolation is on).
bool HostWindow::native_renderer_enabled() const {
    return open_ && g.native_available && gsr_settings_get(GSR_S_NATIVE_RENDER) != 0;
}
int HostWindow::native_renderer_scale() const { return gsr_settings_get(GSR_S_NATIVE_SCALE); }

void HostWindow::set_fixed_view_modes(bool on, const FixedViewMode*, int count, int active_mode) {
    g.view_modes_on = on;
    g.view_mode_count = on ? count : 0;
    g.last_view_reported = clamped_view_mode();
    (void)active_mode;
}
int HostWindow::fixed_view_mode() const { return clamped_view_mode(); }
void HostWindow::set_widescreen_available(bool, bool) {}
bool HostWindow::widescreen_enabled() const { return clamped_view_mode() != 0; }
void HostWindow::set_guest_frame(std::uint64_t) {}
void HostWindow::set_debug_overlay_audio_state(bool, bool, bool, const char*, bool, bool, bool, bool, bool, bool,
                                               const char*) {}

void HostWindow::push_audio_samples(const int16_t* samples, std::size_t count) {
    if (!open_ || g.turbo_muted || !samples || count == 0) return;
    gsr_host_audio_push(samples, static_cast<int>(count));
}

void HostWindow::push_audio_samples_stereo(const int16_t* samples, std::size_t count) {
    if (!open_ || g.turbo_muted || !samples || count == 0) return;
    gsr_host_audio_push(samples, static_cast<int>(count));
}

void HostWindow::set_turbo_audio_muted(bool on) {
    if (g.turbo_muted != on) gsr_host_audio_reset();
    g.turbo_muted = on;
}
void HostWindow::reset_audio() { gsr_host_audio_reset(); }
void HostWindow::audio_bridge_stats(std::uint64_t* underruns, std::uint64_t* overflows) const {
    if (underruns) *underruns = 0;
    if (overflows) *overflows = 0;
}

HostWindow::Events HostWindow::pump() {
    Events ev{};
    GsrHostInput in{};
    gsr_host_poll(&in);
    ev.quit = in.quit != 0;
    ev.keyinput = in.keyinput;
    ev.fast_forward = in.fast_forward != 0;
    ev.turbo_multiplier = gsr_settings_get(GSR_S_FF_MULT10) / 10.0f;
    ev.turbo_uncapped = gsr_settings_get(GSR_S_FF_UNCAPPED) != 0;
    ev.turbo_mute_audio = gsr_settings_get(GSR_S_FF_MUTE) != 0;
    ev.enhanced_timing = g.timing_available;
    // 2x frame interpolation needs the display actually running at about 120 Hz (the Java side asks for
    // that mode when the option is turned on); without it the engine would present frames the screen drops.
    ev.frame_interpolation_2x = g.interp_available && gsr_settings_get(GSR_S_INTERP) != 0 &&
                                gsr_host_display_hz() >= 100.0f;
    g.interp_on = ev.frame_interpolation_2x;
    gsr_host_set_interpolating(g.interp_on ? 1 : 0);

    const int vm = clamped_view_mode();
    ev.view_mode = vm;
    if (vm != g.last_view_reported) {
        ev.view_mode_changed = true;
        g.last_view_reported = vm;
    }

    // App in the background / screen locked: pause the guest (toggle on each change of state).
    const bool paused = in.paused != 0;
    if (paused != g.last_paused) {
        ev.toggle_pause = true;
        g.last_paused = paused;
    }
    return ev;
}

}  // namespace gbarecomp
