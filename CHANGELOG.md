# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow SemVer once the first one is tagged. Nothing is tagged yet; the
history before this file was started (2026-10-21) is summarised, not itemised.
`git log` and [docs/RENDER_TRIAGE.md](docs/RENDER_TRIAGE.md) hold the detail.

## [Unreleased]

### Added
- `scripts/greedy_discover.py`: discovery that keeps a harvested entry only if
  the game's screens do not get worse, and writes rejected ones commented out.
- Heartbeat diagnostic: the first time SP leaves WRAM, print it with the
  recent function trail (3-D Tetris climbs past `0x05010000`).
- `--headless`, `--record out.mp4` and `--frames N` on the generic driver, the
  house-style flags shared with snesrecomp. They set the existing
  `VBRECOMP_HEADLESS*` knobs; `--record` pipes frames to ffmpeg at 50.27 fps.
- `Setup.cmd` / `scripts/setup.ps1`: the Quick start. Checks prerequisites, asks
  before installing, takes your ROM, runs GETTING_STARTED steps 1-5 and leaves a
  `Play <game>.cmd` launcher.
- CI (`.github/workflows/build.yml`): builds the library and recompiler, runs
  `test_mem` and the ROM-free instruction conformance suite on every push and PR.
- netlab recipe (`recomp-netlab/projects/vbrecomp.env`): builds on the clang-cl
  builder, QA runs `test_mem` on a test VM.
- `NOTICE`, `LICENSES/`, `CHANGELOG.md`, `ROADMAP.md`, `CONTRIBUTING.md`.

### Changed
- `VBRECOMP_PRESS` takes a comma-separated list of masks, pressed one per burst
  in turn, and every script now defaults to `1000,0004` (Start, then A) instead
  of `303C`. Holding A+B+L+R+Start+Select together is a soft-reset combo: 3-D
  Tetris and Teleroboxer restarted to their warning screen on every burst.
  `scripts/reference/shot.lua` presses the same buttons.
- Game targets copy `SDL2.dll` beside the executable after linking, so the
  scripts no longer copy it (the copy failed while any game was running, and
  vcpkg's own deploy step needs PowerShell 7).

### Fixed
- Interrupt handlers cannot unwind through the code they interrupted: the
  dispatcher raises a frame floor (`vb_recomp_frame_floor`) to the interrupted
  depth while a handler runs. Panic Bomber's timer handler unwound through the
  main loop, never reached RETI, and left the game running with EP set.
  Virtual Lab reaches Level 01; Mario Clash, Red Alarm, Teleroboxer and
  Vertical Force (USA) get further. No title got worse.
- A call whose callee ends in a tail-transfer chain that jumps back to the
  call's own return address now RETURNS (`vb_recomp_drain_to`). It used to be
  dispatched as a new entry, so the caller's epilogue ran twice: 3-D Tetris's
  timer stub lost 8 bytes of SP per interrupt until SP left WRAM, StarFox's
  116. Mario's Tennis now plays a match and Space Invaders advances; Mario
  Clash regressed (it calls interior targets with no entry, now visible).
- The emitter writes a stub for a declared function it declined to emit.
  Waterworld's harvested `07FFFFFE` (the last halfword of ROM) was declared
  and put in the function table but never defined, and the link failed.
- `scripts/discover.ps1` only harvests halfword-aligned cartridge ROM
  addresses (not WRAM, low memory or garbage), and treats a commented-out
  `# entry X` as known so a deliberately removed entry is not harvested back
  (it re-added Galactic Pinball's `07F000AC`).
- `DPSTTS` bit 6 is SCANRDY and now stays set; FCLK (bit 7) pulses instead. Golf
  and T&E Virtual Golf waited for bit 6 to hold for 20,000 timer ticks and never
  left their warning screen.
- `movea ..., r31` + `jmp [rN]` with r31 = the next instruction is emitted as an
  indirect call (`VB_CALLI`), not a table transfer. Golf's state dispatcher
  nested a C frame per state change and overflowed the stack.
- `scripts/sweep.ps1` passes each game's committed `--hints` file and uses the
  Release recompiler; a full sweep used to drop every discovered entry point.
- `v810recomp` creates its output directory (and parents). On a clean checkout
  `corpus/` does not exist, so GETTING_STARTED step 4 failed with
  `cannot open '.../recomp_funcs.c' for writing`.
- `test_mem` used register offsets from the original skeleton (TLR at 0x14,
  SDLR at 0x2C); the runtime uses the hardware ones (TLR 0x18, THR 0x1C,
  TCR 0x20, SDLR 0x10, SDHR 0x14), so two of its seven tests always failed.
- The build without SDL2 failed (`SDL.h` not found): `src/screenshot.c` was
  compiled unconditionally. It is now built only with SDL2, as the docs say.

### Removed
- `.claude/settings.local.json` from the index (local tool settings, never meant
  to be committed).

## Before 2026-10-21 (summary)

- **Recompiler** (`tools/v810recomp`): all 76 ROMs in the corpus recompile and
  compile. Long-call idioms (PC capture, inline arguments), jump tables, context
  resumes after a stack switch, ROM-mirror normalisation, profile-guided entry
  hints (`scripts/discover.ps1`, `games/hints/`).
- **Runtime**: VIP world clipping for worlds wider than the screen and negative
  origins, VER register, register mirroring, column table mapping, packed 2bpp
  direct frame buffer, level-triggered VIP interrupts, live pad reads.
- **Harnesses**: `tools/conformance.py` (77 V810 encodings, no ROM),
  `scripts/regress.ps1` (fixed game panel against a baseline),
  `scripts/render_survey.ps1`, and hardware comparison against BizHawk run as a
  black box (`scripts/reference/`).
