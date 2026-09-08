# vbrecomp

**Static recompilation toolkit for Virtual Boy games — recompile the entire library.**

Remember the Virtual Boy? Nintendo's red-and-black fever dream from 1995? We're bringing those games back as native executables — no neck strain, no retina damage required.

![Mario's Tennis title screen running via vbrecomp](screenshots/mariotennis_title.png)

## What is this?

vbrecomp statically recompiles Virtual Boy ROMs into native C, then links that C against a small hardware runtime (CPU state, VIP video, VSU audio, timers, input). Think [N64Recomp](https://github.com/N64Recomp/N64Recomp), but for Nintendo's most misunderstood console.

The retail Virtual Boy library is only 22 games. That's small enough to recompile *all of them* — and that goal drives the whole project. Rather than a separate repo per game, **everything lives here**: the recompiler, the runtime, and every game, so improvements to the tool immediately benefit the whole set.

## How it works

```
  ROM (.vb)
     │
     ▼
  ┌──────────────────┐   discovers functions via control-flow analysis,
  │  v810recomp      │   translates each V810 routine to a C function
  │  (tools/)        │   (vb_func_<addr>), resolves jumps, jump tables
  └──────────────────┘
     │  generated/recomp_funcs.c  (human-readable C, see below)
     ▼
  ┌──────────────────┐   per-game src/main.c wires up entry point,
  │  vbrecomp runtime│   interrupt handlers, and the frame loop, then
  │  (src/, include/)│   links the generated C against the runtime:
  └──────────────────┘     CPU · VIP · VSU · timers · input · SDL2
     │
     ▼
  native game executable
```

The recompiled output is meant to be **read**, not just run. Flag math is factored into named helpers, every line carries its original address and disassembly, and functions get header comments:

```c
/* sub_07F80026: 0x07F80026-0x07F8004A (36 bytes), IRQ level 2 handler */
void vb_func_07F80026(void) {
    /* 07F80026: add  -4, r3 */
    vb_cpu.r[3] = vb_add(vb_cpu.r[3], 0xFFFFFFFC);   /* r3 = sp */
    /* 07F80028: st.w r31, 0[r3] */
    vb_mem_write32(vb_cpu.r[3] + (int32_t)(int16_t)0x0000, vb_cpu.r[31]);
    ...
```

## The plan: corpus-driven hardening

Every ROM in the library is a test case. The recompiler is only as good as the worst ROM it chokes on, so we run it against **all of them** and treat each failure as a concrete bug:

1. **Recompile everything** — `scripts/sweep.ps1` runs `v810recomp` on every ROM and syntax-checks the generated C with MSVC. Output: [`STATUS.md`](STATUS.md) (auto-generated compile matrix) + `corpus/results.json`.
2. **Fix what breaks** — each ROM that fails to recompile or compile is a decoder/analysis/codegen gap to fix in `tools/v810recomp`.
3. **Boot & render** — bring games up one at a time, factoring common patterns (frame sync, decompressors) into shared helpers so each new game is less manual.
4. **Cross-validate** — an independent Ghidra V810 disassembly is diffed against our function table to catch missed functions, data-misparsed-as-code, and boundary disagreements, and to source real function names. See [`docs/RECOMPILER.md`](docs/RECOMPILER.md#cross-validation).

Progress is tracked in [`COMPATIBILITY.md`](COMPATIBILITY.md), updated as we go.

## Status

- **76 / 76 ROMs** (retail + protos + homebrew) recompile and compile clean.
- **65 / 76 games render on screen** and **39 are advancing** (the frame changes between samples, i.e. the game is running) through the **generic driver** (`games/driver/main.c`) -- no game-specific glue: the recompiler emits the discovered interrupt handlers + reset vector, and the driver wires them up. Five of the 11 that draw nothing are blank on real hardware too -- the three Snowball Wars demos, plus Capitan Sevilla 3D and Null Pointer Mockup, both measured with and without input -- so the honest denominator is 71.
- Getting there took repeated passes of the same loop: find a shared correctness bug, fix it once, measure the whole corpus. Recent ones:
  - **The column table was mapped onto the world attributes.** `0x3DC00-0x3DFFF` is the VIP's per-column brightness ramp, not a mirror of `0x3D800`, so every game that wrote one wiped all 32 worlds with it. StarFox had loaded 7,350 CHR bytes and set up a 380x224 world, then read it back as `GX=254 GY=254` -- 254 being `0x00FE`, the column value landing on top of it.
  - **The VIP's VER register reads back 2, and games test it.** Hyper Fighting (Beta) spun on `andi 0x0002` of `0x0005F844` forever -- 14.4 million CHR writes in 400 frames -- because an unimplemented register returned 0. VIP registers also mirror every 0x80 bytes, which we were not decoding.
  - **`jmp [r31]` after a stack switch is a context resume, not a return.** Ordinary code only adds to `r3`; replacing it wholesale from memory is either a task switch or a save/restore pair, told apart by whether a matching `st.w r3` exists. Jack Bros' cooperative scheduler resumes tasks this way and we were emitting a plain return, so its tasks were created and never ran.
  - **A `jal` off the top of ROM wraps within the CART**, which repeats every `rom_size` bytes -- not to the bottom of the 27-bit map. Hyper Fighting calls `0x08000290` from `0x07C20906`, one mirror period past its 4 MB ROM at `0x07C00000`; masking gave `0x00000290`, inside VIP memory, and seven calls in its boot were dropped.
  - **`andi imm, reg, r0` is the V810 TEST idiom** -- the result is discarded, the flags are not. Skipping it because the destination is `r0` left the next branch reading stale flags; 3-D Tetris's VIP handler tested FRAMESTART that way and drew nothing at all.
  - **`jal <next instruction>` is not a call**, it is the compiler's PC-capture idiom for a long call; emitting it as one ran the following instructions twice with `r31` already advanced.
  - **A drained tail target is jumped to, not called**, so raising the call depth for it made the target's own `jmp [r31]` read as a return and unwound the transfer chain.
  - **The VIP interrupt line is level-triggered** on `INTPND & INTENB`; requesting it once per frame dropped every source but the first a handler happened to service.
  - **Profile-guided function discovery** (`scripts/discover.ps1`): the runtime counts jump targets it could not resolve, those are fed back as `entry` hints, and the loop repeats to a fixpoint. Hints are committed per game under `games/hints/`.
- Earlier passes in the same style, kept because the reasoning still applies:
  - **ROM-mirror jump normalization** — reset vectors built as low-mirror (`movhi 0x0700`) or `jmp [r31]` computed jumps were mistaken for returns, so the entry never ran (dead boot, SP=0). Fixing both took the most games from "boots dead" to "boots."
  - **VIP status-register phase cycling** (DPSTTS/XPSTTS) — games poll these for a specific display/drawing phase; cycling through each phase (instead of a 2-state toggle) lets their frame-sync loops terminate.
  - **V810 interrupt-priority masking** was inverted (it masked the high-priority VIP interrupt whenever a game raised `PSW.I`); fixed to accept `level ≥ PSW.I`.
  - **State-machine jump-table detection** — the ubiquitous `ld.w table[idx]; … jmp [rN]` dispatch (with the continuation loaded into `r31` between) and low-mirror table entries are now resolved, so game main-loop state machines dispatch correctly.
  - **Interrupt-handler discovery** for push-prologue vector stubs, plus a runtime guard so a missing handler can't wedge the CPU with `EP` stuck set.
- A reusable **`rename` recompiler hint** lets a hand-written `src/main.c` intercept specific functions (HLE), used by the **Red Alarm** and **Mario's Tennis** custom drivers.

Full per-game state: [`COMPATIBILITY.md`](COMPATIBILITY.md). What each non-rendering game is actually stuck on, with the screenshot evidence: [`docs/RENDER_TRIAGE.md`](docs/RENDER_TRIAGE.md). Detailed fix notes: [`docs/RECOMPILER.md`](docs/RECOMPILER.md).

## Building & running

Requires CMake, a C/C++ compiler (MSVC on Windows), and SDL2 (via vcpkg).

```powershell
# Configure (point at your vcpkg SDL2). -T host=x64 is not optional: the
# generated C for the largest ROMs is one translation unit of several hundred
# thousand lines, and the 32-bit hosted compiler CMake picks by default runs
# out of address space on it ("C1060: compiler is out of heap space").
cmake -S . -B build -T host=x64 -DSDL2_DIR="C:/vcpkg/installed/x64-windows/share/sdl2"

# Build the recompiler + runtime + all games
cmake --build build --config Release

# Run a game (ROMs are not shipped — supply your own .vb)
./build/games/Release/galacticpinball.exe "path/to/Galactic Pinball.vb"
```

Recompile a ROM yourself:

```powershell
./build/Release/v810recomp.exe "game.vb" out_dir [--hints game.hints.txt]
```

**Headless mode** (CI / display-less boxes): set `VBRECOMP_HEADLESS=1` (and optionally `VBRECOMP_HEADLESS_FRAMES=N`) to run the game loop without a window; it still renders into its framebuffer and can write PNG screenshots.

The full corpus sweep:

```powershell
./scripts/extract_roms.ps1     # extract ROMs -> roms/  (gitignored)
./scripts/sweep.ps1            # recompile + syntax-check all -> STATUS.md
./scripts/render_survey.ps1    # boot every game headless, classify what it drew
```

**Instruction conformance** — the one check that needs no ROM, so it runs on a clean checkout:

```powershell
python tools/conformance.py
```

Each case is a V810 encoding written down in the tool, assembled into a synthetic ROM, put through the real recompiler and runtime, and compared against the architecture manual. It is what turns "this game stalls" into "this instruction is wrong" — see [`docs/RECOMPILER.md`](docs/RECOMPILER.md#instruction-conformance).

## Repository layout

```
include/vbrecomp/   public runtime API (cpu, mem, vip, vsu, timer, gamepad, ...)
src/                runtime implementation + SDL2 platform + ImGui menu
tools/v810recomp/   the static recompiler (decode, analyze, emit)
games/<name>/       per-game: generated/ C, src/main.c glue, hints, screenshots
scripts/            extract_roms.ps1, sweep.ps1
docs/               architecture & recompiler internals
COMPATIBILITY.md    living per-game status
STATUS.md           auto-generated compile matrix (from sweep.ps1)
```

## Documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — Virtual Boy hardware, memory map, the VIP renderer, validated technical fixes.
- [`docs/RECOMPILER.md`](docs/RECOMPILER.md) — how `v810recomp` works, the hints format, the readability design, the corpus workflow, and Ghidra cross-validation.

## Credits & dependencies

- [SDL2](https://www.libsdl.org/) — windowing, input, audio.
- [Dear ImGui](https://github.com/ocornut/imgui) — menu bar and toasts (via [sp00nznet/tools](https://github.com/sp00nznet/tools)).
- [stb_image_write](https://github.com/nothings/stb) — screenshots.
- [rustual-boy](https://github.com/emu-rs/rustual-boy) — reference for VIP/VSU behavior (architecture reference only; all vbrecomp code is original).
- [Ghidra_v810_v830](https://github.com/20Enderdude20/Ghidra_v810_v830) by 20Enderdude20 — V810 processor module used for independent cross-validation.

## License

MIT — do whatever you want with it.
