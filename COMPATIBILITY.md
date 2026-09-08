# Compatibility

Living status of every ROM in the library. **Recompiles** = v810recomp emits C; **Compiles** = that C builds clean (MSVC `/Zs`) - both columns come from `scripts/sweep.ps1` (see [STATUS.md](STATUS.md)). **Boots / Renders / Playable** are hand-tracked runtime status.

Status values: `yes` | `wip` (partial) | `no` | `-` (untested).

**76 ROMs - 76 recompile, 76 compile clean.**

## Retail (28)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| 3-D Tetris (USA) | 1024 | 818 | yes | yes | - | no | - |  |
| Galactic Pinball (Japan, USA) (En) | 1024 | 55 | yes | yes | yes | wip | no | First pixels on screen; boots through 4-state dispatch, settles ~frame 527 |
| Golf (USA) | 2048 | 99 | yes | yes | yes | yes | - |  |
| Insmouth no Yakata (Japan) | 1024 | 238 | yes | yes | yes | yes | - |  |
| Jack Bros. (USA) | 1024 | 78 | yes | yes | - | no | - |  |
| Jack Bros. no Meiro de Hiihoo! (Japan) | 1024 | 74 | yes | yes | - | no | - |  |
| Mario Clash (Japan, USA) (En) | 1024 | 833 | yes | yes | yes | wip | no | Boots via the generic driver (no hand-written glue); programs VIP + world attributes, advancing frames |
| Mario's Tennis (Japan, USA) (En) | 512 | 243 | yes | yes | yes | wip | wip | Demo mode runs; title/court/sprites render; gameplay upper-BG corruption (block rendering) |
| Nester's Funky Bowling (USA) | 2048 | 338 | yes | yes | yes | yes | - |  |
| Panic Bomber (USA) | 512 | 216 | yes | yes | yes | yes | - |  |
| Red Alarm (Japan) | 1024 | 382 | yes | yes | yes | yes | - |  |
| Red Alarm (USA) | 1024 | 382 | yes | yes | yes | wip | no | Boots to title; wireframe framebuffer populated, pixel layout WIP; 3D scene does not animate |
| SD Gundam - Dimension War (Japan) | 1024 | 324 | yes | yes | yes | yes | - |  |
| Space Invaders - Virtual Collection (Japan) | 512 | 71 | yes | yes | - | no | - |  |
| Space Squash (Japan) | 512 | 185 | yes | yes | yes | yes | - |  |
| T&E Virtual Golf (Japan) | 2048 | 100 | yes | yes | yes | yes | - |  |
| Teleroboxer (Japan, USA) (En) | 1024 | 748 | yes | yes | yes | yes | - |  |
| Tobidase! Panibon (Japan) | 512 | 216 | yes | yes | yes | yes | - |  |
| Vertical Force (Japan) | 1024 | 125 | yes | yes | - | no | - |  |
| Vertical Force (USA) | 1024 | 125 | yes | yes | - | no | - |  |
| Virtual Bowling (Japan) | 1024 | 337 | yes | yes | - | no | - |  |
| Virtual Boy Wario Land (Japan, USA) (En) | 2048 | 3151 | yes | yes | yes | yes | - |  |
| Virtual Fishing (Japan) | 1024 | 92 | yes | yes | - | no | - |  |
| Virtual Lab (Japan) | 1024 | 238 | yes | yes | - | no | - |  |
| Virtual League Baseball (USA) | 1024 | 121 | yes | yes | yes | yes | - |  |
| Virtual Pro Yakyuu '95 (Japan) | 1024 | 111 | yes | yes | yes | yes | - |  |
| V-Tetris (Japan) | 512 | 365 | yes | yes | yes | yes | - |  |
| Waterworld (USA) | 2048 | 141 | yes | yes | yes | yes | - |  |

## Proto / Demo (3)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| Bound High (Japan) (En) (Proto) | 2048 | 471 | yes | yes | - | no | - |  |
| Niko-chan Battle (Japan) (Proto) | 1024 | 395 | yes | yes | yes | yes | - |  |
| Space Pinball (Japan) (En) (Proto) | 2048 | 58 | yes | yes | - | no | - |  |

## Homebrew (45)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| 3-D BattleSnake (World) (Aftermarket) (Homebrew) | 1024 | 29 | yes | yes | yes | yes | - |  |
| 3D Crosswords (World) (Emulator Version) (Aftermarket) (Homebrew) | 2048 | 34 | yes | yes | yes | yes | - |  |
| 3D Crosswords (World) (Hardware Version) (Aftermarket) (Homebrew) | 512 | 34 | yes | yes | yes | yes | - |  |
| Bad Apple (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 16384 | 18 | yes | yes | yes | yes | - |  |
| Bad Apple (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 16384 | 18 | yes | yes | yes | yes | - |  |
| Ballface (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 104 | yes | yes | yes | yes | - |  |
| BLOX (World) (v1.0) (Aftermarket) (Homebrew) | 1024 | 85 | yes | yes | yes | yes | - |  |
| BLOX (World) (v1.1) (Aftermarket) (Homebrew) | 1024 | 109 | yes | yes | yes | yes | - |  |
| BLOX 2 (World) (En,Fr,De,Es,It,Sv,Fi,Cs,Sl) (Aftermarket) (Homebrew) | 2048 | 137 | yes | yes | yes | yes | - |  |
| Capitan Sevilla 2 (World) (Es) (Aftermarket) (Homebrew) | 1024 | 52 | yes | yes | yes | yes | - |  |
| Capitan Sevilla 3D (World) (En,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 238 | yes | yes | - | no | - |  |
| Elevated Speed (World) (High Resolution Version) (Aftermarket) (Homebrew) | 4096 | 66 | yes | yes | - | no | - |  |
| Elevated Speed (World) (Low Resolution Version) (Aftermarket) (Homebrew) | 2048 | 66 | yes | yes | - | no | - |  |
| Fishbone (World) (Aftermarket) (Homebrew) | 2048 | 22 | yes | yes | - | no | - |  |
| Fishbone (World) (Demo 1) (Aftermarket) (Homebrew) | 2048 | 48 | yes | yes | yes | yes | - |  |
| Fishbone (World) (Demo 2) (Aftermarket) (Homebrew) | 2048 | 111 | yes | yes | yes | yes | - |  |
| Fishbone (World) (Demo 3) (Aftermarket) (Homebrew) | 2048 | 120 | yes | yes | yes | yes | - |  |
| Formula V (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 4096 | 183 | yes | yes | - | no | - |  |
| Formula V (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 4096 | 252 | yes | yes | - | no | - |  |
| Hamburgers En Route to Switzerland (World) (Aftermarket) (Homebrew) | 512 | 48 | yes | yes | yes | yes | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 28 | yes | yes | yes | yes | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 2) (Aftermarket) (Homebrew) | 512 | 36 | yes | yes | yes | yes | - |  |
| Hyper Fighting (World) (Aftermarket) (Homebrew) | 4096 | 16 | yes | yes | - | no | - |  |
| Hyper Fighting (World) (Beta) (Aftermarket) (Homebrew) | 2048 | 17 | yes | yes | - | no | - |  |
| Insecticide (World) (Aftermarket) (Homebrew) | 2048 | 68 | yes | yes | yes | yes | - |  |
| Mario Combat (World) (Aftermarket) (Homebrew) | 2048 | 19 | yes | yes | - | no | - |  |
| Mario Kart - Virtual Cup (World) (Aftermarket) (Homebrew) | 1024 | 49 | yes | yes | yes | yes | - |  |
| Mario VB (World) (Aftermarket) (Homebrew) | 512 | 538 | yes | yes | yes | yes | - |  |
| Null Pointer Mockup (World) (Demo) (Aftermarket) (Homebrew) | 512 | 1153 | yes | yes | - | no | - |  |
| Red Square (World) (Aftermarket) (Homebrew) | 512 | 54 | yes | yes | yes | yes | - |  |
| Snatcher (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 19 | yes | yes | - | no | - |  |
| Snowball Wars (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 406 | yes | yes | - | no | - |  |
| Snowball Wars (World) (Demo 2) (Aftermarket) (Homebrew) | 1024 | 443 | yes | yes | - | no | - |  |
| Snowball Wars (World) (Demo 3) (Aftermarket) (Homebrew) | 1024 | 447 | yes | yes | - | no | - |  |
| Soviet Union 2010 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 88 | yes | yes | yes | yes | - |  |
| Soviet Union 2010 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 131 | yes | yes | yes | yes | - |  |
| Soviet Union 2011 (World) (Beta 1) (Aftermarket) (Homebrew) | 2048 | 95 | yes | yes | yes | yes | - |  |
| Soviet Union 2011 (World) (Beta 2) (Aftermarket) (Homebrew) | 2048 | 98 | yes | yes | yes | yes | - |  |
| Soviet Union 2011 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 119 | yes | yes | yes | yes | - |  |
| Soviet Union 2011 (World) (v1.1) (Aftermarket) (Homebrew) | 2048 | 123 | yes | yes | yes | yes | - |  |
| Soviet Union 2011 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 131 | yes | yes | yes | yes | - |  |
| SPONG (World) (En,Fr,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 173 | yes | yes | - | no | - |  |
| StarFox (World) (Demo) (Aftermarket) (Homebrew) | 128 | 17 | yes | yes | - | no | - |  |
| Tron VB (World) (Demo) (Aftermarket) (Homebrew) | 64 | 33 | yes | yes | yes | yes | - |  |
| VUEngine Platformer (World) (En,Fr,De,Es-ES) (Demo) (Aftermarket) (Homebrew) | 2048 | 200 | yes | yes | - | no | - |  |

