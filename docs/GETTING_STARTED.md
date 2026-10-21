# Getting started

From "I have a Virtual Boy ROM" to "I have recompiled it, run it, and can
reproduce the numbers in this repository".

Nothing here needs a ROM until [step 3](#3-add-your-roms) — the library, the
recompiler and the conformance suite all build and pass on a clean checkout.

---

## 0. What you need

| | |
|---|---|
| **CMake** | 3.15+ |
| **A C/C++ compiler** | MSVC (Visual Studio 2022 Build Tools) is what this is developed against; the library and tool are portable C |
| **SDL2** | for the windowed player. Optional — without it, define `VBRECOMP_NO_SDL` and use headless mode |
| **Python 3** | conformance suite, gallery generator |
| **PowerShell 5.1+** | the corpus scripts (`.ps1`) |
| **ROMs** | your own dumps of cartridges you own. None are included |

SDL2 via vcpkg is the assumed layout:

```powershell
vcpkg install sdl2:x64-windows
```

Optional, only for the hardware-comparison scripts in `scripts/reference/`:
[BizHawk](https://tasvideos.org/BizHawk) with its Mednafen Virtual Boy core.

---

## 1. Build the library and the recompiler

```powershell
cmake -B build -T host=x64 -DSDL2_DIR=C:/vcpkg/installed/x64-windows/share/sdl2
cmake --build build --config Release --target v810recomp
```

That produces `build/Release/v810recomp.exe` — the static recompiler — and the
`vbrecomp` runtime library it links against.

No ROMs yet, so the per-game targets are skipped; CMake prints
`skipping <target>: ... not generated yet` for each. That is expected.

## 2. Check the build is sane

```powershell
python tools/conformance.py
```

77 fixed V810 encodings with hand-written expected results — flags, carries,
sign-extension, the `andi ..., r0` test idiom. It needs no ROM, so it is the one
check anybody can run on a clean checkout, and it is the first thing to run after
touching the emitter.

Expected: `77/77 cases pass`.

## 3. Add your ROMs

```
roms/
  Vertical Force (USA).vb
  ...
```

`roms/` is gitignored. The corpus scripts key off the filenames, and
`COMPATIBILITY.md` uses No-Intro style names.

## 4. Recompile one game

```powershell
build/Release/v810recomp.exe "roms/Vertical Force (USA).vb" corpus/Vertical_Force_USA
```

This writes `corpus/Vertical_Force_USA/recomp_funcs.{c,h}` — one C function per
discovered V810 routine, each line preceded by its original address and
disassembly. The tool prints the header, the interrupt vectors it decoded and how
many functions it found.

Some games need profile-guided entry points, because a jump target the game
computes at run time cannot be found statically. Those live in
`games/hints/<slug>.hints.txt` and are committed:

```powershell
build/Release/v810recomp.exe "roms/Jack Bros. (USA).vb" corpus/Jack_Bros_USA `
    --hints games/hints/Jack_Bros_USA.hints.txt
```

`scripts/discover.ps1 -Slug <slug>` is what harvests them: it runs the game, reads
the addresses the runtime counted as unreachable, feeds them back as hints, and
repeats until nothing new appears.

> The generated C is a machine translation of copyrighted game code — a derived
> work of your ROM. It is gitignored and must not be redistributed. See
> [THIRD_PARTY.md](../THIRD_PARTY.md).

## 5. Build and run it

Point a second build tree at your corpus directory. Every subdirectory with a
`recomp_funcs.c` becomes a `corpus_<slug>` executable:

```powershell
cmake -S . -B build_all -T host=x64 `
    -DSDL2_DIR=C:/vcpkg/installed/x64-windows/share/sdl2 `
    -DVBRECOMP_CORPUS_DIR=$PWD/corpus -DVBRECOMP_BUILD_TESTS=OFF
cmake --build build_all --config Release --target corpus_Vertical_Force_USA
build_all/games/Release/corpus_Vertical_Force_USA.exe "roms/Vertical Force (USA).vb"
```

The ROM path is passed at run time — the recompiled code holds the game's
*instructions*, and still reads its data out of the ROM image.

There is no game-specific code in that executable: it is
[`games/driver/main.c`](../games/driver/main.c) plus the generated file. The
driver registers the interrupt handlers the recompiler discovered, runs the reset
vector, and renders whatever the game puts in VRAM.

### Headless, with screenshots

The quick way, with the same flag names as the other recomp toolkits:

```powershell
build_all/games/Release/corpus_Vertical_Force_USA.exe --headless --frames 900 "roms/Vertical Force (USA).vb"
build_all/games/Release/corpus_Vertical_Force_USA.exe --record vf.mp4 --frames 600 "roms/Vertical Force (USA).vb"
```

`--record` implies `--headless` and needs `ffmpeg` on `PATH`; it writes 384x224
at 50.27 fps. The flags just set the environment variables below, which give
finer control:

```powershell
$env:VBRECOMP_HEADLESS      = "1"
$env:VBRECOMP_HEADLESS_FRAMES = "900"
$env:VBRECOMP_PRESS         = "1000,0004"   # Start, then A, one per burst
$env:VBRECOMP_SHOT_PATH     = "shot"
$env:VBRECOMP_SHOT_EVERY    = "300"    # also dump every 300 frames
build_all/games/Release/corpus_Vertical_Force_USA.exe "roms/Vertical Force (USA).vb"
```

`VBRECOMP_PRESS` matters more than it looks: most of the retail set gates its
health-warning screen on a button press and waits forever without one. Press one
button per burst: holding A, B, L, R, Start and Select together is a soft-reset
combo, and the game restarts to its warning screen every time.

A "frame" here is `VBRECOMP_FRAME_TICKS` interrupt checks (20,000 by default),
not a hardware frame — roughly 2.25 of these to one hardware frame. See the
pacing note in [RENDER_TRIAGE.md](RENDER_TRIAGE.md) before drawing conclusions
from a change that alters function granularity.

### Useful environment variables

| Variable | Does |
|---|---|
| `VBRECOMP_HEADLESS`, `VBRECOMP_HEADLESS_FRAMES` | run without a window, for N frames |
| `VBRECOMP_SHOT_PATH`, `VBRECOMP_SHOT_EVERY` | write PNG screenshots |
| `VBRECOMP_PRESS` | hex button masks, comma-separated, auto-pressed one per burst in turn |
| `VBRECOMP_HEARTBEAT` | periodic VIP/CPU state, hot reads, VRAM write counts, parked transfer targets |
| `VBRECOMP_FRAME_TICKS` | interrupt checks per rendered frame |
| `VBRECOMP_COUNT_FUNC=<hex>` | count entries to one function, cumulative |
| `VBRECOMP_WATCH` / `VBRECOMP_WWATCH=<hex>` | who reads / writes an address, with values |
| `VBRECOMP_WRAM_DUMP`, `VBRECOMP_WRAM_FRAME` | dump all 64 KB of WRAM at a frame, to diff against hardware |

---

## 6. Reproduce the whole corpus

In order. The first two are long — this is a sequential build of every ROM you
have, by design (never build these in parallel).

```powershell
# 1. recompile every ROM in roms/ and syntax-check the C     -> STATUS.md, corpus/results.json
./scripts/sweep.ps1 -Tool build/Release/v810recomp.exe

# 2. build every corpus target and boot each one headless    -> build_all/, corpus/_shots/
./scripts/build_all.ps1

# 3. boot every game for 900 frames and classify what it drew -> corpus/render_survey.json,
./scripts/render_survey.ps1                                  #    corpus/_timeline/<slug>/

# 4. regenerate the docs that are derived from those runs
./scripts/gen_compat.ps1                # -> COMPATIBILITY.md
python scripts/gen_gallery.py           # -> corpus/gallery.html
```

`sweep.ps1` defaults to the **Debug** tool path, so pass `-Tool` as above if you
only built Release.

### Checking a recompiler change quickly

The full loop above takes about an hour. This does not:

```powershell
./scripts/regress.ps1              # a fixed panel of games, diffed against a saved baseline
./scripts/regress.ps1 -Panel all   # every game already built, no rebuild
./scripts/regress.ps1 -Save        # record current behaviour as the baseline
```

It classifies each game exactly as `render_survey.ps1` does — `advancing`,
`frozen`, `blank`, `dead` — and prints only what moved.

### Comparing against real hardware

`scripts/reference/` drives BizHawk's Mednafen VB core as an oracle. The one
command that matters:

```powershell
./scripts/reference/compare.ps1 -Slug Jack_Bros_USA -RefFrame 300 -OurFrame 700
```

It dumps WRAM from hardware and from us, then prints the non-zero counts, the
256-byte pages hardware has content in and we do not, and the first differing
words. Read `scripts/reference/README.md` first — it documents the memory-domain
trap that made a whole line of enquiry wrong (`DRAM` is VIP memory, **not** the
CPU's work RAM; you want `WRAM`).

---

## 7. Rebuilding the legacy per-game targets

Five games predate the generic driver and have hand-written boot glue in
`games/<name>/src/main.c`. Their generated code is not committed, so those
targets are skipped until you regenerate it:

```powershell
build/Release/v810recomp.exe "roms/Mario Clash (Japan, USA) (En).vb" games/marioclash/generated
cmake -B build && cmake --build build --config Release --target marioclash
```

New work should use the generic driver instead — that is the point of the
corpus path above.

---

## Where to look next

| | |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | the Virtual Boy itself, and how the runtime models it |
| [RECOMPILER.md](RECOMPILER.md) | how `v810recomp` translates V810 to C |
| [RENDER_TRIAGE.md](RENDER_TRIAGE.md) | every measured finding, including the ones that failed |
| [COMPATIBILITY.md](../COMPATIBILITY.md) | per-game status |
