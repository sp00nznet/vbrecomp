# vbrecomp

**Static recompilation libraries for Virtual Boy games.**

Remember the Virtual Boy? Nintendo's glorious red-and-black fever dream from 1995? Yeah, we're bringing those games back from the dead — and this time, you don't need to destroy your neck or your retinas to play them.

![Mario's Tennis title screen running via vbrecomp](screenshots/mariotennis_title.png)

![Tennis court with 3D perspective rendering](screenshots/mariotennis_court.png)

## What is this?

vbrecomp is a set of libraries for statically recompiling Virtual Boy ROMs into native executables. Think [N64Recomp](https://github.com/N64Recomp/N64Recomp), but for Nintendo's most misunderstood console.

The Virtual Boy only had 22 games. That's a small enough library that we could realistically recompile *all of them*. That's the goal.

### The Pipeline

1. **V810 Static Recompiler** (`tools/v810recomp`) — Translates V810 binary code into C functions
2. **VIP (Video Image Processor)** — Renders those beautiful red parallax graphics
3. **VSU (Virtual Sound Unit)** — Recreates the surprisingly decent 6-channel sound hardware
4. **Hardware Runtime** — Timers, interrupts, game pad, link port — all the glue

## Current Status

### VIP Renderer

The VIP renderer is the core of the project and is actively under development:

| Feature | Status |
|---------|--------|
| Normal mode (BGM=0) | Working — tiled backgrounds with multi-segment BGMap support |
| H-Bias mode (BGM=1) | Stubbed — treated as Normal |
| Affine mode (BGM=2) | Working — per-scanline transforms, area sampling for zoom-out |
| OBJ mode (BGM=3) | Working — hardware sprites via OAM with SPT group support |
| Eye filtering | Working — proper LON/RON handling for left/right eye |
| CHR RAM mirrors | Working — 0x78000-0x7FFFF validated against rustual-boy |
| Separate CHR segments | Working — CHR 0-3 independent for block-based tile swapping |
| World attribute mirror | Working — 0x3DC00 for dynamic world updates |
| Direct frame buffer | Working — column-major 2bpp planar format for wireframe games |

### Key Technical Fixes

All validated against the [rustual-boy](https://github.com/emu-rs/rustual-boy) reference emulator:

- VIP register offsets corrected (DPSTTS=0x20, GPLT0=0x60, SPT0=0x48, etc.)
- CHR RAM mirrors at 0x78000-0x7FFFF (was incorrectly mapping to BGMap)
- BGMap base derived from HEAD bits 3:0 (was incorrectly from PARAM)
- `is_vram_addr` accepts addresses >0x60000 (was silently dropping writes)
- GX is 10-bit signed (sign-extend from bit 9)
- OBJ word format: word0=X, word1=L/R/parallax, word2=Y, word3=pal/flip/char
- Param table offset uses `param_base * 2` (was masking with 0xFFF0)
- v810recomp emitter: only add JMP-table label targets when they fall inside the current function (cross-function targets handled by tail call instead) — fixes undefined-label C compile errors on Galactic Pinball
- v810recomp emitter: emit orphan-label stubs at end of function for branch targets that fall mid-instruction (off the decoder's 4-byte walk) — keeps generated C compilable when the analyzer extends a function into data
- v810recomp: `--hints <file>` flag to manually resolve indirect jumps and add function entry points the static analyzer can't reach (e.g. function pointers stored in WRAM and dispatched later) — needed for Galactic Pinball's main dispatch

### Open Problems

- **Block-based VIP rendering** — The VB renders 8 rows at a time, but our renderer draws all 224 rows at once. Games that swap CHR tiles mid-frame (like Mario's Tennis during gameplay) get garbled upper backgrounds. VRAM data is confirmed correct; the issue is render timing.

## Target Games

| Game | Repo | Status |
|------|------|--------|
| Mario's Tennis | [vb-mariotennis](https://github.com/sp00nznet/vb-mariotennis) | Demo mode runs, rendering WIP |
| Red Alarm | [vb-redalarm](https://github.com/sp00nznet/vb-redalarm) | Boots to title, frame buffer rendering WIP |
| Galactic Pinball | vb-galacticpinball (private) | Reset executes, VIP interrupt loop firing; BGMap rendering next |

The Virtual Boy library is only 22 games. Once the core libraries are solid, porting additional titles should be straightforward.

## Architecture

The Virtual Boy is a surprisingly clean little system:

- **NEC V810** — 32-bit RISC CPU @ 20MHz
- **VIP** — Custom video processor, 384x224 per eye, 4 shades of red
- **VSU** — 6 sound channels (5 wave + 1 noise)
- Simple memory map, no MMU shenanigans

This makes it a great candidate for static recompilation.

## Stretch Goals

- VR headset support — the Virtual Boy was *meant* for this, it just arrived 25 years too early
- Stereoscopic 3D rendering on modern hardware
- Block-based VIP rendering for accurate mid-frame tile timing

## Dependencies

- SDL2 — windowing, input, audio
- Dear ImGui — menu bar and toast notifications (from [sp00nznet/tools](https://github.com/sp00nznet/tools))
- stb_image_write — screenshot capture

## License

MIT — do whatever you want with it.
