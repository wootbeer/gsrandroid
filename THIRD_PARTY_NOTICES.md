# Third-party notices

This file collects the licence and attribution notices from the projects this port is built on, copied unchanged from the vendored copies in this repository (version 0.6 of Golden Sun Recompiled). The originals stay where they are and must stay with any copy of this project. See `COPYING` for the notice covering the Android port itself. Full licence texts are in the `licenses/` folder.

---

## Golden Sun Recompiled (Shmargus): licence statement

Source: https://github.com/Shmargus/GSRecomp, files `The Broken Seal/LICENSE` and `The Lost Age/LICENSE` (the same text), copied from `gsrandroid/src/main/cpp/gsrecomp/tbs/LICENSE` and `gsrandroid/src/main/cpp/gsrecomp/tla/LICENSE`. Its `../engine/` is `gsrecomp/engine/` here.

```
License not yet selected.

All rights are reserved for original material in this repository -- src/,
tools/, tests/, config/, scripts/ and the documentation -- until the project
owner selects and adds an explicit license.

Third-party components remain under their own licenses, and one of them binds
this whole project:

  The emulation and recompilation engine in ../engine/ (the collection's
  engine folder) is copyright Matthew Stan and is licensed under the PolyForm
  Noncommercial License 1.0.0. Its text is at ../engine/LICENSE and must stay
  with any copy. It permits use, modification and redistribution for
  noncommercial purposes only. Nothing here runs without that engine, so THIS
  PROJECT IS NONCOMMERCIAL, and any license later chosen for the original
  material cannot escape that while the engine is part of the build.

  Portions of the engine are derived from mGBA (MPL-2.0) and from
  JRickey/gba-recomp (MIT OR Apache-2.0); see
  ../engine/THIRD_PARTY_ATTRIBUTION.md and ../engine/MODIFICATIONS.md.

  Dear ImGui (MIT), toml++ (MIT) and SDL2 (zlib) are fetched or installed at
  build time and are not stored in this repository.

No Golden Sun ROM, BIOS, save or extracted asset is in this repository, and
none may be added; that material belongs to Nintendo and Camelot Software
Planning.

See ATTRIBUTION.md for who owns what, and docs/LEGAL.md before publishing,
distributing, or relicensing this project.
```

---

## Golden Sun Recompiled (Shmargus): attribution

Copied from `gsrandroid/src/main/cpp/gsrecomp/ATTRIBUTION.md` (upstream's `ATTRIBUTION.md`, one file for both games since 0.6). Its `engine/` is `gsrecomp/engine/` here. The Android app does not build the music mod player, so dr_flac is not part of it.

# What is in this repository, and who owns it

Plain answers first, because the licences here are not all the same and one of
them limits what this project may ever be.

## The short version

| Part | Whose | Licence | In this repo? |
|---|---|---|---|
| The games: Golden Sun and Golden Sun: The Lost Age, their ROMs, BIOS, art, music, text | Nintendo / Camelot Software Planning | Not ours to license | **No, and never** |
| The engine, `engine/` | Matthew Stan (mstan) | PolyForm Noncommercial 1.0.0 | Yes, an edited copy |
| Code ported into the engine from others | see "Inside the engine" below | MIT / Apache-2.0 / MPL-2.0 / public domain | Yes, inside `engine/src/` |
| Everything else: `launcher/`, `player_builder/`, `scripts/`, each game folder, the docs | this project | not yet selected; see `LICENSE` | Yes |
| Dear ImGui, toml++, dr_flac, SDL2 | their authors | MIT, MIT, public domain or MIT-0, zlib | No: fetched or installed at build time |
| Music mods (recordings in `Music/`) | their makers | their own terms | No, and never |

**The practical consequence: this project is noncommercial.** The engine's
licence permits use, modification and redistribution for noncommercial purposes
only. That covers the whole build, because nothing here runs without the engine.

## The games

No ROM, BIOS, save, savestate, screenshot of copyrighted art, or extracted
asset is in this repository, and none may be added. You supply your own legally
obtained ROMs; the launcher checks their SHA-1.

The C++ code the build produces is derived from your ROM. It is written to each
game's `local/` and build folders, which are not tracked here and must not be
published, and player releases never contain it.

What this repository does hold about the games is our own analysis: symbol
names, addresses, function boundaries and hardware observations in each game's
`config/` and `src/`: facts measured about the programs, not their content.

## The engine, `engine/`

Upstream is [`mstan/gbarecomp`](https://github.com/mstan/gbarecomp), copyright
Matthew Stan, licensed **PolyForm Noncommercial 1.0.0**; the full text is at
`engine/LICENSE` and stays there.

That licence allows what this repository does: use it, change it, and pass it
on, **for any purpose other than a commercial one**. It requires that anyone
who receives a copy also receives the licence, which is why `engine/LICENSE`
is committed alongside the code, and why every player release carries it in
`licenses/`.

`engine/` here is **not upstream**: it is a copy that diverged on 2026-07-17
and has been changed extensively since (the PPU, the ARM runtime, audio, the
overlay loader, the host window, the field renderer and the recompiler).
`engine/MODIFICATIONS.md` describes what changed and why. Building against
upstream will not reproduce this project.

Where a fix belongs: anything about ARMv4T, GBA hardware or the recompiler goes
inside `engine/`. Anything about Golden Sun stays in the game folders.

### Inside the engine

The engine ports code from other projects, credited in full in
`engine/THIRD_PARTY_ATTRIBUTION.md`:

- **[JRickey/gba-recomp](https://github.com/JRickey/gba-recomp)**, MIT OR
  Apache-2.0, used with the author's permission: the MP2K driver detection,
  the screen-colour simulation, the cartridge RTC, and the audio shadow
  verifier.
- **[mGBA](https://github.com/mgba-emu/mgba)**, MPL-2.0: the BIOS
  high-level-emulation routines in `engine/src/runtime/bios_hle.{h,cpp}`.
  Those files remain governed by the MPL-2.0; its text is at
  <https://mozilla.org/MPL/2.0/>. mGBA itself is not part of this repository.
- **Screen filters**, four GLSL shaders from libretro's
  [glsl-shaders](https://github.com/libretro/glsl-shaders), ported into
  `engine/src/runtime/screen_filter.cpp`: LCD3x by Gigaherz (public domain),
  xBR-lv2 by Hyllian (MIT), CRT Lottes by Timothy Lottes (public domain) and
  ScaleFX by Sp00kyFox (MIT). The MIT notices are kept verbatim above those
  shaders, as the licence requires.

## Our own code

Everything outside `engine/` is this project's work. `LICENSE` has not
selected a licence for it, so all rights are reserved by default: people may
read this repository, but nobody may reuse our code until a licence is chosen.
Whatever is chosen cannot escape the engine's noncommercial terms while the
engine is part of the build.

## Build-time dependencies, not in this repository

CMake fetches these at configure time, each pinned to an exact version, or uses
a local copy in `engine/third_party/` if one is present:

| Library | Use | Licence |
|---|---|---|
| Dear ImGui v1.91.9-docking | the launcher and the in-game menus | MIT |
| toml++ v3.4.0 | reading `game.toml` and the symbol metadata | MIT |
| dr_flac v0.13.4 | playing music mods' FLAC files | public domain or MIT No Attribution |

And these must be installed on the machine: a C++ toolchain (MinGW-w64 on
Windows, GCC 15+ on Linux), SDL2 (zlib licence), OpenGL, Python 3.10+, CMake
and Ninja. Player releases list the exact packages they bundle in
`THIRD_PARTY_NOTICES.txt`.

## Credits

- **Vincent Dortel aka [Juste-D](https://www.youtube.com/@JusteDni)**, whose
  remastered soundtracks are the first music mod, shared with his permission
  and kept outside this repository (see [MUSIC_MODS.md](MUSIC_MODS.md)).

## If any of this changes

Selling, sponsoring or otherwise monetising this project is the one thing the
engine's licence forbids, and it would have to be taken up with mstan first.
Adding a dependency means adding a row here. Adding game data is never allowed.

---

## gbarecomp third-party attribution

Copied from `gsrandroid/src/main/cpp/gsrecomp/engine/THIRD_PARTY_ATTRIBUTION.md`. The gbarecomp licence (PolyForm Noncommercial 1.0.0) is at `gsrandroid/src/main/cpp/gsrecomp/engine/LICENSE`.

# Third-Party Attribution

Portions of this project are ported from other open-source projects,
used with permission and under their licenses. Facts (struct offsets,
byte signatures, documented hardware/driver behavior) and, where noted,
ported logic are credited below. License texts of permissively-licensed
upstreams are reproduced in their repositories.

## JRickey/gba-recomp

- **Upstream:** https://github.com/JRickey/gba-recomp
- **Author:** Jrickey
- **License:** MIT OR Apache-2.0 (used with the author's permission)

| Our file | Upstream source | What was ported |
|---|---|---|
| `src/gba/gba_m4a.{h,cpp}` | `crates/gba-core/src/mp2k.rs` | MP2K ("m4a") SDK driver detection (SoundMain literal-pool signature + CRC fallback) and the SoundInfo / SoundChannel / WaveData guest-struct offset map. Re-implemented in C++; the live-state dump path is original to this project. |
| `src/runtime/color_lut.{h,cpp}` | `crates/screen/src/{color,profile,lut}.rs` | Screen-color simulation: CIE colorimetry core, measured per-revision panel models, and the BGR555→RGBA8 LUT build. Re-implemented in C++; the present-time RGB888-input apply path is ours. |
| `src/gba/gba_rtc.{h,cpp}` | `crates/gba-core/src/{rtc,hostclock}.rs` | Cartridge GPIO port + S-3511A RTC (serial state machine, BCD date/time, Seiko signature detection) and the civil↔linear host-clock helpers (Hinnant day algorithms, public domain). Re-implemented in C++ (Win32 `GetLocalTime` / POSIX `localtime_r`). |
| `src/gba/audio_shadow.{h,cpp}` | `crates/gba-core/src/shadow.rs` | Engine-agnostic HLE-shadow differential verifier: envelope-correlation self-check vs the canon FIFO stream, probation auto-gain, prove/strike/pause-and-reprobe. Re-implemented in C++. |
| `src/gba/mp2k_shadow.{h,cpp}` | `crates/gba-core/src/mp2k.rs` | MP2K HLE shadow mixer: float voice re-render (envelope mirror, PCM/DPCM sampling, intra-tick gain interpolation, reverb ring) driving the differential verifier. Re-implemented in C++. |

Each ported file carries an attribution header pointing here. Ported
code remains under the upstream's MIT OR Apache-2.0 terms; this notice
and those headers satisfy the attribution requirement.

## mGBA

- **Upstream:** https://github.com/mgba-emu/mgba
- **Author:** Jeffrey Pfau and contributors
- **License:** MPL-2.0

| Our file | Upstream source | What was ported |
|---|---|---|
| `src/runtime/bios_hle.{h,cpp}` | `src/gba/bios.c` | GBA BIOS SWI High-Level Emulation: the Div/Sqrt/ArcTan/ArcTan2 fixed-point routines and stall formulas, the LZ77 / Huffman / run-length / diff-unfilter decompressors, BitUnPack, the (float) affine-matrix builders, and MidiKey2Freq. Re-implemented in C++ against this project's `g_cpu` + bus bridge. HLE is opt-in; LLE (the recompiled real BIOS) remains the default and the correctness oracle. |

Portions of `src/runtime/bios_hle.cpp` are derived from mGBA and remain
subject to the MPL-2.0; the upstream source is at
https://github.com/mgba-emu/mgba (`src/gba/bios.c`), and the derived files are included here in full as source,
satisfying the licence's source-availability requirement.

## dr_flac

- **Upstream:** https://github.com/mackron/dr_libs (`dr_flac.h` v0.13.4)
- **Author:** David Reid
- **License:** public domain (Unlicense) or MIT No Attribution

Listed by gbarecomp 0.6 for its music mod player (`src/runtime/host_local_music.cpp`). That file is not built in the Android app, which contains no dr_flac code.

---

## TinyCC

The on-device C compiler. LGPL 2.1; full text in `gsrandroid/src/main/cpp/tcc/COPYING`. `tccelf.c` has been modified for this port, and the source is included in this repository.

---

## Screen filters

The optional screen filters (Display > Screen filter) are the single-pass filters Golden Sun Recompiled ported
from libretro's glsl-shaders (`engine/src/runtime/screen_filter.cpp`), copied verbatim with their notices
into `gsrandroid/src/main/cpp/gsr/gsr_screen_filter_shaders.inc`:

- **LCD3x** by Gigaherz: public domain.
- **CRT Lottes** by Timothy Lottes: public domain ("Please take and use, change, or whatever.").
- **xBR-lv2** by Hyllian, Copyright (C) 2011-2016: MIT licence. The full notice is in `licenses/xBR-lv2-MIT.txt`
  and is shown in the app's Credits screen.

---

## Licence texts

Full texts are in the `licenses/` folder:

- `gbarecomp-PolyForm-Noncommercial-1.0.0.txt`: the gbarecomp engine, and the terms the whole project is bound by.
- `mGBA-MPL-2.0.txt`: mGBA, for the BIOS routines derived from it.
- `JRickey-gba-recomp-MIT.txt` and `JRickey-gba-recomp-Apache-2.0.txt`: JRickey/gba-recomp (MIT OR Apache-2.0).
- `TinyCC-LGPL-2.1.txt`: TinyCC.
- `xBR-lv2-MIT.txt`: Hyllian's xBR-lv2 screen filter (MIT).
