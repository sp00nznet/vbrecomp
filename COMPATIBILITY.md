# Compatibility

Living status of every ROM in the library. **Recompiles** = v810recomp emits C; **Compiles** = that C builds clean (MSVC `/Zs`) - both columns come from `scripts/sweep.ps1` (see [STATUS.md](STATUS.md)). **Boots / Renders / Playable** are hand-tracked runtime status.

Status values: `yes` | `wip` (partial) | `no` | `-` (untested).

**76 ROMs - 76 recompile, 76 compile clean.**

## Retail (28)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| 3-D Tetris (USA) | 1024 | 586 | yes | yes | - | - | - |  |
| Galactic Pinball (Japan, USA) (En) | 1024 | 31 | yes | yes | yes | wip | no | First pixels on screen; boots through 4-state dispatch, settles ~frame 527 |
| Golf (USA) | 2048 | 64 | yes | yes | - | - | - |  |
| Insmouth no Yakata (Japan) | 1024 | 143 | yes | yes | - | - | - |  |
| Jack Bros. (USA) | 1024 | 29 | yes | yes | - | - | - |  |
| Jack Bros. no Meiro de Hiihoo! (Japan) | 1024 | 29 | yes | yes | - | - | - |  |
| Mario Clash (Japan, USA) (En) | 1024 | 791 | yes | yes | - | - | - |  |
| Mario's Tennis (Japan, USA) (En) | 512 | 205 | yes | yes | yes | wip | wip | Demo mode runs; title/court/sprites render; gameplay upper-BG corruption (block rendering) |
| Nester's Funky Bowling (USA) | 2048 | 189 | yes | yes | - | - | - |  |
| Panic Bomber (USA) | 512 | 188 | yes | yes | - | - | - |  |
| Red Alarm (Japan) | 1024 | 221 | yes | yes | - | - | - |  |
| Red Alarm (USA) | 1024 | 221 | yes | yes | yes | wip | no | Boots to title; wireframe framebuffer populated, pixel layout WIP; 3D scene does not animate |
| SD Gundam - Dimension War (Japan) | 1024 | 231 | yes | yes | - | - | - |  |
| Space Invaders - Virtual Collection (Japan) | 512 | 45 | yes | yes | - | - | - |  |
| Space Squash (Japan) | 512 | 3 | yes | yes | - | - | - |  |
| T&E Virtual Golf (Japan) | 2048 | 65 | yes | yes | - | - | - |  |
| Teleroboxer (Japan, USA) (En) | 1024 | 611 | yes | yes | - | - | - |  |
| Tobidase! Panibon (Japan) | 512 | 188 | yes | yes | - | - | - |  |
| Vertical Force (Japan) | 1024 | 101 | yes | yes | - | - | - |  |
| Vertical Force (USA) | 1024 | 101 | yes | yes | - | - | - |  |
| Virtual Bowling (Japan) | 1024 | 250 | yes | yes | - | - | - |  |
| Virtual Boy Wario Land (Japan, USA) (En) | 2048 | 3025 | yes | yes | - | - | - |  |
| Virtual Fishing (Japan) | 1024 | 22 | yes | yes | - | - | - |  |
| Virtual Lab (Japan) | 1024 | 207 | yes | yes | - | - | - |  |
| Virtual League Baseball (USA) | 1024 | 76 | yes | yes | - | - | - |  |
| Virtual Pro Yakyuu '95 (Japan) | 1024 | 71 | yes | yes | - | - | - |  |
| V-Tetris (Japan) | 512 | 159 | yes | yes | - | - | - |  |
| Waterworld (USA) | 2048 | 85 | yes | yes | - | - | - |  |

## Proto / Demo (3)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| Bound High (Japan) (En) (Proto) | 2048 | 319 | yes | yes | - | - | - |  |
| Niko-chan Battle (Japan) (Proto) | 1024 | 341 | yes | yes | - | - | - |  |
| Space Pinball (Japan) (En) (Proto) | 2048 | 33 | yes | yes | - | - | - |  |

## Homebrew (45)

| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |
|------|----|-------|------------|----------|-------|---------|----------|-------|
| 3-D BattleSnake (World) (Aftermarket) (Homebrew) | 1024 | 12 | yes | yes | - | - | - |  |
| 3D Crosswords (World) (Emulator Version) (Aftermarket) (Homebrew) | 2048 | 16 | yes | yes | - | - | - |  |
| 3D Crosswords (World) (Hardware Version) (Aftermarket) (Homebrew) | 512 | 16 | yes | yes | - | - | - |  |
| Bad Apple (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 16384 | 4 | yes | yes | - | - | - |  |
| Bad Apple (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 16384 | 4 | yes | yes | - | - | - |  |
| Ballface (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 98 | yes | yes | - | - | - |  |
| BLOX (World) (v1.0) (Aftermarket) (Homebrew) | 1024 | 42 | yes | yes | - | - | - |  |
| BLOX (World) (v1.1) (Aftermarket) (Homebrew) | 1024 | 45 | yes | yes | - | - | - |  |
| BLOX 2 (World) (En,Fr,De,Es,It,Sv,Fi,Cs,Sl) (Aftermarket) (Homebrew) | 2048 | 109 | yes | yes | - | - | - |  |
| Capitan Sevilla 2 (World) (Es) (Aftermarket) (Homebrew) | 1024 | 20 | yes | yes | - | - | - |  |
| Capitan Sevilla 3D (World) (En,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 159 | yes | yes | - | - | - |  |
| Elevated Speed (World) (High Resolution Version) (Aftermarket) (Homebrew) | 4096 | 51 | yes | yes | - | - | - |  |
| Elevated Speed (World) (Low Resolution Version) (Aftermarket) (Homebrew) | 2048 | 51 | yes | yes | - | - | - |  |
| Fishbone (World) (Aftermarket) (Homebrew) | 2048 | 14 | yes | yes | - | - | - |  |
| Fishbone (World) (Demo 1) (Aftermarket) (Homebrew) | 2048 | 29 | yes | yes | - | - | - |  |
| Fishbone (World) (Demo 2) (Aftermarket) (Homebrew) | 2048 | 69 | yes | yes | - | - | - |  |
| Fishbone (World) (Demo 3) (Aftermarket) (Homebrew) | 2048 | 73 | yes | yes | - | - | - |  |
| Formula V (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | 4096 | 176 | yes | yes | - | - | - |  |
| Formula V (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | 4096 | 176 | yes | yes | - | - | - |  |
| Hamburgers En Route to Switzerland (World) (Aftermarket) (Homebrew) | 512 | 20 | yes | yes | - | - | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 14 | yes | yes | - | - | - |  |
| Hamburgers En Route to Switzerland (World) (Demo 2) (Aftermarket) (Homebrew) | 512 | 18 | yes | yes | - | - | - |  |
| Hyper Fighting (World) (Aftermarket) (Homebrew) | 4096 | 9 | yes | yes | - | - | - |  |
| Hyper Fighting (World) (Beta) (Aftermarket) (Homebrew) | 2048 | 10 | yes | yes | - | - | - |  |
| Insecticide (World) (Aftermarket) (Homebrew) | 2048 | 32 | yes | yes | - | - | - |  |
| Mario Combat (World) (Aftermarket) (Homebrew) | 2048 | 12 | yes | yes | - | - | - |  |
| Mario Kart - Virtual Cup (World) (Aftermarket) (Homebrew) | 1024 | 25 | yes | yes | - | - | - |  |
| Mario VB (World) (Aftermarket) (Homebrew) | 512 | 523 | yes | yes | - | - | - |  |
| Null Pointer Mockup (World) (Demo) (Aftermarket) (Homebrew) | 512 | 1135 | yes | yes | - | - | - |  |
| Red Square (World) (Aftermarket) (Homebrew) | 512 | 32 | yes | yes | - | - | - |  |
| Snatcher (World) (Demo) (Aftermarket) (Homebrew) | 2048 | 12 | yes | yes | - | - | - |  |
| Snowball Wars (World) (Demo 1) (Aftermarket) (Homebrew) | 256 | 395 | yes | yes | - | - | - |  |
| Snowball Wars (World) (Demo 2) (Aftermarket) (Homebrew) | 1024 | 432 | yes | yes | - | - | - |  |
| Snowball Wars (World) (Demo 3) (Aftermarket) (Homebrew) | 1024 | 436 | yes | yes | - | - | - |  |
| Soviet Union 2010 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 46 | yes | yes | - | - | - |  |
| Soviet Union 2010 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 79 | yes | yes | - | - | - |  |
| Soviet Union 2011 (World) (Beta 1) (Aftermarket) (Homebrew) | 2048 | 54 | yes | yes | - | - | - |  |
| Soviet Union 2011 (World) (Beta 2) (Aftermarket) (Homebrew) | 2048 | 60 | yes | yes | - | - | - |  |
| Soviet Union 2011 (World) (v1.0) (Aftermarket) (Homebrew) | 2048 | 71 | yes | yes | - | - | - |  |
| Soviet Union 2011 (World) (v1.1) (Aftermarket) (Homebrew) | 2048 | 72 | yes | yes | - | - | - |  |
| Soviet Union 2011 (World) (v1.2) (Aftermarket) (Homebrew) | 2048 | 78 | yes | yes | - | - | - |  |
| SPONG (World) (En,Fr,De,Es) (Demo) (Aftermarket) (Homebrew) | 2048 | 122 | yes | yes | - | - | - |  |
| StarFox (World) (Demo) (Aftermarket) (Homebrew) | 128 | 10 | yes | yes | - | - | - |  |
| Tron VB (World) (Demo) (Aftermarket) (Homebrew) | 64 | 13 | yes | yes | - | - | - |  |
| VUEngine Platformer (World) (En,Fr,De,Es-ES) (Demo) (Aftermarket) (Homebrew) | 2048 | 148 | yes | yes | - | - | - |  |

