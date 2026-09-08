# Boot status (generic driver)

Every recompiled ROM built with the shared generic driver (no game-specific code) and booted headless for 600 frames. Screenshots in `corpus/_shots/`.

- Builds: **75 / 77**
- Runs without crashing: **75 / 77**
- Drew visible output: **46 / 77** (non-blank screenshot)

Most games run but don't render under the naive driver yet -- they need per-game boot/timing tuning (as the lead games got). 'Drew visible output' marks the ones that already show something.

| # | Game | Builds | Runs | Drew | Notes |
|---|------|--------|------|------|-------|
| 1 | 3-D BattleSnake (World) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 2 | 3D Crosswords (World) (Emulator Version) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 3 | 3D Crosswords (World) (Hardware Version) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 4 | Bad Apple (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 5 | Bad Apple (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 6 | Ballface (World) (Demo) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 7 | BLOX (World) (v1.0) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 8 | BLOX (World) (v1.1) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 9 | BLOX 2 (World) (En,Fr,De,Es,It,Sv,Fi,Cs,Sl) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 10 | Capitan Sevilla 2 (World) (Es) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 11 | Fishbone (World) (Demo 1) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 12 | Fishbone (World) (Demo 2) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 13 | Fishbone (World) (Demo 3) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 14 | Golf (USA) | ok | yes | yes |  |
| 15 | Hamburgers En Route to Switzerland (World) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 16 | Hamburgers En Route to Switzerland (World) (Demo 1) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 17 | Hamburgers En Route to Switzerland (World) (Demo 2) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 18 | Insecticide (World) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 19 | Insmouth no Yakata (Japan) | ok | yes | yes |  |
| 20 | Mario Clash (Japan, USA) (En) | ok | yes | yes |  |
| 21 | Mario Kart - Virtual Cup (World) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 22 | Mario VB (World) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 23 | Mario's Tennis (Japan, USA) (En) | ok | yes | yes |  |
| 24 | Nester's Funky Bowling (USA) | ok | yes | yes |  |
| 25 | Niko-chan Battle (Japan) (Proto) | ok | yes | yes |  |
| 26 | Panic Bomber (USA) | ok | yes | yes |  |
| 27 | Red Alarm (Japan) | ok | yes | yes |  |
| 28 | Red Alarm (USA) | ok | yes | yes |  |
| 29 | Red Square (World) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 30 | Soviet Union 2010 (World) (v1.0) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 31 | Soviet Union 2010 (World) (v1.2) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 32 | Soviet Union 2011 (World) (Beta 1) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 33 | Soviet Union 2011 (World) (Beta 2) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 34 | Soviet Union 2011 (World) (v1.0) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 35 | Soviet Union 2011 (World) (v1.1) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 36 | Soviet Union 2011 (World) (v1.2) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 37 | Space Squash (Japan) | ok | yes | yes |  |
| 38 | T&E Virtual Golf (Japan) | ok | yes | yes |  |
| 39 | Teleroboxer (Japan, USA) (En) | ok | yes | yes |  |
| 40 | Tobidase! Panibon (Japan) | ok | yes | yes |  |
| 41 | Tron VB (World) (Demo) (Aftermarket) (Homebrew) | ok | yes | yes |  |
| 42 | Virtual Boy Wario Land (Japan, USA) (En) | ok | yes | yes |  |
| 43 | Virtual League Baseball (USA) | ok | yes | yes |  |
| 44 | Virtual Pro Yakyuu '95 (Japan) | ok | yes | yes |  |
| 45 | V-Tetris (Japan) | ok | yes | yes |  |
| 46 | Waterworld (USA) | ok | yes | yes |  |
| 47 |  | fail | no | no | vbrecomp.lib(interrupt.obj) : error LNK2019: unresolved external symbol vb_recomp_drain referenced in function vb_interrupt_check [G:\recomp\vb\vbrecomp\build_all\games\corpus__vf.vcxproj] |
| 48 | SD Gundam - Dimension War (Japan) | fail | no | no | vbrecomp.lib(interrupt.obj) : error LNK2019: unresolved external symbol vb_recomp_drain referenced in function vb_interrupt_check [G:\recomp\vb\vbrecomp\build_all\games\corpus_SD_Gundam_Dimension_War_Japan.vcxproj] |
| 49 | 3-D Tetris (USA) | ok | yes | no |  |
| 50 | Bound High (Japan) (En) (Proto) | ok | yes | no |  |
| 51 | Capitan Sevilla 3D (World) (En,De,Es) (Demo) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 52 | Elevated Speed (World) (High Resolution Version) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 53 | Elevated Speed (World) (Low Resolution Version) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 54 | Fishbone (World) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 55 | Formula V (World) (Demo) (Emulator Version) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 56 | Formula V (World) (Demo) (Hardware Version) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 57 | Galactic Pinball (Japan, USA) (En) | ok | yes | no |  |
| 58 | Hyper Fighting (World) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 59 | Hyper Fighting (World) (Beta) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 60 | Jack Bros. (USA) | ok | yes | no |  |
| 61 | Jack Bros. no Meiro de Hiihoo! (Japan) | ok | yes | no |  |
| 62 | Mario Combat (World) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 63 | Null Pointer Mockup (World) (Demo) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 64 | Snatcher (World) (Demo) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 65 | Snowball Wars (World) (Demo 1) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 66 | Snowball Wars (World) (Demo 2) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 67 | Snowball Wars (World) (Demo 3) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 68 | Space Invaders - Virtual Collection (Japan) | ok | yes | no |  |
| 69 | Space Pinball (Japan) (En) (Proto) | ok | yes | no |  |
| 70 | SPONG (World) (En,Fr,De,Es) (Demo) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 71 | StarFox (World) (Demo) (Aftermarket) (Homebrew) | ok | yes | no |  |
| 72 | Vertical Force (Japan) | ok | yes | no |  |
| 73 | Vertical Force (USA) | ok | yes | no |  |
| 74 | Virtual Bowling (Japan) | ok | yes | no |  |
| 75 | Virtual Fishing (Japan) | ok | yes | no |  |
| 76 | Virtual Lab (Japan) | ok | yes | no |  |
| 77 | VUEngine Platformer (World) (En,Fr,De,Es-ES) (Demo) (Aftermarket) (Homebrew) | ok | yes | no |  |
