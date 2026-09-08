# Render triage

What the whole corpus draws through the generic driver, and the categories the
failures fall into. Regenerate with:

```powershell
./scripts/render_survey.ps1              # numbers + corpus/_timeline/<slug>/
./scripts/render_survey.ps1 -Diagnose    # + heartbeat detail, ~10x slower
./scripts/render_survey.ps1 -Press ""    # survey with no input injected
```

Each ROM boots headless for 900 frames and is sampled at frames 300, 600 and
900. The verdict is read off those screenshots (a blank 384x224 frame is an
all-black PNG of exactly 3,619 bytes), not off a debug counter — so it does not
depend on running the game slowly enough for the heartbeat to print, which is
what used to make this survey disagree with `build_all.ps1`.

## Where the corpus stands

| | Count | What it means |
|---|---|---|
| Advancing | 28 | The frame changes between samples: the game is running. |
| Drawing, then frozen | 13 | One image, byte-identical at 300/600/900. |
| Nothing on screen | 34 | Every sample blank; no character data ever written. |

"Renders something" is 41/76, but that number is the less useful of the two:
a game frozen on a correctly-drawn warning screen counts as rendering and is
not running. **Advancing** is the number to drive.

## Categories

### 1. Nobody was pressing anything — resolved, 6 → 27 advancing

The retail set gates its health-warning screen on a button. Waterworld's own
boot code reads the pad through `sub_07E00C96` (write `0x84` to SCR, spin on
SCR bit 1, read SDLR/SDHR), masks the result with `0x303C` — `VB_BTN_A | B |
RT | LT | STA | SEL` — and waits for a *rising edge*:

```
07E00FCC: ld.h  40[r3], r10    ; previous sample
07E00FD0: andi  0x303C, r10, r10
07E00FD4: bz    0x07E00FDA     ; only if nothing was held before
07E00FDA: ld.h  36[r3], r10    ; current sample
07E00FDE: andi  0x303C, r10, r10
07E00FE2: bgt   0x07E00FE8     ; ...and something is held now
```

A headless run pressing nothing never produces that edge. This is not a
recompiler fault — the game is correctly waiting for a player. The survey now
holds `0x303C` in bursts (`VBRECOMP_PRESS`, `-Press`), which took the corpus
from 6 games advancing to 27, and Waterworld from a frozen warning screen
through its auto-pause option screen into the round HUD.

The earlier suspicion that this was the VIP interrupt path was wrong: the VIP
handler fires (300+ times per run, no SP leak), and the two WRAM addresses the
game reads two million times are its own pad-state slots, not an interrupt flag.

### 2. Mario Clash: SCR's start bit had to read back set — resolved

Two idioms arm a pad read, and only one survives a register that reads back
what was stored:

```
Waterworld:   ld.b SCR; ori  0x0084; st.b SCR   -- sets the start bit
Mario Clash:  ld.h SCR; andi 0x0084; st.h SCR   -- keeps the start bit
```

Mario Clash's read-modify-write only arms if `SCR_START` is already there. Its
init leaves SCR at `0x80`, so `0x80 & 0x84` wrote `0x80` forever and the buttons
were never latched — it read zero 161,598 times a run while its 64-entry jump
table sat at state 2 testing `pad & 0x303C0000`. SCR now reads back with the
start bit set; Mario Clash reaches its OPTION / AUTOMATIC PAUSE screen and
Waterworld, which never depended on the read-back, is unchanged.

`VBRECOMP_PEEK=<hex>[,<hex>...]` was added for this: the hot-read list gives the
address and `VBRECOMP_WATCH` gives the reader, but for a state variable the
*value* is the question.

### 3. movbsu wrote to address zero — resolved

The bit-string block copy takes its addresses in r29/r30 and its bit offsets in
r26/r27. The first implementation had them inverted, so every blit read and
wrote near address zero: scribbling on the left framebuffer while the real
destination stayed empty — worse than the no-op it replaced. Golf makes 1.2
million trips through its copy helper (`sub_07E0E4D0`, a `movbsu` wrapper) and
still reported `CHR=0/0`.

The conformance cases had passed while agreeing with the same wrong belief, so
the assignment now comes from ROM: Golf loads `r29 = r6; r26 = (r6 & 3) << 3`
and `r30 = r7; r27 = (r7 & 3) << 3`, pairing each address with the offset from
its own low two bits, and Ballface blits with a word-aligned source (`r27 = 0`)
and a variable destination bit position (`r26 = r5`), a glyph's x.

### 4. The entry returns instead of looping — 13 games (was 16)

A game's main loop is a control-flow CYCLE, and the recompiler used to translate
it as call/return. Mario's Tennis was the clean specimen: `sub_07F801B2` ends in
a 64-entry jump-table dispatch, and each state handler ends `jr 0x07F801F4` back
to that dispatcher. Called, that is unbounded mutual recursion; returned, the
loop is dropped and the boot unwinds after one state — which is why these ROMs
print "Reset returned after 0–9 frames" and then run no game code at all.

**Fixed in the tail-jump model (merged).** A `jal` is a call; every other
transfer — `jr`, `jmp [reg]`, a branch leaving the function, a jump-table case —
is a jump that parks its target in `vb_recomp_tail_target` and returns, and
every call site drains the chain so the transfer completes before the caller
resumes. That took the corpus from 41 rendering to 45 and unblocked Mario's
Tennis, Golf and Virtual Bowling.

Thirteen games still reach this line, and they do **not** share a cause. The
execution trace (`vb_recomp_trace`, dumped by the driver when the reset path
returns — the last 64 function entries) gives each one's terminal function:

| Game | depth | last three functions |
|---|---|---|
| StarFox | 10 | `07FE64D0 → 07FE64E4 → 07FE0300` |
| Snatcher | 12 | `07E02698 → 07E026FA → 07E01058` |
| Mario Combat | 12 | `07E02400 → 07E02478 → 07E00E08` |
| Fishbone | 10 | `07E0234C → 07E0238A → 07E01154` |
| Hyper Fighting | 9 | `07C20A32 → 07C01EC4 → 07C01EC4` |
| Hyper Fighting (Beta) | 10 | `07E17D12 → 07E01CD4 → 07E01CD4` |
| Null Pointer Mockup | 24 | `07FBB39C → 07F9ECD4 → 07F8B108` |
| Space Pinball | 31 | `07EA746E → 07EA748E → 07EA4818` |
| SPONG | 64+ | `07E32C74 → 07E403D0 → 07E3FC20` |
| Formula V | 64+ | `07C34C64 → 07C34C98 → 07C388B4` |
| VUEngine Platformer | 64+ | `07E4CEE0 → 07E4D2C0 → 07E4D2C0` |
| Capitan Sevilla 3D | 64+ | `07E0F690 → 07E0F6C4 → 07E2A88C` |

No two end in the same place, so there is no single shared fix left in this
class — it is twelve individual investigations, each starting from a named
function. The depths split them in two: four games fill the 64-entry ring (they
execute a great deal and *then* stop) while the rest give up after nine or ten
functions.

Three things are ruled out for all of them: they hit no unimplemented calls;
following r31 from the driver recovers nothing (one hop, then the routine
returns without setting a continuation); and neither dispatching a computed jump
on the register's real value nor treating a top-level `jmp [r31]` as a transfer
changes any of them (0 of 13, twice) — though both of those are correctness
fixes worth keeping and are in `main`.

### 5. Running, but the state machine never advances — 13 games

3-D BattleSnake, BLOX 2, Ballface, Bound High, Nester's Funky Bowling, Panic
Bomber, SD Gundam, Teleroboxer, Tobidase! Panibon, Tron VB and V-Tetris sit on
one frame with the buttons held. Not a button gate — five masks (`303C`, `1000`,
`0004`, `0010`, `F03C`) leave Teleroboxer and Panic Bomber byte-identical out to
6,000 frames — and not a deadlock: Mario Clash's hot address resolved to the
standard frame-sync pair on `XPSTTS & 0x000C`, and both halves terminate.

### 6. Polling a display phase that never arrives — part of the 34 blanks

Golf reads `DPSTTS` (0x0005F820) 1,248,381 times and writes no character data
at all (`CHR=0/0`). `vb_vip_frame_advance()` toggles the display status bits
rather than cycling them through the real phase sequence, so a game waiting on
a specific phase order waits forever. Space Squash and Elevated Speed show the
same shape.

### 7. Garbled second character set

Bound High and V-Tetris render the English warning text correctly and the
Japanese text below it as wrong tiles. One character set lands and the other
does not, so this is the transfer of a specific tile range — not the renderer,
which is drawing exactly what it was given.

### 8. Partial glyph rows

Tron VB draws the top strokes of its logo and loses the bottoms. Hamburgers
renders a full bitmap title correctly but corrupts the small text at bottom
left. Rows within a tile are being dropped.

### 9. World placement

V-Tetris draws its text block flush left where it should be centred; SD Gundam
has a column of stray tiles down the left edge. Waterworld's round HUD renders
but its play field is empty. World X/Y or BGMap offsets are wrong for some
worlds and right for others in the same frame.

### 10. Configured but never filled — Jack Bros (2 games)

Jack Bros runs its main loop (six subroutines per frame) and never stops, but
`CHR=0/8192` — it writes 8,192 character bytes, all zero. `sub_07F0091C` sets
up a copy of 0x700 words from WRAM `0x05003800` into VRAM `0x0003A000`, and
that loop is correct; the buffer it copies is simply empty. `VBRECOMP_WWATCH`
(the write half of `VBRECOMP_WATCH`, added for this) counts **2 writes** to
that address in a whole run.

The display side is configured properly — worlds 28 and 22 enabled, full-screen
383x223, BGMaps at 0x2E000 and 0x2C000 — with `NZ=0` and `CHRnz=0` behind them.
So this is not a renderer fault and not a stall: whatever decompresses or builds
the tile data never runs. Ruled out along the way: the memory bus reads ROM and
every mirror correctly (`0x07F01000`, `0x07001000` and `0x07801000` all return
`C4F9002A`, matching the file).

Jack Bros (USA) and Jack Bros. no Meiro de Hiihoo (Japan) are the same game, so
one fix covers both.

## What is already ruled out

- **Unimplemented calls.** Stub functions, unfound JAL callees and unresolved
  indirect jumps are counted at run time (`vb_recomp_stub`, printed by the
  heartbeat). The stalled games sampled so far hit none of them.
- **The VIP interrupt path**, at least for Waterworld: the handler is
  registered, fires, and returns without leaking the stack.
- **Instruction semantics** for everything `tools/conformance.py` covers: the
  ALU and its flags, immediate sign- vs zero-extension, shifts and their
  carry-out, the 64-bit multiply/divide register pair, every `setf` condition,
  load sign-extension, the float unit, the bit-string block copy, and
  branch/call/return shapes.
