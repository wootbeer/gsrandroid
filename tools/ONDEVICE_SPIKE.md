# On-device compile spike (x86 VM stand-in, 2 cores)

Local dev notes only. Nothing here ships; the generated code never goes in the repo or APK.

| Test | Result |
|---|---|
| One 4.8 MB shard, g++ -std=c++20 -O0 | 8.7 s, 530 MB peak RAM |
| Same shard, g++ -Og | 9.3 s, 420 MB peak RAM |
| Same shard as C (gcc -x c -O0) | 4.3 s, 124 MB peak RAM |
| Same shard, tcc | 0.09 s, 26 MB peak RAM |
| Whole game (577 shards, ~490 MB of source), tcc | ~27 s, one process |
| Link all objects (tcc -shared) | 1.2 s, ~790 MB peak RAM, 236 MB .so |

Shards are effectively C. Making them tcc-compilable needed only text shims:
- `extern "C" ` -> `extern ` and drop the closing `}` of the extern "C" block in recompiled.h
- `#include <cstdint>` -> `<stdint.h>`, plus `-include stdbool.h`
- `enum : uint32_t {` -> `enum {` in runtime_arm.h
- `struct DispatchEntry {...};` -> `typedef struct DispatchEntry {...} DispatchEntry;`

Not yet measured: runtime speed of tcc code (no optimizer), tcc on aarch64/Android, loading the result.

## Runtime test (x86 VM, Linux engine headless, game code compiled by tcc)

Engine built from GSRecomp (SDL2 + GL stubbed, `--no-window --no-bios`), linked against a
`libGoldenSunGame.so` made by tcc from all 577 shards + stamp_registry (symbol_map skipped).
Boots the real ROM, FULLY_STATIC, 0 dispatch misses, 0 interpreted instructions.

| Frames (headless) | Wall time | Per frame |
|---|---|---|
| 600 | 1.4 s | 2.3 ms |
| 1800 | 4.5 s | 2.5 ms |
| 7200 | 18.4 s | 2.6 ms (about 1.1 ms of it is PPU) |

Real-time budget is 16.7 ms/frame. CAVEAT: with no input the game sits at the same PC
(0x08003324, about 190k cycles/frame, so mostly idle). This is the title/idle load only, not
battles or heavy spells. Needs an input replay or savestate to test those.

### Overworld test from a real save (tcc game code, headless, scripted input)
Loaded the .sav (copy), Start/A/A, then walked in a square for 12000 frames: about 3.2 ms/frame
total (PPU about 1.4 ms) on the x86 VM, FULLY_STATIC. Rain and lightning effects on screen.
No battle was triggered in 12000 frames (probably a no-encounter zone, or too little walking).
Next: record an input trace on PC into a battle (GBARECOMP_INPUT_RECORD=trace.txt writes the
trace plus a .state beside it) and replay it here with GBARECOMP_INPUT_REPLAY.

### Battle replay (a recorded trace, tcc game code, x86 VM, headless)
Replay of battle_trace.txt reproduces the fight (Bat vs Isaac and Garet, rain). FULLY_STATIC,
0 misses. Frames 5000-5700 (the battle) cost 3.1-3.7 ms/frame total (PPU about 1.4 ms) versus
16.7 ms budget. Only physical attacks in this recording; heavy Psynergy not yet measured.

## Android spike build (arm64, libtcc in-process)
Code: cpp/tcc (vendored tcc, LGPL), cpp/gsr/gsr_cc.* (compile/link wrapper), gsr_shim.* (declaration
syntax shim), hdr/ + gsr_headers.c (freestanding headers, regenerate with tools/embed_headers.py),
gsr_spike.* (runner), gsr_engine_stubs.c (placeholder engine symbols, remove when the engine lands).
Verified on the VM: builds with NDK r26d clang for arm64-v8a (libgsrhost.so exports the 43 engine
symbols, SONAME ok); the same wrapper on x86 compiled all 578 shards, linked, and dlopen()ed OK.
NOT verified: running on arm64/bionic.

Run on the Retroid (adb cannot create folders under /sdcard/Android/data on Android 11+, so the
shards go into the app's internal folder with run-as; needs a debug build):
  $adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
  & $adb push "<gsr_generated folder>" /data/local/tmp/
  & $adb shell run-as wootbeer.gsrandroid mkdir -p files/spike_in
  & $adb shell run-as wootbeer.gsrandroid cp -r /data/local/tmp/gsr_generated files/spike_in/gen
  & $adb shell run-as wootbeer.gsrandroid touch files/spike_in/RUN
  & $adb shell rm -r /data/local/tmp/gsr_generated
then launch the app. Optional files/spike_in/workers.txt (1-8). Screen: blue + bar = running, cyan = success,
magenta = failed. Results: /sdcard/Android/data/wootbeer.gsrandroid/files/spike/report.txt and logcat tag gsr-spike.

## RESULT on Retroid Pocket 6 (QCS8550, Android 13, 8 cores), arm64 libtcc, 4 workers
- Compile: 578 shards in 6.7 s wall (26 s CPU), peak 194 MB per worker, 0 failures.
- Link: arm64 direct branches reach only +-128 MiB, so one 236 MB library fails (PLT out of range). Folders never
  call each other, so they are packed into 4 libraries (60/63/62/55 MB), 3.4 s total, peak 331 MB.
- dlopen of all 4: 51 ms; symbols and data relocations correct. Whole spike 10.5 s.
Not yet on device: the ROM -> C translation step (gba_recompile, about 4 min on the x86 VM), running the code under the engine.

## Step 2: the whole flow on the device (ROM -> translate -> block timing -> compile -> link -> load)
Code: cpp/gsrecomp (GSRecomp's gba_recompile + gsr_builder, copied; only GSR_ANDROID_INPROCESS hooks added in
tools/gsr_builder/builder_main.cpp, search for GSR-ANDROID), gsr/gsr_build.* (compile + pack + link + load),
gsr/gsr_android.cpp (glue), gsr/gsr_spike.c (test driver). The translator runs as a function in a forked
child of the app (apps cannot exec programs from their storage).
Verified on the VM (x86 build of the same code, reduced build plan): builder exit 0, 2 libraries loaded.
Device test: files/spike_in/data = builder\data from the Windows release, plus empty RUN and TRANSLATE files;
the ROM is the one picked in the launcher (files/gsr/game/golden_sun.gba).
Free space first: run-as ... rm -r files/spike_in/gen files/spike_work
