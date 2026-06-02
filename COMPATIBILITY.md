# Compatibility

Living status of every ROM in the library. **Recompiles** = v810recomp emits C; **Compiles** = that C builds clean (MSVC `/Zs`) - both columns come from `scripts/sweep.ps1` (see [STATUS.md](STATUS.md)). **Boots / Renders / Playable** are hand-tracked runtime status.

Status values: `yes` | `wip` (partial) | `no` | `-` (untested).

**76 ROMs - 76 recompile, 76 compile clean.**

## Retail (28)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| 3-D Tetris (USA) | 1024 | 585 | yes | yes | - | - | - |  |
| Galactic Pinball (Japan, USA) (En) | 1024 | 30 | yes | yes | yes | wip | no | First pixels on screen; boots through 4-state dispatch, settles ~frame 527 |
| Golf (USA) | 2048 | 0 | yes | yes | - | - | - |  |
| Insmouth no Yakata (Japan) | 1024 | 142 | yes | yes | - | - | - |  |
| Jack Bros. (USA) | 1024 | 28 | yes | yes | - | - | - |  |
| Jack Bros. no Meiro de Hiihoo! (Japan) | 1024 | 28 | yes | yes | - | - | - |  |
| Mario Clash (Japan, USA) (En) | 1024 | 790 | yes | yes | - | - | - |  |
| Mario's Tennis (Japan, USA) (En) | 512 | 205 | yes | yes | yes | wip | wip | Demo mode runs; title/court/sprites render; gameplay upper-BG corruption (block rendering) |
| Nester's Funky Bowling (USA) | 2048 | 0 | yes | yes | - | - | - |  |
| Panic Bomber (USA) | 512 | 187 | yes | yes | - | - | - |  |
| Red Alarm (Japan) | 1024 | 220 | yes | yes | - | - | - |  |
| Red Alarm (USA) | 1024 | 220 | yes | yes | yes | wip | no | Boots to title; wireframe framebuffer populated, pixel layout WIP; 3D scene does not animate |
| SD Gundam - Dimension War (Japan) | 1024 | 230 | yes | yes | - | - | - |  |
| Space Invaders - Virtual Collection (Japan) | 512 | 44 | yes | yes | - | - | - |  |
| Space Squash (Japan) | 512 | 2 | yes | yes | - | - | - |  |
| T&E Virtual Golf (Japan) | 2048 | 64 | yes | yes | - | - | - |  |
| Teleroboxer (Japan, USA) (En) | 1024 | 610 | yes | yes | - | - | - |  |
| Tobidase! Panibon (Japan) | 512 | 187 | yes | yes | - | - | - |  |
| Vertical Force (Japan) | 1024 | 100 | yes | yes | - | - | - |  |
| Vertical Force (USA) | 1024 | 100 | yes | yes | - | - | - |  |
| Virtual Bowling (Japan) | 1024 | 250 | yes | yes | - | - | - |  |
| Virtual Boy Wario Land (Japan, USA) (En) | 2048 | 3024 | yes | yes | - | - | - |  |
| Virtual Fishing (Japan) | 1024 | 21 | yes | yes | - | - | - |  |
| Virtual Lab (Japan) | 1024 | 206 | yes | yes | - | - | - |  |
| Virtual League Baseball (USA) | 1024 | 75 | yes | yes | - | - | - |  |
| Virtual Pro Yakyuu '95 (Japan) | 1024 | 70 | yes | yes | - | - | - |  |
| V-Tetris (Japan) | 512 | 158 | yes | yes | - | - | - |  |
| Waterworld (USA) | 2048 | 84 | yes | yes | - | - | - |  |

## Proto / Demo (3)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| Bound High (Japan) (En) (Proto) | 2048 | 318 | yes | yes | - | - | - |  |
| Niko-chan Battle (Japan) (Proto) | 1024 | 340 | yes | yes | - | - | - |  |
| Space Pinball (Japan) (En) (Proto) | 2048 | 32 | yes | yes | - | - | - |  |

## Homebrew (45)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| 3-D BattleSnake (World) (Aftermarket) (Homebrew) | 1024 | 11 | yes | yes | - | - | - |  |
| 3D Crosswords (World) (Emulator Version) (Aftermarket) (Homebrew) | 2048 | 15 | yes | yes | - | - | - |  |
| 3D Crosswords (World) (Hardware Version) (Aftermarket) (Homebrew) | 512 | 15 | yes | yes | - | - | - |  |
| Bad Apple (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 16384 | 3 | yes | yes | - | - | - |  |
| Bad Apple (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 16384 | 3 | yes | yes | - | - | - |  |
| Ballface (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 97 | yes | yes | - | - | - |  |
| BLOX (World) (v1.0) (Aftermarket) (Homebrew) | 1024 | 41 | yes | yes | - | - | - |  |
| BLOX (World) (v1.1) (Aftermarket) (Homebrew) | 1024 | 44 | yes | yes | - | - | - |  |
| BLOX 2 (World) (En,Fr,De,Es,It,Sv,Fi,Cs,Sl) (Aftermarket) (Homebrew) | 2048 | 108 | yes | yes | - | - | - |  |
| Capitan Sevilla 2 (World) (Es) (Aftermarket) (Homebrew) | 1024 | 19 | yes | yes | - | - | - |  |
| Capitan Sevilla 3D (World) (En,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 158 | yes | yes | - | - | - |  |
| Elevated Speed (World) (High Resolution Version) (Aftermarket) (Homebrew) | 4096 | 50 | yes | yes | - | - | - |  |
| Elevated Speed (World) (Low Resolution Version) (Aftermarket) (Homebrew) | 2048 | 50 | yes | yes | - | - | - |  |
| Fishbone (World) (Aftermarket) (Homebrew) | 2048 | 13 | yes | yes | - | - | - |  |
| Fishbone (World) (Demo 1) (Aftermarket) (Homebrew) | 2048 | 28 | yes | yes | - | - | - |  |
| Fishbone (World) (Demo 2) (Aftermarket) (Homebrew) | 2048 | 68 | yes | yes | - | - | - |  |
| Fishbone (World) (Demo 3) (Aftermarket) (Homebrew) | 2048 | 72 | yes | yes | - | - | - |  |
| Formula V (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 4096 | 175 | yes | yes | - | - | - |  |
| Formula V (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 4096 | 175 | yes | yes | - | - | - |  |
| Hamburgers En Route to Switzerland (World) (Aftermarket) (Homebrew) | 512 | 19 | yes | yes | - | - | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 13 | yes | yes | - | - | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 2) (Aftermarket) (Homebrew) | 512 | 17 | yes | yes | - | - | - |  |
| Hyper Fighting (World) (Aftermarket) (Homebrew) | 4096 | 8 | yes | yes | - | - | - |  |
| Hyper Fighting (World) (Beta) (Aftermarket) (Homebrew) | 2048 | 9 | yes | yes | - | - | - |  |
| Insecticide (World) (Aftermarket) (Homebrew) | 2048 | 31 | yes | yes | - | - | - |  |
| Mario Combat (World) (Aftermarket) (Homebrew) | 2048 | 11 | yes | yes | - | - | - |  |
| Mario Kart - Virtual Cup (World) (Aftermarket) (Homebrew) | 1024 | 24 | yes | yes | - | - | - |  |
| Mario VB (World) (Aftermarket) (Homebrew) | 512 | 522 | yes | yes | - | - | - |  |
| Null Pointer Mockup (World) (Demo) (Aftermarket) (Homebrew) | 512 | 1134 | yes | yes | - | - | - |  |
| Red Square (World) (Aftermarket) (Homebrew) | 512 | 1 | yes | yes | - | - | - |  |
| Snatcher (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 11 | yes | yes | - | - | - |  |
| Snowball Wars (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 394 | yes | yes | - | - | - |  |
| Snowball Wars (World) (Demo 2) (Aftermarket) (Homebrew) | 1024 | 431 | yes | yes | - | - | - |  |
| Snowball Wars (World) (Demo 3) (Aftermarket) (Homebrew) | 1024 | 435 | yes | yes | - | - | - |  |
| Soviet Union 2010 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 45 | yes | yes | - | - | - |  |
| Soviet Union 2010 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 78 | yes | yes | - | - | - |  |
| Soviet Union 2011 (World) (Beta 1) (Aftermarket) (Homebrew) | 2048 | 53 | yes | yes | - | - | - |  |
| Soviet Union 2011 (World) (Beta 2) (Aftermarket) (Homebrew) | 2048 | 59 | yes | yes | - | - | - |  |
| Soviet Union 2011 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 70 | yes | yes | - | - | - |  |
| Soviet Union 2011 (World) (v1.1) (Aftermarket) (Homebrew) | 2048 | 71 | yes | yes | - | - | - |  |
| Soviet Union 2011 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 77 | yes | yes | - | - | - |  |
| SPONG (World) (En,Fr,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 121 | yes | yes | - | - | - |  |
| StarFox (World) (Demo) (Aftermarket) (Homebrew) | 128 | 9 | yes | yes | - | - | - |  |
| Tron VB (World) (Demo) (Aftermarket) (Homebrew) | 64 | 12 | yes | yes | - | - | - |  |
| VUEngine Platformer (World) (En,Fr,De,Es-ES) (Demo) (Aftermarket) (Homebrew) | 2048 | 147 | yes | yes | - | - | - |  |

