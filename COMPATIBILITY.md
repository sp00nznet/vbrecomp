# Compatibility

Living status of every ROM in the library. **Recompiles** = v810recomp emits C; **Compiles** = that C builds clean (MSVC `/Zs`) - both columns come from `scripts/sweep.ps1` (see [STATUS.md](STATUS.md)). **Boots / Renders / Playable** are hand-tracked runtime status.

Status values: `yes` | `wip` (partial) | `no` | `-` (untested).

**76 ROMs - 76 recompile, 0 compile clean.**

## Retail (28)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| 3-D Tetris (USA) | 1024 | 743 | yes | no | - | no | - |  |
| Galactic Pinball (Japan, USA) (En) | 1024 | 33 | yes | no | yes | wip | no | First pixels on screen; boots through 4-state dispatch, settles ~frame 527 |
| Golf (USA) | 2048 | 67 | yes | no | - | no | - |  |
| Insmouth no Yakata (Japan) | 1024 | 190 | yes | no | - | no | - |  |
| Jack Bros. (USA) | 1024 | 30 | yes | no | - | no | - |  |
| Jack Bros. no Meiro de Hiihoo! (Japan) | 1024 | 30 | yes | no | - | no | - |  |
| Mario Clash (Japan, USA) (En) | 1024 | 792 | yes | no | yes | wip | no | Boots via the generic driver (no hand-written glue); programs VIP + world attributes, advancing frames |
| Mario's Tennis (Japan, USA) (En) | 512 | 210 | yes | no | yes | wip | wip | Demo mode runs; title/court/sprites render; gameplay upper-BG corruption (block rendering) |
| Nester's Funky Bowling (USA) | 2048 | 191 | yes | no | yes | yes | - |  |
| Panic Bomber (USA) | 512 | 193 | yes | no | yes | yes | - |  |
| Red Alarm (Japan) | 1024 | 259 | yes | no | yes | yes | - |  |
| Red Alarm (USA) | 1024 | 259 | yes | no | yes | wip | no | Boots to title; wireframe framebuffer populated, pixel layout WIP; 3D scene does not animate |
| SD Gundam - Dimension War (Japan) | 1024 | 232 | yes | no | yes | yes | - |  |
| Space Invaders - Virtual Collection (Japan) | 512 | 47 | yes | no | - | no | - |  |
| Space Squash (Japan) | 512 | 148 | yes | no | yes | wip | - |  |
| T&E Virtual Golf (Japan) | 2048 | 68 | yes | no | - | no | - |  |
| Teleroboxer (Japan, USA) (En) | 1024 | 612 | yes | no | yes | wip | - |  |
| Tobidase! Panibon (Japan) | 512 | 193 | yes | no | yes | yes | - |  |
| Vertical Force (Japan) | 1024 | 106 | yes | no | - | no | - |  |
| Vertical Force (USA) | 1024 | 106 | yes | no | - | no | - |  |
| Virtual Bowling (Japan) | 1024 | 255 | yes | no | - | no | - |  |
| Virtual Boy Wario Land (Japan, USA) (En) | 2048 | 3026 | yes | no | - | no | - |  |
| Virtual Fishing (Japan) | 1024 | 66 | yes | no | - | no | - |  |
| Virtual Lab (Japan) | 1024 | 208 | yes | no | - | no | - |  |
| Virtual League Baseball (USA) | 1024 | 80 | yes | no | yes | yes | - |  |
| Virtual Pro Yakyuu '95 (Japan) | 1024 | 75 | yes | no | yes | yes | - |  |
| V-Tetris (Japan) | 512 | 160 | yes | no | yes | yes | - |  |
| Waterworld (USA) | 2048 | 108 | yes | no | yes | yes | - |  |

## Proto / Demo (3)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| Bound High (Japan) (En) (Proto) | 2048 | 437 | yes | no | yes | yes | - |  |
| Niko-chan Battle (Japan) (Proto) | 1024 | 346 | yes | no | - | no | - |  |
| Space Pinball (Japan) (En) (Proto) | 2048 | 35 | yes | no | - | no | - |  |

## Homebrew (45)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| 3-D BattleSnake (World) (Aftermarket) (Homebrew) | 1024 | 12 | yes | no | - | no | - |  |
| 3D Crosswords (World) (Emulator Version) (Aftermarket) (Homebrew) | 2048 | 16 | yes | no | yes | yes | - |  |
| 3D Crosswords (World) (Hardware Version) (Aftermarket) (Homebrew) | 512 | 16 | yes | no | yes | yes | - |  |
| Bad Apple (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 16384 | 4 | yes | no | - | no | - |  |
| Bad Apple (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 16384 | 4 | yes | no | - | no | - |  |
| Ballface (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 103 | yes | no | yes | yes | - |  |
| BLOX (World) (v1.0) (Aftermarket) (Homebrew) | 1024 | 42 | yes | no | yes | yes | - |  |
| BLOX (World) (v1.1) (Aftermarket) (Homebrew) | 1024 | 45 | yes | no | yes | yes | - |  |
| BLOX 2 (World) (En,Fr,De,Es,It,Sv,Fi,Cs,Sl) (Aftermarket) (Homebrew) | 2048 | 114 | yes | no | - | no | - |  |
| Capitan Sevilla 2 (World) (Es) (Aftermarket) (Homebrew) | 1024 | 20 | yes | no | - | no | - |  |
| Capitan Sevilla 3D (World) (En,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 164 | yes | no | - | no | - |  |
| Elevated Speed (World) (High Resolution Version) (Aftermarket) (Homebrew) | 4096 | 56 | yes | no | yes | wip | - |  |
| Elevated Speed (World) (Low Resolution Version) (Aftermarket) (Homebrew) | 2048 | 56 | yes | no | yes | wip | - |  |
| Fishbone (World) (Aftermarket) (Homebrew) | 2048 | 19 | yes | no | - | no | - |  |
| Fishbone (World) (Demo 1) (Aftermarket) (Homebrew) | 2048 | 34 | yes | no | yes | yes | - |  |
| Fishbone (World) (Demo 2) (Aftermarket) (Homebrew) | 2048 | 74 | yes | no | yes | yes | - |  |
| Fishbone (World) (Demo 3) (Aftermarket) (Homebrew) | 2048 | 78 | yes | no | yes | yes | - |  |
| Formula V (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 4096 | 181 | yes | no | - | no | - |  |
| Formula V (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 4096 | 181 | yes | no | - | no | - |  |
| Hamburgers En Route to Switzerland (World) (Aftermarket) (Homebrew) | 512 | 20 | yes | no | yes | yes | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 14 | yes | no | yes | yes | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 2) (Aftermarket) (Homebrew) | 512 | 18 | yes | no | yes | yes | - |  |
| Hyper Fighting (World) (Aftermarket) (Homebrew) | 4096 | 14 | yes | no | - | no | - |  |
| Hyper Fighting (World) (Beta) (Aftermarket) (Homebrew) | 2048 | 15 | yes | no | - | no | - |  |
| Insecticide (World) (Aftermarket) (Homebrew) | 2048 | 32 | yes | no | - | no | - |  |
| Mario Combat (World) (Aftermarket) (Homebrew) | 2048 | 17 | yes | no | - | no | - |  |
| Mario Kart - Virtual Cup (World) (Aftermarket) (Homebrew) | 1024 | 30 | yes | no | - | no | - |  |
| Mario VB (World) (Aftermarket) (Homebrew) | 512 | 528 | yes | no | - | no | - |  |
| Null Pointer Mockup (World) (Demo) (Aftermarket) (Homebrew) | 512 | 1140 | yes | no | - | no | - |  |
| Red Square (World) (Aftermarket) (Homebrew) | 512 | 32 | yes | no | yes | yes | - |  |
| Snatcher (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 17 | yes | no | - | no | - |  |
| Snowball Wars (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 400 | yes | no | yes | yes | - |  |
| Snowball Wars (World) (Demo 2) (Aftermarket) (Homebrew) | 1024 | 437 | yes | no | yes | yes | - |  |
| Snowball Wars (World) (Demo 3) (Aftermarket) (Homebrew) | 1024 | 441 | yes | no | yes | yes | - |  |
| Soviet Union 2010 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 46 | yes | no | - | no | - |  |
| Soviet Union 2010 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 79 | yes | no | - | no | - |  |
| Soviet Union 2011 (World) (Beta 1) (Aftermarket) (Homebrew) | 2048 | 54 | yes | no | - | no | - |  |
| Soviet Union 2011 (World) (Beta 2) (Aftermarket) (Homebrew) | 2048 | 60 | yes | no | - | no | - |  |
| Soviet Union 2011 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 71 | yes | no | - | no | - |  |
| Soviet Union 2011 (World) (v1.1) (Aftermarket) (Homebrew) | 2048 | 72 | yes | no | - | no | - |  |
| Soviet Union 2011 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 78 | yes | no | - | no | - |  |
| SPONG (World) (En,Fr,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 127 | yes | no | - | no | - |  |
| StarFox (World) (Demo) (Aftermarket) (Homebrew) | 128 | 15 | yes | no | - | no | - |  |
| Tron VB (World) (Demo) (Aftermarket) (Homebrew) | 64 | 13 | yes | no | - | no | - |  |
| VUEngine Platformer (World) (En,Fr,De,Es-ES) (Demo) (Aftermarket) (Homebrew) | 2048 | 153 | yes | no | - | no | - |  |

