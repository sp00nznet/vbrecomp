# The v810recomp recompiler

`tools/v810recomp` translates a Virtual Boy ROM into C. It is a static (ahead-of-time) recompiler: it discovers code, decodes V810 instructions, and emits one C function per discovered routine.

## Pipeline

1. **Load & header** (`main.c`) — read the ROM, print the header, normalize mirrored addresses.
2. **Entry discovery** (`main.c`) — decode the reset vector (`0xFFFFFFF0`, a MOVHI/MOVEA/JMP stub) to find the real entry point. The reset vector itself is always emitted as a function (`vb_func_07FFFFF0`) since the runtime calls it to boot. Interrupt handlers are found by decoding the vector stubs near the end of ROM.
3. **Analysis** (`analyze.c`) — recursive control-flow walk from every known entry. Follows JAL (calls), JR/Bcond (branches), and resolves `jmp [reg]` via constant propagation (MOVHI/MOVEA → register value). Builds the function table, jump tables, and resolved-jump list.
4. **Emit** (`emit.c`) — for each confirmed function, a two-pass walk: collect intra-function branch targets as labels, then emit labelled C. Produces `recomp_funcs.c` and `recomp_funcs.h`.

## Control-flow translation

A guest routine becomes one C function, but guest control flow is not a call
tree: a main loop is a **cycle**, and a state handler ends by jumping back to
its dispatcher. Translating that as a call is unbounded recursion; translating
it as a return drops the loop after one state. So the model splits in two:

- **`jal` is the only call.** `VB_CALL(vb_func_<target>, <addr>)` — records the
  frame, runs the callee, then drains (below).
- **Everything else is a transfer.** `VB_JUMP(<addr>)` parks the target in
  `vb_recomp_tail_target` and returns; the parked target is dispatched by the
  **drain** at every call site, so a chain of transfers runs iteratively at a
  flat C stack depth instead of nesting. A drained target is dispatched without
  raising the call depth, because it was jumped to, not called — otherwise its
  own `jmp [r31]` reads as a return and the chain unwinds early.

Within those two:

- **JR / Bcond** inside the function → `goto label_<addr>;`
- **JR / Bcond** out of the function → `VB_JUMP(<addr>);`
- **`jmp [reg]`** resolved by constant propagation → `goto` for a local label,
  `VB_JUMP` otherwise.
- **`jmp [r31]`** → `VB_RET()`, which is a return only while a `jal` frame is
  open; a function entered by a transfer has no caller, so at depth 0 its r31
  holds a continuation and the instruction is a jump.
- **Jump tables** → `switch` over the register, one `case` per entry.
- **Unresolved `jmp [reg]`** → dispatched at run time through a sorted
  `addr -> vb_func_*` table at the bottom of `recomp_funcs.c`.

### Transfers into the middle of a function

An exact table match only finds function *heads*, and most of what a game
computes at run time is an address inside one. Those used to be dropped — Mario
Clash discards 128,699 of them in a 600-frame run, all four hot targets interior
to a known function. On a table miss the dispatcher now parks the address in
`vb_recomp_entry` and calls the enclosing function, whose **entry dispatch** (a
`switch` at the bottom over the labels it actually declared) turns it into a
`goto`. The switch is the range check: an address that is not one of its labels
still falls through to being counted, exactly as before.

Two things this must not do, both learned by breaking them:

- The parked entry is consumed **before** `vb_interrupt_check()`, which can run
  a whole interrupt handler whose functions would otherwise see and discard it.
- It never routes while `PSW.EP` is set. Diverting a handler mid-flight abandons
  its C frame, so it never reaches `RETI` and `EP` then blocks every later
  interrupt.

### Function boundaries

A branch into the middle of an existing function **splits** it, so the target
can be its own entry. A split point must be a real instruction boundary: the
analyzer walks the function to check, stepping over inline call arguments on the
way. Cutting inside those arguments makes a function out of data — in Galactic
Pinball the first halfword of one such argument block, `0x9800`, decodes as
`bp .`, a branch to itself that the preceding function fell into and spun in for
the whole run.

### Resolved-jump targets become functions

A computed `jmp [reg]` never returns to the original flow, so its target is the head of its own routine. The analyzer promotes such targets to standalone, tail-called functions (the same treatment JAL callees and `jmp` hints get). Folding them into the current function instead produced **overlapping function ranges**, where a `goto` referenced a label defined only in a different emitted function — an undefined-label compile error. Promotion keeps every function self-contained.

## Readability design

The generated C is meant to be read. Two choices make that work:

- **Every line is annotated** with its original address and disassembly (`/* 07F80026: add -4, r3 */`), and each function has a header comment with its address range and role.
- **Flag math is factored into named helpers** in [`include/vbrecomp/cpu.h`](../include/vbrecomp/cpu.h) instead of inlined. The recompiler emits `vb_cpu.r[3] = vb_add(vb_cpu.r[3], 0xFFFFFFFC);` rather than an 8-line PSW block. Helpers: `vb_add`, `vb_sub`, `vb_cmp`, `vb_setf_logic`, `vb_shl`, `vb_shr`, `vb_sar`, `vb_mul`, `vb_mulu`, `vb_div`, `vb_divu`. Each updates the PSW exactly as hardware does.

This shrinks generated files ~3–5× and turns flag soup into a legible transliteration of the assembly.

## Hints

Some control flow can't be resolved statically (function pointers stored in WRAM and dispatched later, inline data after a call). A per-game `<game>.hints.txt` supplies the missing pieces. One directive per line, `#` for comments:

| Directive | Meaning |
|-----------|---------|
| `jmp <from> <to>` | Resolve the indirect `jmp [reg]` at `<from>` to absolute `<to>` (and register `<to>` as a function). |
| `entry <addr>` | Add a function entry point the static analyzer can't reach. |
| `skip <target> <bytes>` | `<target>` is an "inline-data-after-JAL" helper that consumes `<bytes>` of data immediately following each call site; don't decode those bytes as code. |

**`skip` is now detected automatically** and rarely needs writing down. The
long-call idiom reads its arguments inline through r31 and returns past them,
so the `add N, r31` immediately before `jmp [r31]` names the argument size
exactly; a scan over `jal` targets registers them before analysis, because the
trace needs the amount while walking the *callers*. Galactic Pinball's
`vb_func_07F417B8` — 8 bytes of source/destination pointers after each call —
was the case that motivated the hint and is now found without one.

## The corpus workflow

The recompiler is hardened against the entire ROM library, not just the games in active development.

```powershell
./scripts/extract_roms.ps1     # extract every ROM -> roms/  (gitignored)
./scripts/sweep.ps1            # recompile + MSVC /Zs syntax-check each
```

`sweep.ps1` writes [`../STATUS.md`](../STATUS.md) (a compile matrix: recomp ok? function count? compiles clean? first error) and `corpus/results.json`. It runs sequentially with per-ROM timeouts. A ROM that recompiles but yields a suspiciously low function count (e.g. 0) signals an analysis gap worth chasing — these are the next targets.

## Instruction conformance

The corpus workflow tells you *which ROM* broke; it cannot tell you *which instruction*. `tools/conformance.py` closes that gap, and it is the only check here that runs on a clean checkout — it needs no ROM at all.

```powershell
python tools/conformance.py             # run every case
python tools/conformance.py --list      # show what each encoding decodes to
python tools/conformance.py -k mul      # only cases whose name matches
```

Each case is a short V810 body written as encodings in the tool itself, assembled into a synthetic 64 KB ROM (real header, real reset vector, real interrupt-vector layout), recompiled by the actual `v810recomp`, compiled by MSVC, and executed against the runtime built from the working tree. So a pass means the whole path — decode, analyse, emit, compile, run — agrees with the V810 manual for that instruction, not just that some model of it does.

Two design points matter:

- **Expectations come from the architecture manual, not from `cpu.h`.** A case that restates what the runtime already does passes forever and pins nothing. Three of the original cases were wrong in exactly that way and were corrected against the manual rather than against our output.
- **Each body runs under SEH.** A translation that faults on the host — an `idiv` overflow where the V810 completes the instruction — reports itself as a failed case instead of taking the run down and hiding everything after it. That is how `div: INT_MIN / -1` was found.

Cases are grouped by what they pin: flag semantics (`add`/`sub`/`cmp` and the CY-vs-OV distinction), immediate sign- vs zero-extension, shift counts and the carry-out, the 64-bit multiply/divide register pair, every `setf` condition code, load sign-extension, the float unit, and the bit-string block copy.

## Headless validation

Set `VBRECOMP_HEADLESS=1` (optionally `VBRECOMP_HEADLESS_FRAMES=N`) to run a game with no window/renderer. The recompiled loop still executes and renders into its framebuffer, and games can write PNG screenshots via stb. This is how boot/render behavior is validated on CI or display-less machines.

## Cross-validation

Independent disassembly is a powerful check on the recompiler's own analysis. Neither IDA Pro nor stock Ghidra ships a native V810 (only the related V850), so we use the [Ghidra_v810_v830](https://github.com/20Enderdude20/Ghidra_v810_v830) processor module by **20Enderdude20** (credit to them).

The approach (mirroring the IDA recomp-toolkit pattern):

1. Load the ROM in Ghidra at the recompiler's true memory layout so addresses line up.
2. Let Ghidra analyze independently.
3. **Diff** Ghidra's function set against our table to surface:
   - functions Ghidra found that we **missed** (e.g. ROMs the sweep reports with too few functions),
   - "functions" we emit that are actually **data**,
   - **boundary** disagreements.
4. Decompile high-value functions to aligned C as a naming/porting reference, feeding real names into a per-game symbol map.

## Source layout

| File | Responsibility |
|------|----------------|
| `decode.c` | V810 instruction decoder + disassembler |
| `analyze.c` | Control-flow analysis, function/jump-table discovery |
| `emit.c` | C code generation |
| `main.c` | Driver: load, entry/IRQ discovery, hints, orchestration |
| `v810recomp.h` | Shared context types |
