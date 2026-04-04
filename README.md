# vbrecomp

**Static recompilation libraries for Virtual Boy games.**

Remember the Virtual Boy? Nintendo's glorious red-and-black fever dream from 1995? Yeah, we're bringing those games back from the dead — and this time, you don't need to destroy your neck or your retinas to play them.

## What is this?

vbrecomp is a set of libraries for statically recompiling Virtual Boy ROMs into native executables. Think [N64Recomp](https://github.com/N64Recomp/N64Recomp), but for Nintendo's most misunderstood console.

The pipeline:
1. **V810 Static Recompiler** — Translates V810 binary code into C
2. **VIP (Video Image Processor)** — Renders those beautiful red parallax graphics
3. **VSU (Virtual Sound Unit)** — Recreates the surprisingly decent sound hardware
4. **Hardware Runtime** — Timers, interrupts, game pad, link port, all the glue

## Current Status

### VIP Renderer
The VIP renderer is functional with the following features:
- **Affine mode** (BGM=2) — per-scanline transforms with 16-byte parameter stride, area sampling for zoom-out
- **Normal mode** (BGM=0) — standard tiled backgrounds with multi-segment BGMap support
- **OBJ mode** (BGM=3) — hardware sprites via OAM with SPT group support
- **H-Bias mode** (BGM=1) — treated as Normal for now
- **Eye filtering** — proper LON/RON handling for left/right eye rendering
- **World attribute mirror** at 0x3DC00 for dynamic world updates
- **CHR RAM mirrors** at 0x78000-0x7FFFF (validated against [rustual-boy](https://github.com/emu-rs/rustual-boy))
- **Separate CHR segments** — CHR 0-3 kept independent to support block-based tile swapping
- **ImGui menu system** with toast notifications (from [sp00nznet/tools](https://github.com/sp00nznet/tools))

### Mario's Tennis Progress
- Warning screen text renders clearly
- Mario character sprites visible and animated
- Tennis court with 3D perspective, net, ball, service lines
- Court close-up and wide views both render
- Demo mode runs naturally after ~70 second timeout
- Game progresses through states 0→1→2→4→6 (gameplay)

### Known Issues
- **Gameplay view corruption** — during demo mode, the upper background renders garbled tiles. Root cause: the game does block-based CHR tile swapping (VB renders 8 rows at a time), but our renderer draws all 224 rows at once. Reference emulator comparison confirms VRAM data is correct; the issue is render timing.
- **Warning screen invisible** — with corrected VIP register offsets, the warning screen palettes/brightness aren't initialized during early states. Forced palettes provide fallback.

### Key Fixes (validated against rustual-boy reference emulator)
- VIP register offsets corrected (DPSTTS=0x20, GPLT0=0x60, SPT0=0x48, etc.)
- CHR RAM mirrors at 0x78000-0x7FFFF (was mapping to BGMap)
- BGMap base from HEAD bits 3:0 (was incorrectly from PARAM)
- `is_vram_addr` accepts addresses >0x60000 (was silently dropping writes)
- GX is 10-bit signed (sign-extend from bit 9)
- OBJ format: word0=X, word1=L/R/parallax, word2=Y, word3=pal/flip/char
- Param table offset: param_base * 2 (was masking with 0xFFF0)

## Target Games
- **Mario's Tennis** — Primary target (playable with rendering issues)
- **Red Alarm** — Secondary target (wireframe madness)

## Stretch Goals

- VR headset support — the Virtual Boy was *meant* for this, it just arrived 25 years too early
- Stereoscopic 3D rendering on modern hardware
- Block-based VIP rendering for accurate tile timing

## Architecture

The Virtual Boy is a surprisingly clean little system:
- **NEC V810** — 32-bit RISC CPU @ 20MHz
- **VIP** — Custom video processor, 384x224 per eye, 4 shades of red
- **VSU** — 6 sound channels (5 wave + 1 noise)
- Simple memory map, no MMU shenanigans

This makes it a great candidate for static recompilation.

## Dependencies

- SDL2 — windowing, input, audio
- Dear ImGui — menu bar and toast notifications
- stb_image_write — screenshot capture

## License

MIT — do whatever you want with it.
