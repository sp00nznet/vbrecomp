# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow SemVer once the first one is tagged. Nothing is tagged yet; the
history before this file was started (2026-10-21) is summarised, not itemised.
`git log` and [docs/RENDER_TRIAGE.md](docs/RENDER_TRIAGE.md) hold the detail.

## [Unreleased]

### Added
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

### Fixed
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
