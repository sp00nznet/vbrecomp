# Contributing

Contributions are welcome, especially fixes that move a game in
[COMPATIBILITY.md](COMPATIBILITY.md) or the triage in
[docs/RENDER_TRIAGE.md](docs/RENDER_TRIAGE.md). The best fixes here are shared
ones: a recompiler or VIP bug found in one game and measured across the whole
corpus.

## What never goes in a PR

- ROMs, or anything extracted from one (graphics, audio, tables, WRAM dumps).
- Recompiler output (`corpus/`, `games/*/generated/`), disassembly listings, or
  symbol maps generated from a ROM. The tool ships; its output stays on your
  machine.
- Code from proprietary SDKs or leaked material.

## Where your code comes from

This project is MIT. Contributions must be your own work or under an
MIT-compatible licence. The usual way that goes wrong is porting a fix you saw in
a GPL emulator, which relicenses it by accident and is very hard to untangle
later. For the Virtual Boy that means **Mednafen** (and its libretro/Beetle VB
core and BizHawk's copy of it), **Red Viper**, **VBjin** and anything derived
from them. Reading hardware documentation and running those emulators as a black
box to compare output is fine (that is what `scripts/reference/` does); copying
or paraphrasing their code is not. If a change came from somewhere, say where in
the PR.

## AI-assisted contributions

Welcome, provided a human understood and verified the change. "The model wrote
it" is not a reason it is right; the measurements are.

## Before you open a PR

1. Build (README, "Building from source").
2. `python tools/conformance.py` must still print `77/77 cases pass` (or more,
   if you added cases).
3. If you touched the recompiler or the runtime: `scripts/regress.ps1` against
   its baseline, and say which games moved. A change that helps one game and
   costs another is still useful to know about; say so.
