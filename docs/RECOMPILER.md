# The v810recomp recompiler

`tools/v810recomp` translates a Virtual Boy ROM into C. It is a static (ahead-of-time) recompiler: it discovers code, decodes V810 instructions, and emits one C function per discovered routine.

## Pipeline

1. **Load & header** (`main.c`) — read the ROM, print the header, normalize mirrored addresses.
2. **Entry discovery** (`main.c`) — decode the reset vector (`0xFFFFFFF0`, a MOVHI/MOVEA/JMP stub) to find the real entry point. The reset vector itself is always emitted as a function (`vb_func_07FFFFF0`) since the runtime calls it to boot. Interrupt handlers are found by decoding the vector stubs near the end of ROM.
3. **Analysis** (`analyze.c`) — recursive control-flow walk from every known entry. Follows JAL (calls), JR/Bcond (branches), and resolves `jmp [reg]` via constant propagation (MOVHI/MOVEA → register value). Builds the function table, jump tables, and resolved-jump list.
4. **Emit** (`emit.c`) — for each confirmed function, a two-pass walk: collect intra-function branch targets as labels, then emit labelled C. Produces `recomp_funcs.c` and `recomp_funcs.h`.

## Control-flow translation

- **JAL** → `vb_cpu.r[31] = <return>; vb_func_<target>();`
- **JR / Bcond** within the function → `goto label_<addr>;`
- **JR / Bcond** to another function → tail call `vb_func_<target>(); return;`
- **JMP [reg]** resolved to a known function → tail call.
- **JMP [reg]** resolved to an intra-function label → `goto`.
- **Jump tables** → `switch` over the register, one `case` per entry.

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

Example (Galactic Pinball): `vb_func_07F417B8` is a graphics decompressor whose JAL is followed by 8 bytes (src + dst pointers) at 31 call sites — handled with `skip 07F417B8 8`.

## The corpus workflow

The recompiler is hardened against the entire ROM library, not just the games in active development.

```powershell
./scripts/extract_roms.ps1     # extract every ROM -> roms/  (gitignored)
./scripts/sweep.ps1            # recompile + MSVC /Zs syntax-check each
```

`sweep.ps1` writes [`../STATUS.md`](../STATUS.md) (a compile matrix: recomp ok? function count? compiles clean? first error) and `corpus/results.json`. It runs sequentially with per-ROM timeouts. A ROM that recompiles but yields a suspiciously low function count (e.g. 0) signals an analysis gap worth chasing — these are the next targets.

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
