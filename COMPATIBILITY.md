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
| ... still reaching new state at frame 900 | **44** |
| ... drew once and froze | **22** |
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
| 3-D Tetris (USA) | 1024 | 895 | yes | yes | yes | yes | advancing | - |  |
| Galactic Pinball (Japan, USA) (En) | 1024 | 109 | yes | yes | yes | wip | frozen | no | Renders since the inline-call-argument fix; world attributes now written, image still garbled |
| Golf (USA) | 2048 | 505 | yes | yes | yes | yes | advancing | wip | In game (2026-10-21): hole overview, then on the course taking shots |
| Insmouth no Yakata (Japan) | 1024 | 274 | yes | yes | yes | yes | advancing | wip | In game (2026-10-21): first-person corridor with HUD, level timer counting down, Start/A input only |
| Jack Bros. (USA) | 1024 | 273 | yes | yes | - | no | blank | - |  |
| Jack Bros. no Meiro de Hiihoo! (Japan) | 1024 | 262 | yes | yes | - | no | blank | - |  |
| Mario Clash (Japan, USA) (En) | 1024 | 846 | yes | yes | yes | wip | advancing | no | Boots via the generic driver (no hand-written glue); programs VIP + world attributes, advancing frames |
| Mario's Tennis (Japan, USA) (En) | 512 | 254 | yes | yes | yes | wip | advancing | wip | Demo mode runs; title/court/sprites render; gameplay upper-BG corruption (block rendering) |
| Nester's Funky Bowling (USA) | 2048 | 341 | yes | yes | yes | yes | frozen | - |  |
| Panic Bomber (USA) | 512 | 226 | yes | yes | yes | yes | frozen | - |  |
| Red Alarm (Japan) | 1024 | 392 | yes | yes | yes | yes | advancing | - |  |
| Red Alarm (USA) | 1024 | 392 | yes | yes | yes | wip | advancing | no | Boots to title; wireframe framebuffer populated, pixel layout WIP; 3D scene does not animate |
| SD Gundam - Dimension War (Japan) | 1024 | 335 | yes | yes | yes | yes | frozen | - |  |
| Space Invaders - Virtual Collection (Japan) | 512 | 145 | yes | yes | yes | yes | frozen | - |  |
| Space Squash (Japan) | 512 | 521 | yes | yes | yes | yes | advancing | wip | In game (2026-10-21): auto-pause prompt, GET READY, a scored rally, the continue screen, with Start/A input only |
| T&E Virtual Golf (Japan) | 2048 | 495 | yes | yes | yes | yes | advancing | wip | In game (2026-10-21): on the course mid-hole, club select and swing meter, shots taken |
| Teleroboxer (Japan, USA) (En) | 1024 | 757 | yes | yes | yes | yes | advancing | - |  |
| Tobidase! Panibon (Japan) | 512 | 224 | yes | yes | yes | yes | frozen | - |  |
| Vertical Force (Japan) | 1024 | 350 | yes | yes | yes | yes | advancing | - |  |
| Vertical Force (USA) | 1024 | 347 | yes | yes | yes | yes | advancing | - |  |
| Virtual Bowling (Japan) | 1024 | 352 | yes | yes | yes | yes | frozen | - |  |
| Virtual Boy Wario Land (Japan, USA) (En) | 2048 | 3238 | yes | yes | yes | yes | frozen | - |  |
| Virtual Fishing (Japan) | 1024 | 232 | yes | yes | yes | yes | frozen | - |  |
| Virtual Lab (Japan) | 1024 | 327 | yes | yes | yes | yes | advancing | - |  |
| Virtual League Baseball (USA) | 1024 | 1998 | yes | yes | yes | yes | advancing | no | Reaches pitcher select and batting order (2026-10-21) |
| Virtual Pro Yakyuu '95 (Japan) | 1024 | 421 | yes | yes | yes | yes | advancing | no | Reaches pitcher select and batting order (2026-10-21) |
| V-Tetris (Japan) | 512 | 372 | yes | yes | yes | yes | frozen | - |  |
| Waterworld (USA) | 2048 | 138 | yes | yes | yes | yes | advancing | - |  |

## Proto / Demo (3)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Motion | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|--------|----------|-------|
| Bound High (Japan) (En) (Proto) | 2048 | 604 | yes | yes | yes | yes | advancing | wip | In game (2026-10-21): World 1 with score; still flips blank in some sessions |
| Niko-chan Battle (Japan) (Proto) | 1024 | 407 | yes | yes | yes | yes | frozen | - |  |
| Space Pinball (Japan) (En) (Proto) | 2048 | 103 | yes | yes | yes | yes | frozen | - |  |

## Homebrew (45)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Motion | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|--------|----------|-------|
| 3-D BattleSnake (World) (Aftermarket) (Homebrew) | 1024 | 29 | yes | yes | yes | yes | frozen | - |  |
| 3D Crosswords (World) (Emulator Version) (Aftermarket) (Homebrew) | 2048 | 34 | yes | yes | yes | yes | advancing | - |  |
| 3D Crosswords (World) (Hardware Version) (Aftermarket) (Homebrew) | 512 | 34 | yes | yes | yes | yes | advancing | - |  |
| Bad Apple (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 16384 | 18 | yes | yes | yes | yes | advancing | - |  |
| Bad Apple (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 16384 | 18 | yes | yes | yes | yes | advancing | - |  |
| Ballface (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 104 | yes | yes | yes | yes | frozen | - |  |
| BLOX (World) (v1.0) (Aftermarket) (Homebrew) | 1024 | 82 | yes | yes | yes | yes | advancing | - |  |
| BLOX (World) (v1.1) (Aftermarket) (Homebrew) | 1024 | 109 | yes | yes | yes | yes | advancing | - |  |
| BLOX 2 (World) (En,Fr,De,Es,It,Sv,Fi,Cs,Sl) (Aftermarket) (Homebrew) | 2048 | 137 | yes | yes | yes | yes | frozen | - |  |
| Capitan Sevilla 2 (World) (Es) (Aftermarket) (Homebrew) | 1024 | 55 | yes | yes | yes | yes | advancing | - |  |
| Capitan Sevilla 3D (World) (En,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 775 | yes | yes | - | no | blank | - |  |
| Elevated Speed (World) (High Resolution Version) (Aftermarket) (Homebrew) | 4096 | 100 | yes | yes | yes | yes | advancing | - |  |
| Elevated Speed (World) (Low Resolution Version) (Aftermarket) (Homebrew) | 2048 | 100 | yes | yes | yes | yes | advancing | - |  |
| Fishbone (World) (Aftermarket) (Homebrew) | 2048 | 522 | yes | yes | yes | yes | frozen | - |  |
| Fishbone (World) (Demo 1) (Aftermarket) (Homebrew) | 2048 | 48 | yes | yes | yes | yes | advancing | - |  |
| Fishbone (World) (Demo 2) (Aftermarket) (Homebrew) | 2048 | 111 | yes | yes | yes | yes | advancing | - |  |
| Fishbone (World) (Demo 3) (Aftermarket) (Homebrew) | 2048 | 120 | yes | yes | yes | yes | advancing | - |  |
| Formula V (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 4096 | 810 | yes | yes | - | no | blank | - |  |
| Formula V (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 4096 | 711 | yes | yes | yes | yes | frozen | - |  |
| Hamburgers En Route to Switzerland (World) (Aftermarket) (Homebrew) | 512 | 50 | yes | yes | yes | yes | advancing | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 28 | yes | yes | yes | yes | advancing | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 2) (Aftermarket) (Homebrew) | 512 | 36 | yes | yes | yes | yes | advancing | - |  |
| Hyper Fighting (World) (Aftermarket) (Homebrew) | 4096 | 658 | yes | yes | yes | yes | advancing | - |  |
| Hyper Fighting (World) (Beta) (Aftermarket) (Homebrew) | 2048 | 192 | yes | yes | yes | yes | frozen | - |  |
| Insecticide (World) (Aftermarket) (Homebrew) | 2048 | 68 | yes | yes | yes | yes | advancing | - |  |
| Mario Combat (World) (Aftermarket) (Homebrew) | 2048 | 508 | yes | yes | yes | yes | frozen | - |  |
| Mario Kart - Virtual Cup (World) (Aftermarket) (Homebrew) | 1024 | 50 | yes | yes | yes | yes | advancing | - |  |
| Mario VB (World) (Aftermarket) (Homebrew) | 512 | 543 | yes | yes | yes | yes | advancing | - |  |
| Null Pointer Mockup (World) (Demo) (Aftermarket) (Homebrew) | 512 | 1724 | yes | yes | - | no | blank | - |  |
| Red Square (World) (Aftermarket) (Homebrew) | 512 | 54 | yes | yes | yes | yes | advancing | - |  |
| Snatcher (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 1063 | yes | yes | yes | yes | frozen | - |  |
| Snowball Wars (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 407 | yes | yes | - | no | blank (hw) | - | Blank on real hardware too -- not ours |
| Snowball Wars (World) (Demo 2) (Aftermarket) (Homebrew) | 1024 | 444 | yes | yes | - | no | blank (hw) | - | Blank on real hardware too -- not ours |
| Snowball Wars (World) (Demo 3) (Aftermarket) (Homebrew) | 1024 | 448 | yes | yes | - | no | blank (hw) | - | Blank on real hardware too -- not ours |
| Soviet Union 2010 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 88 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2010 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 131 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2011 (World) (Beta 1) (Aftermarket) (Homebrew) | 2048 | 95 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2011 (World) (Beta 2) (Aftermarket) (Homebrew) | 2048 | 99 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2011 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 119 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2011 (World) (v1.1) (Aftermarket) (Homebrew) | 2048 | 123 | yes | yes | yes | yes | advancing | - |  |
| Soviet Union 2011 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 131 | yes | yes | yes | yes | advancing | - |  |
| SPONG (World) (En,Fr,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 585 | yes | yes | - | no | blank | - |  |
| StarFox (World) (Demo) (Aftermarket) (Homebrew) | 128 | 224 | yes | yes | yes | yes | frozen | - |  |
| Tron VB (World) (Demo) (Aftermarket) (Homebrew) | 64 | 33 | yes | yes | yes | yes | frozen | - |  |
| VUEngine Platformer (World) (En,Fr,De,Es-ES) (Demo) (Aftermarket) (Homebrew) | 2048 | 684 | yes | yes | - | no | blank | - |  |

