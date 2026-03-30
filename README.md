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

🏗️ Early days. We're using real games to drive development:
- **Mario's Tennis** — Primary target
- **Red Alarm** — Secondary target (wireframe madness)

## Stretch Goals

- VR headset support — the Virtual Boy was *meant* for this, it just arrived 25 years too early
- Stereoscopic 3D rendering on modern hardware

## Architecture

The Virtual Boy is a surprisingly clean little system:
- **NEC V810** — 32-bit RISC CPU @ 20MHz
- **VIP** — Custom video processor, 384×224 per eye, 4 shades of red
- **VSU** — 6 sound channels (5 wave + 1 noise)
- Simple memory map, no MMU shenanigans

This makes it a great candidate for static recompilation.

## License

MIT — do whatever you want with it.
