# Third-party notices

This file collects the licence and attribution notices from the projects this port is built on, copied unchanged from the vendored copies in this repository (version 0.3.1 of Golden Sun Recompiled). The originals stay where they are and must stay with any copy of this project. See `COPYING` for the notice covering the Android port itself. Full licence texts are in the `licenses/` folder.

---

## Golden Sun Recompiled (Shmargus): licence statement

Source: https://github.com/Shmargus/GSRecomp, file `LICENSE`, copied from `gsrandroid/src/main/cpp/gsrecomp/LICENSE`.

```
License not yet selected.

All rights are reserved for original material in this repository -- src/,
tools/, tests/, config/, scripts/ and the documentation -- until the project
owner selects and adds an explicit license.

Third-party components remain under their own licenses, and one of them binds
this whole project:

  The emulation and recompilation engine in gbarecomp/ is copyright Matthew
  Stan and is licensed under the PolyForm Noncommercial License 1.0.0. Its
  text is at gbarecomp/LICENSE and must stay with any copy. It permits use,
  modification and redistribution for noncommercial purposes only. Nothing
  here runs without that engine, so THIS PROJECT IS NONCOMMERCIAL, and any
  license later chosen for the original material cannot escape that while the
  engine is part of the build.

  Portions of the engine are derived from mGBA (MPL-2.0) and from
  JRickey/gba-recomp (MIT OR Apache-2.0); see
  gbarecomp/THIRD_PARTY_ATTRIBUTION.md.

  Dear ImGui (MIT), toml++ (MIT) and SDL2 (zlib) are fetched or installed at
  build time and are not stored in this repository.

No Golden Sun ROM, BIOS, save or extracted asset is in this repository, and
none may be added; that material belongs to Nintendo and Camelot Software
Planning.

See ATTRIBUTION.md for who owns what, and COPYING before publishing,
distributing, or relicensing this project.
```

---

## Golden Sun Recompiled (Shmargus): attribution

Copied from `gsrandroid/src/main/cpp/gsrecomp/ATTRIBUTION.md`.

# What is in this repository, and who owns it

Plain answers first, because the licences here are not all the same and one of
them limits what this project may ever be.

## The short version

| Part | Whose | Licence | In this repo? |
|---|---|---|---|
| The game: Golden Sun, its ROM, BIOS, art, music, text | Nintendo / Camelot Software Planning | Not ours to license | **No, and never** |
| The engine, `gbarecomp/` | Matthew Stan (mstan) | PolyForm Noncommercial 1.0.0 | Yes, an edited copy |
| Code ported into the engine from others | see "Inside the engine" below | MIT / Apache-2.0 / MPL-2.0 | Yes, inside `gbarecomp/src/` |
| Everything else: `src/`, `tools/`, `tests/`, `config/`, `scripts/`, the docs | this project | not yet selected — see `LICENSE` | Yes |
| Dear ImGui, toml++, SDL2 | their authors | MIT, MIT, zlib | No — fetched or installed at build time |

**The practical consequence: this project is noncommercial.** The engine's
licence permits use, modification and redistribution for noncommercial purposes
only. That covers the whole build, because nothing here runs without the engine.

## The game

No ROM, BIOS, save, savestate, screenshot of copyrighted art, or extracted
asset is in this repository, and none may be added. You supply your own legally
obtained ROM and BIOS; both are hash-verified locally.

The recompiled C the build produces is derived from your ROM. It is written to
`generated/` and `local/`, which are not tracked here and must not be
published. `COPYING` is the rule for all of this.

What this repository does hold about the game is our own analysis: symbol
names, addresses, function boundaries and hardware observations in `config/`
and `symbols/` — facts measured about the program, not its content.

## The engine, `gbarecomp/`

Upstream is [`mstan/gbarecomp`](https://github.com/mstan/gbarecomp), copyright
Matthew Stan, licensed **PolyForm Noncommercial 1.0.0** — the full text is at
`gbarecomp/LICENSE` and stays there.

That licence allows what this repository does: use it, change it, and pass it
on, **for any purpose other than a commercial one**. It requires that anyone
who receives a copy also receives the licence, which is why `gbarecomp/LICENSE`
is committed alongside the code rather than referenced.

`gbarecomp/` here is **not upstream**. It is a copy that diverged on
2026-07-17 and now carries 12 commits of our own — about 47,000 added lines
across 130 files, in the PPU, the ARM runtime, the audio shadow and mixer, the
overlay loader, the host window and the recompiler tool. Building against upstream will not reproduce
this project.

Where a fix belongs: anything about ARMv4T, GBA hardware or the recompiler goes
inside `gbarecomp/` and is a candidate to send upstream. Anything about Golden
Sun stays outside it.

### Inside the engine

The engine itself ports code from two other projects, credited in full in
`gbarecomp/THIRD_PARTY_ATTRIBUTION.md`, which is kept intact:

- **[JRickey/gba-recomp](https://github.com/JRickey/gba-recomp)**, MIT OR
  Apache-2.0, used with the author's permission — the MP2K driver detection,
  the screen-colour simulation, the cartridge RTC, and the audio shadow
  verifier.
- **[mGBA](https://github.com/mgba-emu/mgba)**, MPL-2.0 — the BIOS
  high-level-emulation routines in `gbarecomp/src/runtime/bios_hle.{h,cpp}`.
  Those files remain governed by the MPL-2.0; its text is at
  <https://mozilla.org/MPL/2.0/>. mGBA's own source is at the link above.

## Our own code

`src/`, `tools/`, `tests/`, `config/`, `scripts/`, and the documentation are
this project's work. `LICENSE` has not selected a licence for them, so all
rights are reserved by default: people may read this repository, but nobody may
reuse our code until a licence is chosen. Whatever is chosen cannot escape the
engine's noncommercial terms while the engine is part of the build.

## Build-time dependencies, not in this repository

CMake fetches these at configure time, each pinned to an exact version, or uses
a local copy if one is present:

| Library | Use | Licence |
|---|---|---|
| Dear ImGui v1.91.9-docking | the in-game configuration UI | MIT |
| toml++ v3.4.0 | reading `game.toml` and the symbol metadata | MIT |

And these must be installed on the machine: a MinGW-w64 C++ toolchain, SDL2
(zlib licence), OpenGL, Python 3.10+, CMake and Ninja.

## If any of this changes

Selling, sponsoring or otherwise monetising this project is the one thing the
engine's licence forbids, and it would have to be taken up with mstan first.
Adding a dependency means adding a row here. Adding game data means breaking
`COPYING`, which is not a thing to do.

---

## gbarecomp third-party attribution

Copied from `gsrandroid/src/main/cpp/gsrecomp/gbarecomp/THIRD_PARTY_ATTRIBUTION.md`. The gbarecomp licence (PolyForm Noncommercial 1.0.0) is at `gsrandroid/src/main/cpp/gsrecomp/gbarecomp/LICENSE`.

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

---

## TinyCC

The on-device C compiler. LGPL 2.1; full text in `gsrandroid/src/main/cpp/tcc/COPYING`. `tccelf.c` has been modified for this port, and the source is included in this repository.

---

## Licence texts

Full texts are in the `licenses/` folder:

- `gbarecomp-PolyForm-Noncommercial-1.0.0.txt`: the gbarecomp engine, and the terms the whole project is bound by.
- `mGBA-MPL-2.0.txt`: mGBA, for the BIOS routines derived from it.
- `JRickey-gba-recomp-MIT.txt` and `JRickey-gba-recomp-Apache-2.0.txt`: JRickey/gba-recomp (MIT OR Apache-2.0).
- `TinyCC-LGPL-2.1.txt`: TinyCC.
