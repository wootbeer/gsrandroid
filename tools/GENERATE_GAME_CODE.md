# Generating the game code (ROM-derived, never committed)

The Golden Sun game code is generated from your own ROM by GSRecomp's builder. The output
goes to `gsrandroid/src/main/cpp/gsr_generated/` (gitignored, ~510 MB, ~430 recompiled
files, ~12.6M lines).

Steps used (Linux; the Windows release's `builder\gsr_builder.exe` accepts the same
`--generate-only --keep-work` flags):

1. Build `gba_recompile` from `GSRecomp-main/gbarecomp` (CMake, target `gba_recompile`).
2. Build `gsr_builder` from `GSRecomp-main/tools/gsr_builder` (see CMakeLists.txt, GSR_BUILDER_SOURCES).
3. `gsr_builder --rom <rom.gba> --recompiler <gba_recompile> --data GSRecomp-main/config/usa
   --engine <dir with include/runtime_arm.h> --toolchain <dir with bin/g++> --work <dir>
   --keep-work --generate-only`
4. Apply block timing to `<work>/gen` (gsr_builder does this on a normal run, but skips it
   with --generate-only). Without it the heavy spell effects run much slower.
5. Copy `recompiled*.cpp`, `recompiled.h`, `dispatch_table*.cpp`, `symbol_map.cpp` (main only)
   and `stamp_registry.cpp` (stamps only), keeping the sub-folder structure, into gsr_generated/.

Compile these with `-DGBARECOMP_OUTLINE_BUS=1`, as the PC build does.

Note: step 3 once stopped after 143 of 144 steps when run as a background job; running the
`main` step by hand (`gba_recompile --config main.toml --max-functions 40000 --slim`) finished
in ~50 s with ~80 MB of RAM.
