# Compatibility

Living status of every ROM in the library. **Recompiles** = v810recomp emits C; **Compiles** = that C builds clean (MSVC `/Zs`) - both columns come from `scripts/sweep.ps1` (see [STATUS.md](STATUS.md)). **Boots / Renders / Motion** come from the headless survey (`scripts/render_survey.ps1`); **Playable** and the notes are hand-tracked. **Motion** is `advancing` when the frame still changes between samples at 300/600/900, `frozen` when the game drew once and stopped.

Status values: `yes` | `wip` (partial) | `no` | `-` (untested).

## Where this has got to

| | |
|---|---|
| ROMs in the library | **76** |
| Recompile to C | **76 / 76** |
| That C compiles clean | **76 / 76** |
| Put something on screen | **66 / 76** |
| ... still reaching new state at frame 900 | **36** |
| ... drew once and froze | **30** |
| Blank | **10** (3 blank on real hardware too, so **7** are ours) |

Three ROMs draw nothing on the reference emulator either -- verified on
BizHawk's Mednafen VB core at 1800 frames with the same buttons held, where
they never load a single character byte, let alone draw one. So the
achievable denominator is **73**, not 76.

Every one of these numbers is regenerated from a run, not hand-maintained:
`scripts/sweep.ps1` -> `scripts/build_all.ps1` -> `scripts/render_survey.ps1` -> this file.
See [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) to reproduce it.

## Retail (28)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Motion | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|--------|----------|-------|
| 3-D Tetris (USA) | 1024 | 818 | yes | yes | yes | yes | frozen | - |  |
| Galactic Pinball (Japan, USA) (En) | 1024 | 55 | yes | yes | yes | wip | frozen | no | Renders since the inline-call-argument fix; world attributes now written, image still garbled |
| Golf (USA) | 2048 | 99 | yes | yes | yes | yes | frozen | - |  |
| Insmouth no Yakata (Japan) | 1024 | 238 | yes | yes | yes | yes | frozen | - |  |
| Jack Bros. (USA) | 1024 | 78 | yes | yes | - | no | blank | - |  |
| Jack Bros. no Meiro de Hiihoo! (Japan) | 1024 | 74 | yes | yes | - | no | blank | - |  |
| Mario Clash (Japan, USA) (En) | 1024 | 833 | yes | yes | yes | wip | advancing | no | Boots via the generic driver (no hand-written glue); programs VIP + world attributes, advancing frames |
| Mario's Tennis (Japan, USA) (En) | 512 | 243 | yes | yes | yes | wip | advancing | wip | Demo mode runs; title/court/sprites render; gameplay upper-BG corruption (block rendering) |
| Nester's Funky Bowling (USA) | 2048 | 338 | yes | yes | yes | yes | frozen | - |  |
| Panic Bomber (USA) | 512 | 216 | yes | yes | yes | yes | frozen | - |  |
| Red Alarm (Japan) | 1024 | 382 | yes | yes | yes | yes | frozen | - |  |
| Red Alarm (USA) | 1024 | 382 | yes | yes | yes | wip | frozen | no | Boots to title; wireframe framebuffer populated, pixel layout WIP; 3D scene does not animate |
| SD Gundam - Dimension War (Japan) | 1024 | 324 | yes | yes | yes | yes | frozen | - |  |
| Space Invaders - Virtual Collection (Japan) | 512 | 71 | yes | yes | yes | yes | frozen | - |  |
| Space Squash (Japan) | 512 | 185 | yes | yes | yes | yes | frozen | - |  |
| T&E Virtual Golf (Japan) | 2048 | 100 | yes | yes | yes | yes | frozen | - |  |
| Teleroboxer (Japan, USA) (En) | 1024 | 748 | yes | yes | yes | yes | frozen | - |  |
| Tobidase! Panibon (Japan) | 512 | 216 | yes | yes | yes | yes | frozen | - |  |
| Vertical Force (Japan) | 1024 | 125 | yes | yes | yes | yes | advancing | - |  |
| Vertical Force (USA) | 1024 | 125 | yes | yes | yes | yes | advancing | - |  |
| Virtual Bowling (Japan) | 1024 | 337 | yes | yes | yes | yes | frozen | - |  |
| Virtual Boy Wario Land (Japan, USA) (En) | 2048 | 3151 | yes | yes | yes | yes | frozen | - |  |
| Virtual Fishing (Japan) | 1024 | 92 | yes | yes | yes | yes | frozen | - |  |
| Virtual Lab (Japan) | 1024 | 238 | yes | yes | yes | yes | advancing | - |  |
| Virtual League Baseball (USA) | 1024 | 121 | yes | yes | yes | yes | advancing | - |  |
| Virtual Pro Yakyuu '95 (Japan) | 1024 | 111 | yes | yes | yes | yes | advancing | - |  |
| V-Tetris (Japan) | 512 | 365 | yes | yes | yes | yes | frozen | - |  |
| Waterworld (USA) | 2048 | 141 | yes | yes | yes | yes | advancing | - |  |

## Proto / Demo (3)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Motion | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|--------|----------|-------|
| Bound High (Japan) (En) (Proto) | 2048 | 471 | yes | yes | yes | yes | advancing | - |  |
| Niko-chan Battle (Japan) (Proto) | 1024 | 395 | yes | yes | yes | yes | frozen | - |  |
| Space Pinball (Japan) (En) (Proto) | 2048 | 58 | yes | yes | yes | yes | frozen | - |  |

## Homebrew (45)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Motion | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|--------|----------|-------|
| 3-D BattleSnake (World) (Aftermarket) (Homebrew) | 1024 | 29 | yes | yes | yes | yes | frozen | - |  |
| 3D Crosswords (World) (Emulator Version) (Aftermarket) (Homebrew) | 2048 | 34 | yes | yes | yes | yes | advancing | - |  |
| 3D Crosswords (World) (Hardware Version) (Aftermarket) (Homebrew) | 512 | 34 | yes | yes | yes | yes | advancing | - |  |
| Bad Apple (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 16384 | 18 | yes | yes | yes | yes | advancing | - |  |
| Bad Apple (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 16384 | 18 | yes | yes | yes | yes | advancing | - |  |
| Ballface (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 104 | yes | yes | yes | yes | frozen | - |  |
| BLOX (World) (v1.0) (Aftermarket) (Homebrew) | 1024 | 85 | yes | yes | yes | yes | advancing | - |  |
| BLOX (World) (v1.1) (Aftermarket) (Homebrew) | 1024 | 109 | yes | yes | yes | yes | advancing | - |  |
| BLOX 2 (World) (En,Fr,De,Es,It,Sv,Fi,Cs,Sl) (Aftermarket) (Homebrew) | 2048 | 137 | yes | yes | yes | yes | frozen | - |  |
| Capitan Sevilla 2 (World) (Es) (Aftermarket) (Homebrew) | 1024 | 52 | yes | yes | yes | yes | advancing | - |  |
| Capitan Sevilla 3D (World) (En,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 238 | yes | yes | - | no | blank | - |  |
| Elevated Speed (World) (High Resolution Version) (Aftermarket) (Homebrew) | 4096 | 66 | yes | yes | yes | yes | advancing | - |  |
| Elevated Speed (World) (Low Resolution Version) (Aftermarket) (Homebrew) | 2048 | 66 | yes | yes | yes | yes | advancing | - |  |
| Fishbone (World) (Aftermarket) (Homebrew) | 2048 | 22 | yes | yes | yes | yes | frozen | - |  |
| Fishbone (World) (Demo 1) (Aftermarket) (Homebrew) | 2048 | 48 | yes | yes | yes | yes | advancing | - |  |
| Fishbone (World) (Demo 2) (Aftermarket) (Homebrew) | 2048 | 111 | yes | yes | yes | yes | advancing | - |  |
| Fishbone (World) (Demo 3) (Aftermarket) (Homebrew) | 2048 | 120 | yes | yes | yes | yes | advancing | - |  |
| Formula V (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 4096 | 183 | yes | yes | - | no | blank | - |  |
| Formula V (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 4096 | 252 | yes | yes | yes | yes | frozen | - |  |
| Hamburgers En Route to Switzerland (World) (Aftermarket) (Homebrew) | 512 | 48 | yes | yes | yes | yes | advancing | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 28 | yes | yes | yes | yes | advancing | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 2) (Aftermarket) (Homebrew) | 512 | 36 | yes | yes | yes | yes | advancing | - |  |
| Hyper Fighting (World) (Aftermarket) (Homebrew) | 4096 | 16 | yes | yes | yes | yes | advancing | - |  |
| Hyper Fighting (World) (Beta) (Aftermarket) (Homebrew) | 2048 | 17 | yes | yes | yes | yes | frozen | - |  |
| Insecticide (World) (Aftermarket) (Homebrew) | 2048 | 68 | yes | yes | yes | yes | advancing | - |  |
| Mario Combat (World) (Aftermarket) (Homebrew) | 2048 | 19 | yes | yes | yes | yes | frozen | - |  |
| Mario Kart - Virtual Cup (World) (Aftermarket) (Homebrew) | 1024 | 49 | yes | yes | yes | yes | advancing | - |  |
| Mario VB (World) (Aftermarket) (Homebrew) | 512 | 538 | yes | yes | yes | yes | advancing | - |  |
| Null Pointer Mockup (World) (Demo) (Aftermarket) (Homebrew) | 512 | 1153 | yes | yes | - | no | blank | - |  |
| Red Square (World) (Aftermarket) (Homebrew) | 512 | 54 | yes | yes | yes | yes | advancing | - |  |
| Snatcher (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 19 | yes | yes | yes | yes | frozen | - |  |
| Snowball Wars (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 406 | yes | yes | - | no | blank (hw) | - | Blank on real hardware too -- not ours |
| Snowball Wars (World) (Demo 2) (Aftermarket) (Homebrew) | 1024 | 443 | yes | yes | - | no | blank (hw) | - | Blank on real hardware too -- not ours |
| Snowball Wars (World) (Demo 3) (Aftermarket) (Homebrew) | 1024 | 447 | yes | yes | - | no | blank (hw) | - | Blank on real hardware too -- not ours |
| Soviet Union 2010 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 88 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2010 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 131 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2011 (World) (Beta 1) (Aftermarket) (Homebrew) | 2048 | 95 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2011 (World) (Beta 2) (Aftermarket) (Homebrew) | 2048 | 98 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2011 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 119 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2011 (World) (v1.1) (Aftermarket) (Homebrew) | 2048 | 123 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2011 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 131 | yes | yes | yes | yes | advancing | - |  |
| SPONG (World) (En,Fr,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 173 | yes | yes | - | no | blank | - |  |
| StarFox (World) (Demo) (Aftermarket) (Homebrew) | 128 | 17 | yes | yes | yes | yes | frozen | - |  |
| Tron VB (World) (Demo) (Aftermarket) (Homebrew) | 64 | 33 | yes | yes | yes | yes | frozen | - |  |
| VUEngine Platformer (World) (En,Fr,De,Es-ES) (Demo) (Aftermarket) (Homebrew) | 2048 | 200 | yes | yes | - | no | blank | - |  |

