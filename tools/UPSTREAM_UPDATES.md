# Upstream updates and credits

This port vendors part of Golden Sun Recompiled ("GSRecomp") under `gsrandroid/src/main/cpp/gsrecomp/`.
Since GSRecomp 0.5 upstream is a collection of two games on one engine; we vendor the engine and the first
game only:

| Ours | Upstream (0.5+) | Before 0.5 |
|---|---|---|
| `gsrecomp/engine/` | `engine/` | `gbarecomp/` |
| `gsrecomp/tbs/` | `The Broken Seal/` (src, config) | `src/`, `config/` |
| `gsrecomp/player_builder/` | `player_builder/` | `tools/gsr_builder/` |
| `gsrecomp/launcher/` | `launcher/` (headers kept for reference, not built) | `src/launcher_main.cpp` |

`tbs` is short for The Broken Seal (no spaces in build paths). The Lost Age (`The Lost Age/`) is not
vendored. The Android-specific code lives beside it, so an engine
update is mostly a folder swap plus re-applying the small marked patches below.

## Credits

- **Golden Sun Recompiled** by Shmargus: https://github.com/Shmargus/GSRecomp (the Golden Sun layer we vendor).
  Its own LICENSE says the licence for its original material is not yet selected; keep its LICENSE and
  ATTRIBUTION.md with the vendored copy.
- **gbarecomp** (the emulation and recompilation engine) by Matthew Stan: https://github.com/mstan/gbarecomp
  (PolyForm Noncommercial 1.0.0, `gsrecomp/engine/LICENSE`, "Copyright (c) 2026 Matthew Stan").
  This makes the whole app noncommercial.
- Third-party work that upstream itself credits is listed in
  `gsrecomp/engine/THIRD_PARTY_ATTRIBUTION.md` (JRickey/gba-recomp, mGBA, and others). That file
  travels with the vendored engine and must stay in the repo and in the About/credits screen.
- TinyCC (LGPL) is bundled to translate and compile on device. `tcc/tccelf.c` is modified (see below),
  so the LGPL notice and a statement of that change must ship with the app.
- The Android shell is built on Descore.

Keep these credits and the licence files in the repository and in the app's credits screen. Do not
remove or reword upstream copyright headers when updating.

## Our additions (not in upstream)

- `gsr/` : host seam and Android glue (`gsr_host`, `gsr_controls`, `gsr_settings`, `gsr_jni`,
  `gsr_main`, `gsr_engine`, `gsr_text`, `gpu_surface_android.cpp`)
- `gsrecomp/engine/src/runtime/host_window_android.cpp` : the HostWindow backend (also the no-op window hand-over, Seamless screen-veil and local-music functions)
- `descore/` additions: `descore_is_paused`, `descore_request_menu`
- Java: `wootbeer.gsrandroid.*` (menu, native bridge, activity)

## Upstream files we patched (re-apply after every update)

Each patch is marked `GSR_ANDROID` or guarded by a `GSR_ANDROID_*` define.

| File | Patch |
|---|---|
| `engine/src/runtime/host_window.cpp` | stub guard becomes `#elif !defined(GSR_ANDROID_HOST)` so the Android backend is used |
| `engine/src/armv4t/runtime_arm.cpp` | `GSR_ANDROID_LATEBIND` |
| `engine/src/runtime/runtime_bus_bridge.cpp` | `__ANDROID__`: times the picture drawing (`GsrPpuTimer`) for the timing log |
| `engine/src/runtime/runtime.cpp` | `GSR_ANDROID_RUNTIME`: the timing log `gsr_perf_log.txt` (path from `GSR_PERF_LOG`, set in `gsr_prepare.c`). Also: when 2x frame interpolation cannot build an in-between frame, blend the previous and new real frames 50/50 instead of jumping to the new one, and tell the host (`gsr_host_present_slot`) which present is the in-between frame so flicker reduction can treat it like the real frames (CMake adds the define) |
| `engine/src/runtime/host_window_android.cpp` (ours) | `present_native` / `native_renderer_enabled` back the High-res rendering option |
| `tbs/src/runner_main.cpp` | `GSR_ANDROID_ENGINE`: `gpu_field_enabled()` reads the Renderer setting live; `gpu_field_present_override` is wrapped to report whether the GPU drew the frame; init result reported |
| `player_builder/builder_main.cpp` | `GSR_ANDROID_INPROCESS`: in-process translator entry, and the `spawn` helper -- a single-threaded process started first thing in `main` that forks every translator (forking from the multithreaded builder could freeze a translator at 59%) and runs the watchdog |
| `engine/src/runtime/crash_handler.cpp` | Android crash handling (bionic has no `backtrace()`) |
| `tcc/tccelf.c` (line ~3920) | on-device linking change (LGPL: keep notice) |
| `gsr/gsr_screen_filter_shaders.inc` (ours, copied) | verbatim copy of `screen_filter.cpp`'s shared fragment header and the LCD3x, xBR-lv2 and CRT Lottes shaders with their notices; copy it again when upstream changes them |
| `tbs/src/unpacker_catch.cpp` | `GSR_ANDROID`: no `<execinfo.h>`/`backtrace()` (API 33+), the host call chain is left out of the diagnostic |
| `tbs/src/runner_main.cpp` (cheats) | `GSR_ANDROID_CHEATS`: the side menu's Cheats page owns the reward multipliers |
| `tbs/src/room_buffer.cpp` | `GSR_ANDROID_ENGINE`: map numbers 0/1 and the title's BGCNT set are not rooms; `room_buffer_row_is_room()` |
| `tbs/src/runner_main.cpp` (more) | `GSR_ANDROID_ENGINE`: margin policy pillarboxes non-room rows; GPU hands boot/title/intro frames to CPU; camera inset skipped for the title; boot/title/intro shown as the native 240x160 part of the canvas (`gsr_host_present_crop`, read in host_window_android.cpp); intro flare averaging; camera-glide blackout |
| `tbs/src/runner_main.cpp` (Seamless) | `GSR_ANDROID_ENGINE`: Seamless mode (hand-over to The Lost Age, new in 0.5) is always off and the new-game prompt does not ask about it |
| `tbs/src/settings_page.h` | `GSR_ANDROID_ENGINE`: the in-game settings page leaves out Screen and No Slowdown (the side menu owns them) |
| `CMakeLists.txt` | source lists, include dirs, `GSR_ANDROID_*` defines, `gpu_surface_android.cpp` instead of upstream `gpu_surface.cpp` |

Files we replace rather than patch: upstream `gpu_surface.cpp` (desktop GL + SDL) is not built; its
GLES3 replacement must keep the `gpu_surface.h` interface. (0.4.3 added `begin/finish_texture_readback`;
ours queues the copy with `glReadPixels` into a pixel-pack buffer and maps it a frame later. 0.5 made
`read_texture_rgb` return bool and added `cancel_texture_readback`, used by `texture_readback_queue.h`.) After an update, diff `gpu_surface.h` and
`gpu_surface.cpp` for new methods and mirror them in `gpu_surface_android.cpp`. Also re-check that all
shaders in `field_scene_renderer.cpp` still validate as GLSL ES 3.00 after the `#version 130` rewrite
(`glslangValidator` on the rewritten source catches this offline).

## Update procedure

1. Note the upstream commit you are moving from and to.
2. Replace the vendored engine folders; keep the `GSR_ANDROID*` patches (search for the marker).
3. New upstream `.cpp` files: add to `CMakeLists.txt`. Removed files: remove them.
4. Re-run the shader check and build.
5. Bump `GSR_BUILD_VERSION` in `gsr/gsr_prepare.h` so devices rebuild their translated game code.
6. Test: boot, field, battle, world map, Expanded view, fast-forward, menu, save/load.
7. Update the vendored engine version recorded here.

The view modes now come from `engine/src/runtime/view_config.h` (Original 240x160, Wide 360x240,
Extra Wide 480x320). The side menu's View setting still offers the first two; the Android HostWindow
reports the Original aspect, so a mode's size is used as is.

Vendored engine version: 0.5.3 (upstream commit 27fedb5), updated 2026-10-20 from https://github.com/Shmargus/GSRecomp
(previously 0.5.2 = 7958fa6, updated 2026-10-19; 0.4.4 = 8770e0d, updated 2026-10-12; 0.4.3 = 53d2eeb, updated 2026-10-09; 0.3.1 = ddde2e2; before that 0.2.3).

How the update is done (repeat this next time): a three-way git merge with base = the upstream commit we
were on (only the files we vendor), ours = the vendored folder, theirs = the new upstream commit. Only files we
patched can conflict. For 0.5.2 the base and ours trees were first moved to the new layout (table above) so
git could match the renamed files. Then copy changed `config/usa` files into `assets/gsr_builder_data`, mirror any
`runtime_arm.h` change into `gsr/hdr/runtime_arm.h` (C-only copy) and run `tools/embed_headers.py`, validate the
`field_scene_renderer.cpp` shaders as GLSL ES 3.00, check the launcher's environment variables
(upstream `launcher/session_environment.h` and `launcher/session_linux.cpp`) against `gsr/gsr_main.c`, and bump `GSR_BUILD_VERSION`.
