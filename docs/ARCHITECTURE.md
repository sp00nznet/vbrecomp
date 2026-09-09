# Architecture

## The Virtual Boy

A surprisingly clean little system:

- **NEC V810** — 32-bit RISC CPU @ 20 MHz, 32 GP registers (r0 hardwired to 0), a handful of system registers. No MMU.
- **VIP** — custom Video Image Processor, 384×224 per eye, 4 shades of red, stereoscopic.
- **VSU** — 6 sound channels (5 wave + 1 noise).
- Simple, flat memory map with ROM mirrored throughout `0x07000000–0x07FFFFFF` (and at `0xFFF00000+`).

This makes it an excellent candidate for static recompilation.

### V810 register ABI (as used by the generated code)

| Reg | Role |
|-----|------|
| `r0` | zero (hardwired) |
| `r3` | sp — stack pointer |
| `r4` | gp — global pointer |
| `r5` | tp — text pointer |
| `r31` | lp — link/return address |
| `sr[5]` | psw — program status word (flags) |

## The VIP renderer

The VIP is the core of the runtime and is under active development (`src/vip.c`).

| Feature | Status |
|---------|--------|
| Normal mode (BGM=0) | Working — tiled backgrounds, multi-segment BGMap |
| H-Bias mode (BGM=1) | Stubbed — treated as Normal |
| Affine mode (BGM=2) | Working — per-scanline transforms, area sampling for zoom-out |
| OBJ mode (BGM=3) | Working — hardware sprites via OAM with SPT group support |
| Eye filtering | Working — LON/RON handling for left/right eye |
| CHR RAM mirrors | Working — 0x78000–0x7FFFF validated against rustual-boy |
| Separate CHR segments | Working — CHR 0–3 independent for block-based tile swapping |
| Column table (0x3DC00) | Working — a separate region, **not** a world-attribute mirror |
| Direct frame buffer | Working — column-major 2bpp planar for wireframe games |

### Validated technical fixes

All cross-checked against the [rustual-boy](https://github.com/emu-rs/rustual-boy) reference emulator:

- VIP register offsets corrected (DPSTTS=0x20, GPLT0=0x60, SPT0=0x48, etc.).
- CHR RAM mirrors at 0x78000–0x7FFFF (were incorrectly mapping to BGMap).
- BGMap base derived from HEAD bits 3:0 (was incorrectly from PARAM).
- `is_vram_addr` accepts addresses >0x60000 (was silently dropping writes).
- GX is 10-bit signed (sign-extend from bit 9).
- OBJ word format: word0=X, word1=L/R/parallax, word2=Y, word3=pal/flip/char.
- Param table offset uses `param_base * 2` (was masking with 0xFFF0).
- ROM address mirroring normalized to `[rom_base, 0x08000000)` — required for ROMs >512 KB.
- **0x3DC00-0x3DFFF is the column table (CTA), not a mirror of the world attributes.**
  It was folded onto 0x3D800, so every game that writes a column table — 512 halfwords,
  typically `0x00FE` — wiped all 32 world attributes and drew nothing. This is the single
  correction that unblocked the most games at once.
- The VIP version register (0x44) reads 2; the register block is 0x80 bytes mirrored through
  0x5F800-0x5FFFF, and the dispatch masks the offset with `& 0x7E`.
- `SDLR`/`SDHR` report the **current** pad rather than a snapshot taken when the game last wrote
  `SCR_START`. The VB can also read the pad on its own; a game relying on that never writes
  START, and every button read returned zero for the whole run.

### Open problem: block-based rendering

The VB renders 8 rows at a time, but our renderer draws all 224 rows at once. Games that swap CHR tiles mid-frame (e.g. Mario's Tennis during gameplay) get garbled upper backgrounds. The VRAM data is confirmed correct; the issue is render timing. Block-based mid-frame rendering is the planned fix and would unblock several games at once.

## Runtime memory & interrupts

- CPU state lives in the global `vb_cpu` (`vb_cpu.r[]`, `vb_cpu.sr[]`).
- Memory access goes through `vb_mem_read{8,16,32}` / `vb_mem_write{8,16,32}`.
- Interrupt handlers are registered with `vb_interrupt_set_handler(level, fn)`; the VB levels are Game Pad (0), Timer (1), Expansion (2), Communication (3), VIP (4).
- A per-frame callback drives rendering and input. The shared
  [`games/driver/main.c`](../games/driver/main.c) implements it for every game; five older
  titles still have hand-written glue in `games/<name>/src/main.c`.
- A "frame" is `VBRECOMP_FRAME_TICKS` calls to `vb_interrupt_check()` (20,000 by default),
  **not** a hardware frame. That function is emitted at every function entry, so a change to
  how the ROM is carved into functions changes the effective frame rate — see the pacing note
  in [RENDER_TRIAGE.md](RENDER_TRIAGE.md) before reading any timing-sensitive measurement.

## Where the work happens now

Per-game glue was the original model and is now the exception. The generic driver boots any
recompiled ROM with no game-specific code, and the corpus scripts build and probe all 76 at
once — see [GETTING_STARTED.md](GETTING_STARTED.md) to reproduce, and
[RENDER_TRIAGE.md](RENDER_TRIAGE.md) for what each remaining failure is stuck on.

## Stretch goals

- VR headset support — the VB was *meant* for this, it just arrived 25 years early.
- Stereoscopic 3D on modern hardware.
- Block-based VIP rendering for accurate mid-frame tile timing.
