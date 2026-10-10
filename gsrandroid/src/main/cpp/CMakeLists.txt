cmake_minimum_required(VERSION 3.10.2)
project(gsrhost C CXX)

# Golden Sun Recompiled for Android -- native build.
#
# One shared library, gsrhost, made of layers:
#
#   descore/  the shared descore shell (JNI bridge, video present, audio mixer, touch overlay).
#             Game-agnostic; carried between ports. Built here in its GLES 3 mode.
#   gsr/      the game side. Milestone 0: a smoke test that checks the picked ROM and draws a
#             status colour. Later: the GSRecomp engine (src/ and gbarecomp/) and the code
#             generated from the player's ROM, behind a descore-backed HostWindow.
#
# The library name has to match DescoreGameConfig.nativeLibrary (see GsrConfig.java), and the JNI
# symbols in descore_jni.c are named for wootbeer.descore.DescoreView.

find_library(log-lib log)
find_library(GLESv3-lib GLESv3)
find_library(EGL-lib EGL)
find_library(android-lib android)

set(DESCORE_SOURCES
		descore/descore_jni.c
		descore/descore_video.c
		descore/descore_audio.c
		descore/touch/descore_touch.c
)

# tcc (libtcc, LGPL) compiled in-process as a static library: it turns the game code that is
# translated from the player's own ROM into a native library on the device (see tools/ONDEVICE_SPIKE.md).
add_library(tcc STATIC tcc/libtcc.c)
target_include_directories(tcc PUBLIC tcc)
target_compile_definitions(tcc PRIVATE ONE_SOURCE=1 TCC_TARGET_ARM64=1 TCC_IS_NATIVE=1 CONFIG_TCC_STATIC=1)
target_compile_options(tcc PRIVATE -w)

# ---- GSRecomp's translator and player builder, run inside the app ---------------------------------
# gsrecomp/ holds unmodified copies of the translator (gba_recompile), its libraries and the player
# builder (gsr_builder), except for the GSR_ANDROID_INPROCESS hooks in builder_main.cpp. No Golden Sun
# code is in here: the game code is translated from the player's own ROM on the device.
set(GSRC ${CMAKE_CURRENT_SOURCE_DIR}/gsrecomp)
set(GSRC_GB ${GSRC}/gbarecomp)
add_library(gsrecomp_core STATIC
		${GSRC_GB}/src/armv4t/arm_decode.cpp
		${GSRC_GB}/src/armv4t/thumb_decode.cpp
		${GSRC_GB}/src/armv4t/arm_ir.cpp
		${GSRC_GB}/src/armv4t/arm_codegen.cpp
		${GSRC_GB}/src/armv4t/symbol_lookup.cpp
		${GSRC_GB}/src/armv4t/interpreter.cpp
		${GSRC_GB}/src/recompile/function_finder.cpp
		${GSRC_GB}/src/recompile/emit_function.cpp
		${GSRC_GB}/src/gba/gba_memory.cpp
		${GSRC_GB}/src/gba/gba_bus.cpp
		${GSRC_GB}/src/gba/gba_io.cpp
		${GSRC_GB}/src/gba/gba_irq.cpp
		${GSRC_GB}/src/gba/gba_dma.cpp
		${GSRC_GB}/src/gba/gba_timers.cpp
		${GSRC_GB}/src/gba/gba_ppu.cpp
		${GSRC_GB}/src/gba/gba_vram_trace.cpp
		${GSRC_GB}/src/gba/gba_audio.cpp
		${GSRC_GB}/src/gba/gba_m4a.cpp
		${GSRC_GB}/src/gba/audio_shadow.cpp
		${GSRC_GB}/src/gba/turbo_audio_scheduler.cpp
		${GSRC_GB}/src/gba/audio_event_capture.cpp
		${GSRC_GB}/src/gba/mp2k_wall_mixer.cpp
		${GSRC_GB}/src/gba/mp2k_wall_update_queue.cpp
		${GSRC_GB}/src/gba/mp2k_shadow.cpp
		${GSRC_GB}/src/gba/gba_input.cpp
		${GSRC_GB}/src/gba/gba_save.cpp
		${GSRC_GB}/src/gba/gba_rtc.cpp
		${GSRC_GB}/src/gba/gba_bios.cpp
		${GSRC_GB}/src/gba/gba_scheduler.cpp
		${GSRC_GB}/src/gba/gba_rom_header.cpp
		${GSRC_GB}/src/gba/sha1.cpp
		${GSRC_GB}/src/gba/crc32.cpp
		${GSRC_GB}/tools/gba_recompile/main.cpp
		${GSRC_GB}/tools/gba_recompile/config.cpp
		${GSRC_GB}/tools/gba_recompile/slim_pass.cpp
		${GSRC}/tools/gsr_builder/builder_main.cpp
		${GSRC}/tools/gsr_builder/guest_call.cpp
		${GSRC}/tools/gsr_builder/block_timing.cpp
		# ---- the engine core: ARM runtime, bus bridge, host seam, debug server ----
		${GSRC_GB}/src/armv4t/runtime_arm.cpp
		${GSRC_GB}/src/debug/cosim.cpp
		${GSRC_GB}/src/debug/cosim_state.cpp
		${GSRC_GB}/src/debug/snapshot.cpp
		${GSRC_GB}/src/debug/tcp_debug_server.cpp
		${GSRC_GB}/src/debug/trace_ring.cpp
		${GSRC_GB}/src/runtime/arm_cpu_bridge.cpp
		${GSRC_GB}/src/runtime/asset_picker.cpp
		${GSRC_GB}/src/runtime/bios_hle.cpp
		${GSRC_GB}/src/runtime/color_lut.cpp
		${GSRC_GB}/src/runtime/crash_handler.cpp
		${GSRC_GB}/src/runtime/frame_interpolation.cpp
		${GSRC_GB}/src/runtime/frame_timing.cpp
		${GSRC_GB}/src/runtime/generated_dispatch.cpp
		${GSRC_GB}/src/runtime/heal_gate.cpp
		${GSRC_GB}/src/runtime/host_config_ui.cpp
		${GSRC_GB}/src/runtime/host_overlay.cpp
		${GSRC_GB}/src/runtime/host_platform.cpp
		${GSRC_GB}/src/runtime/host_window.cpp
		${GSRC_GB}/src/runtime/host_window_android.cpp
		${GSRC_GB}/src/runtime/screen_filter.cpp
		${GSRC_GB}/src/runtime/no_bios_dispatch_table.cpp
		${GSRC_GB}/src/runtime/overlay_compile.cpp
		${GSRC_GB}/src/runtime/overlay_emit.cpp
		${GSRC_GB}/src/runtime/overlay_loader.cpp
		${GSRC_GB}/src/runtime/ram_overlay_registry.cpp
		${GSRC_GB}/src/runtime/runtime.cpp
		${GSRC_GB}/src/runtime/runtime_arm_default_aborts.cpp
		${GSRC_GB}/src/runtime/runtime_bus_bridge.cpp
		${GSRC_GB}/src/runtime/self_heal_cluster.cpp
		gsr/gpu_surface_android.cpp
)
set_source_files_properties(${GSRC_GB}/src/runtime/host_window.cpp
		PROPERTIES COMPILE_DEFINITIONS "GSR_ANDROID_HOST")
set_source_files_properties(${GSRC_GB}/src/armv4t/runtime_arm.cpp
		PROPERTIES COMPILE_DEFINITIONS "GSR_ANDROID_LATEBIND")
set_source_files_properties(${GSRC_GB}/src/runtime/host_config_ui.cpp ${GSRC_GB}/src/runtime/runtime.cpp
		PROPERTIES COMPILE_DEFINITIONS "GBARECOMP_DEFAULT_DEBUG_PORT=19843;GBARECOMP_DEFAULT_GAME_CONFIG=\"game.toml\"")
# GSR_ANDROID_RUNTIME: the interpolation fallback patch in runtime.cpp.
set_property(SOURCE ${GSRC_GB}/src/runtime/runtime.cpp APPEND PROPERTY COMPILE_DEFINITIONS "GSR_ANDROID_RUNTIME")
set_source_files_properties(${GSRC_GB}/tools/gba_recompile/main.cpp
		PROPERTIES COMPILE_DEFINITIONS "main=gba_recompile_main")
set_source_files_properties(${GSRC}/tools/gsr_builder/builder_main.cpp
		PROPERTIES COMPILE_DEFINITIONS "main=gsr_builder_main;GSR_ANDROID_INPROCESS")
target_include_directories(gsrecomp_core PRIVATE
		${GSRC_GB}
		${GSRC_GB}/src/armv4t ${GSRC_GB}/src/gba ${GSRC_GB}/src/recompile ${GSRC_GB}/src/debug
		${GSRC_GB}/src/runtime ${GSRC}/config/usa
		${GSRC_GB}/tools/gba_recompile ${GSRC}/tools/gsr_builder
		${CMAKE_CURRENT_SOURCE_DIR}/gsr)
# CMake 3.10 (pinned in build.gradle) has no cxx_std_20 feature, so ask for C++20 directly
target_compile_options(gsrecomp_core PRIVATE -std=c++20)
target_compile_options(gsrecomp_core PRIVATE -w -fPIC -O2)

set(GSR_SOURCES
		gsr/gsr_main.c
		gsr/gsr_prepare.c
		gsr/gsr_text.c
		gsr/gsr_cc.c
		gsr/gsr_shim.c
		gsr/gsr_headers.c
		gsr/gsr_engine.c
		gsr/gsr_build.c
		gsr/gsr_android.cpp
		gsr/gsr_settings.c
		gsr/gsr_controls.c
		gsr/gsr_host.cpp
		gsr/gsr_screen_filter.cpp
		gsr/gsr_jni.c
)

add_library(gsrhost SHARED ${DESCORE_SOURCES} ${GSR_SOURCES})
target_include_directories(gsrhost PRIVATE descore gsr ${GSRC_GB}/src/runtime)
# GLES 3 mode of the shared shell: no fixed-function video present, shader-based touch overlay.
target_compile_definitions(gsrhost PRIVATE DESCORE_GLES3)

# The engine core lives in gsrecomp_core; link all of it (whole-archive) so libgsrengine.so and the
# game libraries built on the device find every engine symbol exported from libgsrhost.so.
# PRIVATE: libgsrengine.so links against libgsrhost.so only. Left public, CMake would also link the
# static engine core into libgsrengine.so, giving it a second copy of every engine global.
target_link_libraries(gsrhost PRIVATE
		-Wl,--whole-archive gsrecomp_core -Wl,--no-whole-archive
		tcc
		${log-lib}
		${GLESv3-lib}
		${EGL-lib}
		${android-lib}
		m)


# ---- libgsrengine.so: the part of the engine that names game symbols ---------------------------
# runner_main.cpp references the dispatch tables and entry points of the game code, which only exist
# once the device has built them. So it is a separate library, dlopen()ed after the game libraries
# (gsr_engine.c); its undefined game symbols resolve against those.
add_library(gsrengine SHARED
		gsr/gsr_engine_entry.cpp
		gsrecomp/src/runner_main.cpp
		gsrecomp/src/function_tracer.cpp
		gsrecomp/src/map_recorder.cpp
		gsrecomp/src/obj_recorder.cpp
		gsrecomp/src/room_buffer.cpp
		gsrecomp/src/field_scene.cpp
		gsrecomp/src/field_scene_renderer.cpp
		gsrecomp/src/world_map_source.cpp
		gsrecomp/src/effect_particles.cpp
		gsrecomp/src/effect_sources.cpp
		gsrecomp/src/effect_capture.cpp
		gsrecomp/src/mod_loader.cpp
		gsrecomp/src/gs_text_encode.cpp
		gsrecomp/src/move_probe.cpp
		gsrecomp/src/unpacker_catch.cpp
		gsrecomp/src/unpacker_guard.cpp
		gsrecomp/src/earth_surge.cpp
		gsrecomp/src/effect_burst.cpp)
set_source_files_properties(gsrecomp/src/runner_main.cpp PROPERTIES COMPILE_DEFINITIONS "main=gsr_engine_main_cpp")
target_include_directories(gsrengine PRIVATE
		gsr/shim_inc
		${GSRC}/src ${GSRC_GB}
		${GSRC_GB}/src/armv4t ${GSRC_GB}/src/gba ${GSRC_GB}/src/runtime ${GSRC_GB}/src/debug
		${GSRC_GB}/src/recompile ${GSRC}/config/usa)
target_compile_options(gsrengine PRIVATE -std=c++20 -w -fPIC -O2)
target_compile_definitions(gsrengine PRIVATE GBARECOMP_DEFAULT_DEBUG_PORT=19843 GSR_ANDROID_ENGINE)
# game symbols are undefined here on purpose
set_target_properties(gsrengine PROPERTIES LINK_FLAGS "-Wl,-z,undefs -Wl,--no-as-needed")
# libgsgamebridge.so here is an empty stand-in. On the device the real one is made after the game
# libraries (gsr_cc_link_bridge) and loaded before libgsrengine.so, which then finds its game symbols
# through it. Never contains game code.
add_library(gsgamebridge SHARED gsr/bridge_stub.c)
target_link_libraries(gsrengine gsgamebridge gsrhost ${log-lib} m)
