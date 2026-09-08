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
| Advancing | 39 | The frame changes between samples: the game is running. |
| Drawing, then frozen | 27 | One image, byte-identical at 300/600/900. |
| Nothing on screen | 11 | Every sample blank. |
| Dead | 0 | No game crashes any more; see the stack section below. |

"Renders something" is 65/76, but that number is the less useful of the two:
a game frozen on a correctly-drawn warning screen counts as rendering and is
not running. **Advancing** is the number to drive. Five of the 11 blanks
(Snowball Wars 1-3, Capitan Sevilla 3D, Null Pointer Mockup) draw nothing on
real hardware either, measured with and without input, so the honest denominator
is 71.

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

**The reference settles it, and disproves the theory below.** BizHawk's
Mednafen VB core (see `scripts/reference/`) shows WRAM entirely zero on real
hardware too, at every frame — so the empty buffer is normal and this whole
line was misdirected. The real divergence is narrow and dated: reference CHR
goes 0 (frame 30) to 81 non-zero samples (frame 60); vbrecomp stays at 0. The
game loads character data between frames 30 and 60 and we do not.

The superseded static reading, kept because the addresses are still correct: the copy's source pointer is
correct (`[0x0500014C] = 0`, so `r7 = 0x05003800` as intended) and the copy loop
is correct. The buffer is simply never filled — 2 writes to it in a whole run.
Whatever loads or decompresses the tile data sits in a state-machine branch the
game never takes, and finding that branch means reading its state machine
against real hardware behaviour.

Jack Bros (USA) and Jack Bros. no Meiro de Hiihoo (Japan) are the same game, so
one fix covers both.

## The reference says which 29 are actually ours

Every one of the 29 blank games was run on BizHawk's Mednafen VB core for the
same 600 frames (`scripts/reference/drawcheck.lua`), recording the frame at
which character data first appears:

- **26 load CHR on real hardware and not under vbrecomp.** Those are ours, and
  each now carries a deadline: the reference has tiles by frame 10 for most of
  them, frame 40-70 for Jack Bros and Space Invaders, frame 120-190 for
  Formula V and VUEngine Platformer. "Why is it blank" becomes "what happens
  before frame N that we miss".
- **3 draw nothing on hardware either** — Snowball Wars demos 1, 2 and 3, all
  reporting `firstChr=-1, chrFinal=0, fbFinal=0`. They are not vbrecomp
  defects, and counting them against the driver overstates the gap.

So the honest target is 73, not 76, and the real remaining work is 26 games —
each with a frame window rather than an open question.

## Configuration is exhausted

The 29 remaining blank games do not draw under ANY runtime configuration
available to the driver. Every combination below was measured across all 29:

| Dimension | Values tried | Result |
|---|---|---|
| Frame budget | 900, 3000 | 0 of 29 |
| Input | buttons held, no input | 0 of 29 |
| Frame pacing (`VBRECOMP_FRAME_TICKS`) | 2000, 5000, 20000, 60000, 200000 | 0 of 29 |

That is 232 runs in the pacing sweep alone. Only one game was ever recoverable
this way -- Red Alarm, which renders with no input and goes blank with buttons
held -- and it is already counted. Everything left is a code-level fault in
translation or hardware behaviour, not a knob.

## Two games are measurement-flaky, not broken

Bound High and Virtual Bowling alternate between `blank` and
`advancing`/`frozen` across identical runs. They are slow games, and under
load they do not reach the first sampled frame before the survey's 45-second
timeout kills them - so a run that scores them blank is measuring the
timeout, not the game. Read any single-run delta involving these two with
that in mind; a corpus count that moves by exactly these two has not moved.

## The 18 that still draw nothing, by signature

Counting CHR and BGMap writes was not enough to tell these apart, because a
count cannot distinguish a game that fills the WRONG place from one that fills
none. `vb_vip_dump_write_stats` now also prints the address RANGE the non-zero
writes landed in, and with that the remaining blanks separate cleanly:

| Game | CHR nonzero/all | CHR range | BGMap range |
|---|---|---|---|
| Capitan Sevilla 3D | 0/132,056 | - | 0x30120-0x3CD58 |
| Formula V (Emulator) | 0/67,504 | - | 0x32000-0x3BB14 |
| SPONG | 0/66,520 | - | 0x3A000-0x3C9CC |
| VUEngine Platformer | 0/41,944 | - | 0x3A000-0x3D5C8 |
| StarFox | 7,350/44,436 | 0x06019-0x1EFFF | 0x2000A-0x20936 |
| Fishbone | 4,957/22,576 | 0x1E005-0x1FFAF | 0x26000-0x2782F |
| Mario Combat | 3,248/20,480 | 0x06000-0x1FFFF | 0x20002-0x21FFE |
| Snatcher | 2,552/12,288 | 0x1F010-0x1FFFF | - |
| Galactic Pinball | 1,232/1,792 | 0x06010-0x066FE | 0x38000-0x38FFC |
| Jack Bros x2 | 0/8,192 | - | - |
| Hyper Fighting x2 | 0/8,192 | - | - |
| Null Pointer Mockup | 0/16,384 | - | - |
| Snowball Wars x3 | 0/32,768 | - | - |
| 3-D Tetris | 0/0 | - | - |

### A. The VUEngine cluster - 4 games, one cause

Capitan Sevilla 3D, Formula V (Emulator), SPONG and VUEngine Platformer are
the same engine, and they fail identically: tens of thousands of CHR writes
of which **not one is non-zero**, while BGMap fills the top segments. SPONG
writes 2,869 non-zero cells into 0x3A000-0x3C9CC and enables exactly one
world, which points at segment 10 (0x34000) - a segment nothing ever wrote.
Two of the four run past 0x3C000, which is beyond the last BGMap segment
(0x3BFFF) and into the region below the world attributes.

These games never read ROM in bulk: SPONG's hot-read list is its own WRAM
pad slots and the two VIP interrupt registers, nothing else. So the CHR
traffic is the engine clearing character memory and the charset copy never
runs at all. Four games move together on whatever gates it.

### The column table was being written over the world attributes - resolved

`vip_map_addr` folded 0x3DC00-0x3DFFF onto 0x3D800 as a "world attribute
mirror". It is not a mirror: 0x3DC00-0x3DFFF is the COLUMN TABLE (CTA), the
per-column brightness ramp the VIP reads separately. Every game that writes a
column table -- 512 halfwords, typically 0x00FE -- was therefore wiping all 32
world attributes with it.

The symptom was a game that had done everything right and displayed nothing.
StarFox loaded 7,350 non-zero CHR bytes, filled 396 BGMap cells, and set up a
380x224 world, then read back `GX=254 GY=254 MX=254 MY=254` -- 254 being
0x00FE, the column value landing on top of its world. A workaround here used
to block writes of the clear values 0x00FE and 0x0040 to fight the same
symptom; removing the bogus mapping removes the need for it.

StarFox and Virtual Lab go blank -> advancing. Nothing regressed.

### B. Tiles are loaded and nothing is drawn - 5 games

StarFox, Fishbone, Mario Combat, Snatcher and Galactic Pinball all write real
character data now. This is the renderer's problem, not the game's, and it is
the category to attack next because the games are already running.

Fishbone and Snatcher write tiles ONLY into CHR segment 3 (0x1E000-0x1FFFF).
Real hardware mirrors CHR 3 onto CHR 1; our write mapping deliberately keeps
the four segments separate (see `vip_map_addr`), and `chr_get_pixel`
compensates by falling back to the mirrored segment when a row reads zero.
That fallback covers the case, so the mirror hack is not the fault here - but
it is the first thing to re-check if these two move together.

### C. Still nothing at all - 9 games

Jack Bros x2, Hyper Fighting x2, Null Pointer Mockup, 3-D Tetris and the three
Snowball Wars demos (the last of which are blank on hardware too).

Jack Bros is the interesting one: it configures **11 worlds** with real
attributes and then writes 8,192 CHR bytes of zeros and no BGMap at all. Its
main loop was wedged until this session - the VIP handler reached its
GAMESTART branch, which sets the frame flag the loop spins on, once in 300
frames instead of every frame - and with that fixed `sub_07F00222` runs and
the loop is genuinely released. It still loads no tiles, so the tile load sits
behind a state the game has not reached.

## The 12 that are left, and what each is losing control to

Counting a jump to address 0 as an unimplemented call (rather than letting the
drain treat it as "nothing pending") made the last invisible failure visible.
With that, every remaining game now names its own loss:

| Game | lost jumps to |
|---|---|
| SPONG | `0x00000000` x5, `0x01000000` x1 |
| Capitan Sevilla 3D | `0x00000000` x3 |
| Hyper Fighting | `0x0001CC80`, `0x00001CA0`, `0x00001DD0`, `0x000201A8` |
| Jack Bros | `0x00030002` |

SPONG and Capitan Sevilla are the same engine and the same shape: a C++ virtual
dispatch, `r6 = [r29+12]; r10 = [r6]; r10 = [r10+48]; jmp [r10]`, reading a null
object. Their WRAM is populated (2,184 non-zero bytes for SPONG), so the data
section did initialise -- one specific object never got constructed.

Hyper Fighting and Jack Bros lose control to addresses inside VIP VRAM
(0x00000000-0x0003FFFF), which no jump should ever target. Adding this ROM's
base to them does not land on any emitted function, so they are not bare ROM
offsets either; a register holding data is reaching a `jmp`.

3-D Tetris remains the outlier: zero CHR writes, zero BGMap writes, zero
unimplemented calls, no worlds. It does nothing at all and reports nothing.
Galactic Pinball is classified `dead` -- it never reaches the first sampled
frame inside the timeout, which is a performance problem, not a blank screen.

## Galactic Pinball is a stack overflow, not a blank screen - diagnosed

It was classified `dead` because it produced no frame before the timeout. It is
not slow: it exits in 0.2 seconds with `0xC00000FD`, STATUS_STACK_OVERFLOW.

`vb_recomp_deep` only ever fired from `vb_recomp_call`, so a runaway chain of
plain `VB_CALL`s reported nothing. VB_CALL now carries its target address and
checks the same depth window, which names the cycle immediately: one group per
level, `07F0068C -> 07F00976 x6 -> 07F00298 -> 07F007C0 -> 07F00976`, with the
depth climbing by exactly one per iteration. `sub_07F00026` is the game's main
function and is re-entered once per group through a drain that is itself nested
inside it, so the loop nests instead of unwinding.

### Draining only at the outermost level does NOT fix it - measured

The obvious repair is to have `VB_CALL` drain only when the depth returns to
zero, which would keep the stack flat. Measured on the panel: **0 better, 4
worse** -- Mario Clash, Mario's Tennis, Panic Bomber and Teleroboxer all went
blank. Deferring a tail target until the whole call stack unwinds reorders the
work those games depend on. Draining at every call site is load-bearing, and
the fix for Galactic Pinball has to come from somewhere else.

## The stack, and two things that were NOT the problem

Galactic Pinball, 3-D Tetris and Null Pointer Mockup all died with
STATUS_STACK_OVERFLOW while printing nothing -- the process simply vanished,
which is why they read as blank. The driver now catches it and dumps the trace
ring, and all three run to completion.

The cause was **frame size, not runaway recursion**. With the DEEP window
widened to depth 40 none of them ever reported, and the drain cap never fired,
so the logical call depth stays under 40. One guest function becomes one C
function whose frame dwarfs the 8-16 bytes the V810 pushed, and the default
1 MB does not cover a normal nesting depth. Game targets now link with
/STACK:1073741824 -- reserved address space on x64, committed only as touched.

### Interrupt handlers do leak stack -- but do NOT restore it

Galactic Pinball's timer handler leaks exactly 4 bytes on every interrupt, and
Null Pointer Mockup ends up returning to 0x05010000, one byte past the end of
WRAM. So the leak is real and it does walk the stack pointer off the end.

Restoring the stack pointer after each handler is nevertheless **wrong**:
measured across the corpus it is advancing 37 -> 34 and blank 13 -> 17. The
12-game panel showed 0 worse and missed it entirely, which is the case for
running `-Panel all` before believing a runtime change. Some handlers depend
on the drift. Reverted; the leak is still logged under VBRECOMP_HEARTBEAT.

## Jack Bros' state machine is emitted as 207 do-nothing stubs

v810recomp now prints, per ROM, how much of the jump-table graph confirmation
reached. The contrast is stark:

| | tables | owner confirmed | targets confirmed | targets NOT |
|---|---|---|---|---|
| Waterworld (works) | 12 | 4 | 162 | 0 |
| Jack Bros (blank) | 7 | **0** | **0** | **221** |

An unconfirmed jump-table target is emitted as `vb_recomp_stub(addr)` -- a
function that does nothing when the game dispatches to it. Jack Bros has 207
of those against Waterworld's 2, so its entire state machine is stubs.

Confirmation happens once, at table-DISCOVERY time, and only if the enclosing
function is confirmed *at that moment*. Not one of Jack Bros' 7 tables sits in
a confirmed function, so nothing propagated.

### Confirming them all is NOT the fix - measured

Doing it unconditionally clears every stub in the corpus (hundreds -> 2) and is
+2 advancing, but it destroys Space Squash: 4,914 drawn pixels -> 4, verified
by rebuilding that one game against both analyzers. Net it is 63 -> 62
rendering. Restricting it to targets that do not land inside an existing
function is worse still (37 advancing, and Space Squash stays broken).

So the open problem is not "confirm more" -- it is telling a real dispatch
table from bytes that merely decode like one. The counter above is what will
measure any attempt at that.

## Jack Bros: where its tiles come from, and what it never builds

The reference settles the first half exactly. Dumping CHR RAM at frame 90 and
searching the ROM for it shows a straight linear block copy: CHR byte X equals
ROM offset 0xA0000 + X, i.e. the game copies from **0x07FA0000** into VIP
0x06000, and 25,769 of 32,768 bytes match that mapping. Non-zero tile data
spans CHR 0x40-0x3FBC, all in segment 0.

On our side exactly ONE write ever reaches CHR 0x6000, from the boot clear.
The copy is queued through a descriptor that `sub_07F11190` builds -- dest
0x6000, a source, a length -- and that function has exactly two callers,
`sub_07F00CB8` and `sub_07F00CE0`, **neither of which has any caller at all**.
They are reachable only by indirect dispatch, and the game never dispatches to
them.

A corrected WRAM diff says the same thing from the data side. Against the real
WRAM domain (see scripts/reference/README.md -- the earlier comparisons were
reading VIP memory), hardware has 975 non-zero bytes at frame 90 to our 591,
and the difference is concentrated in a region we never touch at all:

| region | bytes the reference has that we do not |
|---|---|
| 0x0500A100-0x0500ACFF | 288 |
| 0x0500E800-0x0500EEFF | ~120 |

So the game builds a structure at 0x0500A100 that we never build, and the tile
transfer hangs off it. That is the next thing to chase, and it is a specific
address rather than a blank screen.

## Jack Bros is a cooperative-task game, and the switch is a `jmp [r31]`

`sub_07F02172` creates a task: it shifts the task id left 9, saves r6-r25 into
a 512-byte context at `0x0500EEA8 + (id << 9)`, stores the entry point, and
registers the context in a table at `0x0500EC00 + (id << 4)`.

`sub_07F020B0` is the context switch. It loads the task's saved stack pointer,
restores r6-r25 from the context, pops the task's resume PC into r31, and
`jmp [r31]`s into it. We emit that as VB_RET.

The task table shows exactly what that costs, against hardware at frame 90:

| | count | context ptr | resume PC |
|---|---|---|---|
| reference slot 0 | 1 | 0x0500EEA0 | 0x0701AE8E |
| ours slot 0 | 1 | 0x0500EEA8 | 0x07F00CE0 |

The reference's context pointer has moved (the task pushed) and its resume PC
is somewhere in the middle of the task. Ours is still the creation-time value
and the entry point: **the task has never run**. That is also why the tile
transfer never happens -- `sub_07F00CB8`/`sub_07F00CE0`, the only callers of
the routine that queues it, are task entry points and nothing else calls them.

### Distinguishing return from computed jump by shadow stack does NOT work

The obvious repair: have every VB_CALL remember the return address it put in
r31, and treat `jmp [r31]` as a return only when r31 still equals it -- a
scheduler that LOADS r31 from a context would then be correctly parked as a
jump. Measured: advancing 38 -> 36, blank 12 -> 13. Too many ordinary returns
reach the instruction with r31 legitimately different, and parking those breaks
more flow than the scheduler case recovers. Reverted.

## Formula V and Jack Bros both copy tiles straight out of ROM

The reference settles where the tile data comes from, for both, exactly:

| game | CHR byte X equals | non-zero CHR span |
|---|---|---|
| Jack Bros | ROM 0xA0000 + X (0x07FA0000) | 0x40-0x3FBC |
| Formula V (Emulator) | ROM 0x306FA0 + X (0x07F06FA0) | 0x22-0xC6F |

Neither is compressed and neither pointer appears anywhere in the ROM as a
32-bit constant, at any alignment, so both are computed.

For Formula V we now know the read side works and the write side does not:
a read watch on 0x07F06FC0 fires (LR=0x07C2C316) and the ROM there holds real
tile bitmaps (0x01FC0000), but every one of its 67,504 CHR writes is zero.
Raw pre-mapping VIP write addresses span only 0x00000020-0x0003DBE0, so this is
not the mirror-mapping fault it looked like -- the game reads the right data and
then never writes it to character memory at all. Its 2,995 non-zero writes go to
BGMap 0x32000-0x3BB14.

`vb_vip_dump_write_stats` now also prints that raw range, because a count and a
mapped range together still could not distinguish "wrote to the wrong place"
from "never wrote".

## The VUEngine cluster: construction stops partway, and hardware says where

Capitan Sevilla 3D, Formula V (Emulator), SPONG and VUEngine Platformer all die
the same way -- a C++ virtual dispatch through a null pointer:

```
07E13D5C: ld.w 12[r6], r6     ; an object field
07E13D60: ld.w  0[r6], r10    ; its vtable
07E13D64: ld.w 40[r10], r10   ; the method
07E13D68: jal  .+4            ; PC capture
07E13D6E: jmp  [r10]          ; -> 0
```

Two things narrow it a long way past "a null pointer".

**It is not VIP interrupts.** None of these games ever writes INTENB -- and
neither does Capitan Sevilla 2, which renders. So a VUEngine game does not need
VIP interrupts, and that whole theory is dead. What separates the pair is that
CS2 reaches display init (DPCTRL=0x0302, BRT=20/40/20, worlds set up) and CS3D
never does (DPCTRL=0x0000, BRT=00/00/00).

**Object construction stops partway, and the reference names the objects.**
Diffing WRAM at frame 200 against hardware, for the object at 0x0500668C:

| field | hardware | ours |
|---|---|---|
| +0 | 0x00038D0C | 0x00038D0C |
| +8 | **0x05007888** | 0 |
| +16 | **0x05000EFC** | 0 |

The first field matches, so the object is allocated and partly built -- but the
two objects it should point at are never created. The same holds further up: at
0x05000F40 hardware has a manager holding a ROM pointer and three allocations
(0x07108130, 0x050082CC, 0x0500A0D4, 0x0500C654) where we have 32 bytes of
zeros. Hardware has 2,276 non-zero WRAM bytes at that frame; we have 1,174.

So the question is not "why is this pointer null" but "which allocation stops",
and the answer is a specific address in a specific frame rather than a class of
bug. One fix should cover all four games.

### The allocator is fine; a later init step never runs, and it is early

Comparing the whole object at 0x0500668C field by field against hardware, most
of it is already right:

| field | hardware | ours |
|---|---|---|
| +32 / +36 / +40 | 0x050089A4 / 0x050089C0 / 0x050089DC | **identical** |
| +60 | 0x07108130 (ROM) | **identical** |
| +8 | 0x05007888 | 0 |
| +16 | 0x05000EFC | 0 |
| +44 | counter | 0 |
| +56 | 0x050066D8 | 0 |

Three allocations from `sub_07E2A054` land at the same addresses on both sides,
so the memory pool works and the constructor runs. `sub_07E32F4C` even zeroes
+8 deliberately (`st.w r0, 8[r29]`) -- it is meant to be filled in later, and
the later step is what is missing.

Dumping hardware at frames 30, 60 and 200 dates it: +8, +16 and +56 are already
set at **frame 30**, and +44 is just a counter climbing (0x140, 0x398, 0xE88).
So the divergence is in early boot, not in the frame loop, and the missing piece
includes an object at 0x05000EF8 holding {0x0F, 0x0003C5F0} that we never build.
Non-zero WRAM: 2,370 on hardware at frame 30 against our 1,174 at frame 450.

## Galactic Pinball hangs on a counted loop with a negative count

The basic-block marker parks it at 0x07F00986, an unrolled multiply-by-addition:

```
07F0097A: mov  r25, r6
07F00980: add  -1, r6
07F00984: bz   0x07F0098E     ; skip if zero
07F00986: add  r7, r23 / add r7, r24 / add -1, r6 / bnz 0x07F00986
```

Its caller sets `mov 6, r25` and counts down. The heartbeat shows
**r25 = 0xFFFFFF9F** (-97), so r6 starts at -98 and the inner loop runs four
billion times -- indistinguishable from a hang, and why this game read as
"no frames" for so long.

Interrupt handlers are NOT the cause. A new check snapshots r2-r30 across every
handler and reports the first register that changed; for Galactic Pinball it
reports none. The handler does still leak 4 bytes of stack per interrupt (see
above), but it preserves every general register, so the counter is being
destroyed somewhere in the game's own translated flow -- most likely the
re-entry of sub_07F00026 through a nested drain that the DEEP trace already
showed, which re-runs the loop body without re-running `mov 6, r25`.

### Profile-guided discovery can harvest a LOOP LABEL, and that is poison

Galactic Pinball's hints file contained `entry 07F000AC`. That address is one
instruction past `mov 6, r25` inside sub_07F00026's counted loop:

```
07F000AA: mov  6, r25          <- initialisation
07F000AC: jal  0x07F00976      <- the harvested "entry" starts HERE
07F000B0: jal  0x07F007C0
07F000B4: add  -1, r25
07F000B6: bnz  0x07F000AC
```

Making it an entry lets control arrive AFTER the initialisation, so `add -1,
r25` decrements whatever r25 happened to hold. Observed: 0xFFFFFF9F (-97),
which makes the inner multiply-by-addition loop at 0x07F00986 run about four
billion times. That is the whole reason this game reported no frames.

Removing the one hint fixes the hang (r25 back to 0, execution moves on).

**Rejecting such entries globally does NOT work.** A rule that drops any entry
landing on a backward-branch target inside another function catches this one
and three more in the same ROM -- and is advancing 39 -> 32, blank 11 -> 18
across the corpus, because plenty of games genuinely are dispatched into the
middle of a loop. Reverted; the hint is removed per-game with a comment in the
hints file so discovery does not re-add it.

### Galactic Pinball, continued: the split is sub_07F000B4, and it is not the hint

Removing `entry 07F000AC` stops the four-billion-iteration hang, but the game
then loses control 591 times a run to that same address. The lost-jump context
says why: `r31 = 0x07F000B0`, so the dispatch IS the loop's own back edge
(`bnz 0x07F000AC`) arriving as a cross-function transfer.

The function that transfers is **sub_07F000B4**, an overlapping entry starting
at the loop's `add -1, r25`, strictly inside sub_07F00026 (0x07F00026-0x07F00164).
sub_07F00026 has the back edge as a proper `goto`; the duplicate does not.

Two attempts to remove it generically, both measured and both reverted:

| attempt | result |
|---|---|
| drop entries on a backward-branch target inside another function | advancing 39 -> 32, blank 11 -> 18 |
| drop scan-created entries that land inside a real function | advancing 39 -> 36, **3 games start crashing** |

And it is not the brute-force JAL sweep that creates it: regenerating with no
hints at all still produces sub_07F000B4, and marking sweep-created functions
and dropping the overlapping ones does not remove this one. Some other path in
the analyzer adds it, and finding which is the next step for this game.

### Which pass creates the split, and why multi-entry functions do not fix it

Six sites call `v810_ctx_add_func` and the output never said which made a given
function. Tagging them (`VBRECOMP_WATCH_FUNC=<hex>`) answers it in one run:
Galactic Pinball's sub_07F000B4 comes from **jmp-target promotion**, five times
-- a resolved `jmp [reg]` whose target lands inside sub_07F00026.

Three fixes tried, all measured against the full corpus, all reverted:

| fix | result |
|---|---|
| skip promotion when the target is inside a known function | Galactic Pinball loses control 591 -> **1,183** times: the address really is dispatched to, so refusing to make it callable is worse |
| multi-entry functions: emit the interior entry as a thunk that parks the address and `goto`s the label inside the real function | works exactly as designed for this game -- unimplemented calls drop to **0** -- but corpus advancing 39 -> 32, blank 11 -> 27, Mario Clash and Teleroboxer both go blank |
| drop entries on a backward-branch target inside another function | advancing 39 -> 32, blank 11 -> 18 |

The multi-entry result is the informative one. It repairs the control flow it
was aimed at and still costs eight games, because plenty of "interior"
functions are genuinely separate routines that merely fall inside another
function's approximate range. So the range data is the real problem: until
function boundaries are exact, any rule keyed on containment will misfire.

### Function ranges are a high-water mark, and now there is exact data

`func->end_addr` is the highest address reached on ANY path explored from the
entry, so one forward jump inflates it and "is this address inside that
function" stops meaning anything. That is why every containment-keyed fix
above misfired.

The analyzer now also records, per ROM byte, which function first traced it
(`ctx->byte_owner`). The two measures disagree badly, in both directions:

| game | interior by end_addr range | interior by exact ownership |
|---|---|---|
| Mario Clash | 121 | 48 |
| Galactic Pinball | 8 | 23 |
| Waterworld | 9 | 29 |

Mario Clash explains the earlier failure exactly: the containment rule thunked
121 functions, of which 73 were not interior at all.

Re-running the multi-entry fix on exact ownership instead is still wrong, and
conformance catches it in seconds -- "bcond: backward branch loops" fails,
because a function whose first byte was traced by a FALL-THROUGH from its
neighbour is not an interior label either. Ownership is necessary but not
sufficient: what is wanted is "reached only by a branch from inside", which
needs edge data the analyzer does not keep.

The ownership array is committed (measured neutral) because it is the
foundation any real fix here needs, and because the disagreement table above is
the evidence that the range data was never trustworthy.

### Multi-entry functions: the rule is now exact, and it still does not work

Built on the ownership data, the precise test for "interior label" is two
conditions, and conformance proves both are needed:

1. `byte_owner[F.addr] == G.addr` -- G actually traced that byte.
2. G BRANCHES to F.addr -- so G already has a label there.

Ownership alone fails: a function whose first byte was traced by a
FALL-THROUGH from its neighbour is a real routine, and thunking it breaks
conformance's `bcond: backward branch loops` in seconds. Adding the branch
condition restores 77/77 and produces 500 thunks across the corpus.

It is still wrong: advancing 39 -> 32, blank 11 -> 23, with Mario Clash and
Mario's Tennis going blank. Reverted.

And Galactic Pinball shows why the whole idea is misconceived for its case. The
thunk for 0x07F000AC is emitted correctly and enters sub_07F00026 at the right
label -- but entering mid-loop legitimately skips `mov 6, r25`, so every
external dispatch decrements the counter again. After six of them r25 goes
negative (observed 0xFFFFFF9F) and the inner loop runs four billion times. The
dispatches themselves are the artifact; making them land correctly does not
help, because they should not be happening at all. sub_07F000B4 -- the
duplicate that transfers back into the loop -- is reached by fall-through, not
by a branch, so no version of this rule removes it.

What is actually needed is per-EDGE data: for each address, whether anything
outside the function targets it. The analyzer records neither in-edges nor
their origins. That is the change to make before this class is attempted again.

### sub_07F000B4 is NOT spurious -- the game really jumps there

Every fix above assumed the overlapping entry was an artifact. It is not.
`VBRECOMP_WATCH_FUNC` names jmp-target promotion as its source, and the source
instruction is unambiguous:

```
07F00AEA: movhi 0xFFF0, r0, r1     ; r1 = 0xFFF00000
07F00AEE: movea 0x00B4, r1, r1     ; r1 = 0xFFF000B4  (= 0x07F000B4)
07F00AF2: jmp   [r1]
```

That is a deliberate tail-jump back into the main loop's `add -1, r25`, built
from immediates. So 0x07F000B4 is a real entry, r25 is meant to be live across
it, and on hardware it holds the right value. Every attempt above to suppress
or re-route that entry was therefore attacking the wrong thing -- which is why
each one cost games elsewhere and none helped this one.

The real question is narrower and different: **why is r25 wrong when we arrive
there.** The caller sets `mov 6, r25`, neither callee writes r25, and interrupt
handlers preserve every general register (checked). So either we reach
0x07F00AF2 when hardware would not, or something between loses r25 in a way the
per-handler check does not see. That is the thread to pull, and it is a
game-state question rather than a recompiler-structure one.

### Root cause: a tail-jump into a caller's loop NESTS instead of unwinding -- FIXED

Galactic Pinball's structure is:

```
sub_07F00026:
  07F000AA: mov 6, r25
  07F000AC: jal 0x07F00976      <- loop body, part 1
  07F000B0: jal 0x07F007C0      <- loop body, part 2
  07F000B4: add -1, r25
  07F000B6: bnz 0x07F000AC

...and inside the body, at 0x07F00AF2:
  movhi 0xFFF0, r0, r1 / movea 0x00B4, r1, r1 / jmp [r1]   -> 0x07F000B4
```

That last jump is a callee ending by tail-jumping back into its CALLER's loop,
at the decrement. On hardware control simply arrives there with r25 live and the
loop continues, once per iteration.

Our tail-jump model parks the target and drains it, which DISPATCHES
sub_07F000B4 as a nested call -- and then the original `VB_CALL(vb_func_07F00976)`
still returns to sub_07F00026, which runs its own copy of the loop as well. The
execution profile shows it outright:

| function | entries in a 64-sample window |
|---|---|
| sub_07F00976 (body part 1) | 15 |
| sub_07F007C0 (body part 2) | 15 |
| sub_07F000B4 (the decrement) | 15 |
| **sub_07F00026 (owns the loop)** | **4** |

The loop advances roughly four times per real iteration. r25 goes 6, 5, ... 0,
exits the inner run, and the outer run then decrements past zero -- the `bnz`
never sees zero again and the count observed at the heartbeat is 0xFFFFFF9F.

This is why every structural fix attempted here failed: the entry is real, the
jump is real, and the defect is that a transfer INTO AN ACTIVE FRAME is executed
as a new frame. Fixing it needs the drain to unwind to the frame that owns the
target (a longjmp, or an explicit unwind flag checked by every VB_CALL) rather
than nest. That is a redesign of the transfer model, not a patch to the
analyzer, and the four analyzer-level attempts recorded above are all dead ends
for exactly that reason.

### The unwinding transfer model (implemented)

The drain now asks, before dispatching a parked target, whether that target is
an INTERIOR ENTRY of a function still on the call stack. If it is, the target
stays parked, `vb_unwinding` is set, and every VB_CALL returns immediately until
the depth reaches zero -- at which point the transfer is dispatched from the
outermost frame. So a callee tail-jumping back into its caller's loop unwinds to
it instead of running a second, nested copy while the original call site also
returns into the caller's own copy.

Three pieces: a stack of active function addresses (`vb_frames`), the unwind
flag, and a generated `vb_interior_owner()` built from the byte-ownership data
-- 4,742 interior entries across the corpus, 38 in Galactic Pinball.

Galactic Pinball's counter is fixed: r25 reads **0** at the heartbeat where it
read 0xFFFFFF9F, and the loop terminates instead of running four billion times.
It still does not draw -- it now loses control 591 times a run to 0x07F000AC,
which is a separate question -- but the double-execution is gone.

Measured: 39 advancing, 28 frozen, 11 blank, **0 worse**. Conformance 77/77.
This is the first change in this area that costs nothing; the four analyzer-
level attempts before it each cost seven games because they were aimed at the
wrong thing.

Re-adding `entry 07F000AC` on top of it does NOT work: those dispatches arrive
when sub_07F00026 is not on the stack, so there is no frame to unwind to and
the loop runs uninitialised again. The hint stays out.

## Where the last eight actually stop

With the unwinding transfer model in, every remaining game has been walked to a
specific stall point. Six of the eight stop in the SAME PLACE conceptually:
they never set up a displayable world.

| game | tiles loaded | display on | worlds set up |
|---|---|---|---|
| Null Pointer Mockup | 5,252 CHR, BGMap 108 cells | yes (DPCTRL=0x030A, BRT=20/40/20) | **none** -- only world 31, holding the END marker; worlds 30, 29 and 0 read zero |
| Capitan Sevilla 3D | 0 non-zero of 132,056 | no (DPCTRL=0) | none |
| SPONG | 0 non-zero of 66,520 | no (DPCTRL=0) | one, pointing at an empty segment |
| Formula V (Emulator) | 0 non-zero of 67,504 | no | none |
| VUEngine Platformer | 0 non-zero of 41,944 | no | none |
| Jack Bros x2 | 0 non-zero of 8,192 | yes (DPCTRL=0x0202) | 11 worlds with real attributes |
| Galactic Pinball | 1,232 CHR, 1,024 BGMap | partly (DPCTRL=0x0300, XPCTRL=0) | none |

Null Pointer is the most advanced: it has character data, a filled BGMap
segment, the display AND the drawing engine enabled, and correct brightness --
and then writes exactly one world attribute, the terminator. Jack Bros is the
mirror image: eleven real worlds and no tile data.

So the shared question for most of the remaining set is not the renderer and not
the CPU: it is that world setup never runs. Two of them (Jack Bros, Null
Pointer) prove the renderer and the tile path each work in isolation.

### Null Pointer Mockup: the three-way branch is NORMAL (earlier reading was wrong)

It writes NO world attributes at all -- a write watch on 0x0003DBE0 shows one
write, from the boot clear, and 0x0003DBC0 shows none. The 0x0040 sitting in
world 31 is ours, not the game's.

The basic-block marker parks it at 0x07F939AA in sub_07F93920:

```
07F93996: shl  -8, r10        ; imm5 = 24: sign-extend the low byte
07F939A4: sar  -8, r10        ; (emitted correctly as shl 24 / sar 24)
07F939A6: cmp  1, r10
07F939A8: bz   0x07F939E6     ; r10 == 1
07F939AA: cmp  r0, r10
07F939AC: bz   0x07F939BA     ; r10 == 0
07F939AE: movea 0xB0C8, r27, r10  ; else: r10 = 0x0500B0C8
07F939B6: jr   0x07FBB33C         ;       and jump to the handler
```

A sign-extended byte that is neither 0 nor 1 sends it down the third arm, and
sub_07FBB324/sub_07FBB33C -- which run 13 and 12 times in a 64-entry window --
are that arm. So the game is taking an error/assert path over and over instead
of initialising, which is why world setup never happens.

The shift emission was checked and is right (imm5 24, not the -8 the
disassembly prints). The next question is where that byte comes from.

### ...because its init runs more than once, and byte_owner cannot see it

The byte the three-way branch tests is at **0x0500B0C0**, and it holds **2**.
The game wrote it itself, from two sites inside sub_07F93920 (LR 0x07F939D4 and
0x07F93942). So this is an initialisation guard -- 0 = not started, 1 = in
progress, 2 = done -- and the third arm is the "already initialised" error.

sub_07F93920 runs 12 times in a 64-entry window and sub_07F93942 -- a label
INSIDE it that is also a function, with zero direct callers -- runs 13. So the
init is being re-entered, the guard correctly says "done", and the game loops in
its error handler forever. Same double-execution family as Galactic Pinball.

The unwinding model does not catch this one, and the reason is worth recording:
`byte_owner` is first-writer-wins, so when the interior function happens to be
ANALYSED BEFORE its container, it claims its own bytes and ownership reports it
as a top-level routine. 213 interior entries are found in this ROM; 0x07F93942
is not among them. The ownership data is therefore order-dependent in exactly
the way `end_addr` was, just less often.

Fixing that means resolving overlaps when they are DETECTED rather than trusting
whoever got there first -- a small change in principle, but it re-opens the same
question the four reverted attempts above ran into, so it needs measuring rather
than assuming.

### Correction: Null Pointer's guard byte is right, the branch is not an error

Two entries above read the three-way branch at 0x07F939AA as an assert: the
byte at 0x0500B0C0 holds 2, neither 0 nor 1, so the game takes a third arm into
sub_07FBB33C. **The reference says that byte is 2 on hardware too.** So the
third arm is ordinary behaviour and nothing is asserting. That reading was
wrong and the conclusions drawn from it -- "running an error path in a loop",
"the init is re-entered" -- do not follow from this evidence.

What the reference does show, at frame 200 against our frame 450:

| | non-zero WRAM |
|---|---|
| hardware | 3,323 |
| ours | 1,370 |

and the divergence starts at the very first word: 0x05000000 holds 0x0703C6C4
on hardware and 0x00003920 for us. Both of our writes to it come from the boot
path (LR = 0, before anything has set a link register), so hardware's value is
written later by code we never reach -- it is not a truncated pointer, which
was my next guess and is also wrong.

The honest state for this game: it loads characters, fills a BGMap segment,
enables display and drawing with correct brightness, sets no world attributes,
and diverges from hardware at the first global. Where it stops running the
hardware's path is not yet known.

### Galactic Pinball: the dispatches are POST-BOOT, and that is the whole problem

`entry 07F000AC` was re-tried once ownership began preferring the container, so
that it registers as an interior entry of sub_07F00026 and the unwinding drain
should catch it. It does register -- and r25 is still 0xFFFFFF9F.

The reason closes the loop on this game. sub_07F00026 is a BOUNDED routine: it
runs its counted loop, restores its saved registers and returns. By the time the
591 dispatches to 0x07F000AC happen -- from the interrupt-driven loop, long
after boot -- there is no sub_07F00026 frame on the stack to unwind to. So the
dispatches run the loop body with no `mov 6, r25` before them, forever.

On hardware the jump at 0x07F00AF2 into that loop happens WHILE sub_07F00026 is
executing. Ours happens after it has returned. So the defect is upstream: we
reach 0x07F00AF2 at a time hardware does not. The WRAM diff agrees -- hardware
builds a table of handler pointers at 0x05000100-0x050001FF (0xFFF09D99,
0xFFF09D4C, ... low-mirror ROM addresses, computed rather than immediates or ROM
data) and we build none of it.

One further reading, checked and withdrawn: sub_07F00976 has exactly ONE caller
(sub_07F00026's loop) yet appears 15 times in a 64-entry trace window against
that caller's 4, which looks like it is dispatched from somewhere else. It is
not -- four entries of a six-iteration loop is twenty-four calls, so fifteen is
simply the loop running. No anomaly; recorded so the count is not re-read as
evidence later.

So: not the entry, not the ownership, not the unwind trigger. The question is
why our control reaches that jump post-boot at all.

## Re-verified on hardware: which of the blanks actually draw

The earlier "denominator is 73" claim rested on the three Snowball Wars demos
being blank on real hardware. That was measured in an earlier session, so it was
re-run, and the same check was applied to the rest of the blank set. All figures
are `drawcheck.lua`, 600 frames, **no input**:

| game | first CHR | CHR at 600 | framebuffer at 600 |
|---|---|---|---|
| Formula V (Emulator) | 120 | 90 | **191** |
| SPONG | 20 | 60 | **147** |
| Galactic Pinball | 10 | 36 | **104** |
| VUEngine Platformer | 190 | 253 | **100** |
| Jack Bros | 50 | 81 | **96** |
| Capitan Sevilla 3D | 20 | 169 | **0** |
| Null Pointer Mockup | 10 | 259 | **0** |
| Snowball Wars 1/2/3 | none | 0 | **0** |

Snowball Wars is confirmed: no character data and no framebuffer content at any
sample. The denominator of 73 stands.

The new result is that **Capitan Sevilla 3D and Null Pointer Mockup also show an
empty framebuffer on hardware** through 600 frames, despite loading plenty of
character data. They are not in the same class as the five above, which visibly
draw.

**The caveat is now resolved.** `drawcheck_input.lua` repeats the measurement
holding 0x303C in the same burst pattern `games/driver/main.c` uses. On real
hardware, over 600 frames, WITH input:

| game | CHR at 600 | framebuffer at 600 |
|---|---|---|
| Capitan Sevilla 3D | 169 | **0** |
| Null Pointer Mockup | 259 | **0** |
| Snowball Wars 1 | 0 | **0** |

So input was not the explanation: both games load character data and put nothing
on screen on real hardware, pressed or not. **Five of the eleven blanks are
hardware-blank, not three**, and the honest denominator is **71**, not 73 --
65 of 71.

Scope of the claim: 600 hardware frames. The corpus survey runs longer in our
own frame units, and the two clocks are not comparable, so this does not rule
out these games drawing later than the window measured.

(`firstDraw` samples every 64 bytes and the final count every 16, so
`firstDraw=-1` with a non-zero final -- VUEngine Platformer -- means a sparse
image the coarse sampling missed, not an absence.)

## Jack Bros: our stack pointer leaves WRAM, and it is the FIRST divergence

Comparing hardware at its first-CHR frame (60) against our settled state, the
very first differing word is the interesting one:

| address | hardware | ours |
|---|---|---|
| 0x0500000C | 0x0500FFF8 | **0x050100A4** |

and the live stack pointer agrees: the heartbeat reports **SP=0x050100A4**,
which is past the top of WRAM (0x0500FFFF). Memory access is not the problem --
vb_mem masks WRAM with VB_WRAM_MASK, so it mirrors correctly -- but the VALUE is
172 bytes above where hardware keeps it, and the game compares and stores it.

The leak detector names the source: `IRQ 4 LEAKED SP: 0500FFFC -> 050100A4
(delta 168)`, in a SINGLE dispatch of the VIP interrupt, which also clobbers r7.

The handler's own prologue and epilogue are deliberately asymmetric:

```
sub_07F0061C:  addi 0xFFEC, r3, r3     ; -20, then st.w r6..r10
   ...
               addi 0x0014, r3, r3     ; +20
               ld.w 0[r3], r1          ;
               add  4, r3              ; +4   -> net +4
               reti
```

That +4 is balanced by the VECTOR stub at 0x07FFFE40, which does
`add -4, r3; st.w r1, 0[r3]` before jumping to the handler. Both are registered
and emitted correctly -- vb_recomp_init_handlers installs 0x07FFFE40, not the
handler -- so one vector plus one handler balances exactly.

**Hypothesis, arithmetic only, not verified:** 168 = 42 x 4, so a chain that
re-enters the handler 42 times without re-entering the vector would produce
precisely this. The trace does not show the handler looping in the window
sampled, so this is a number that fits rather than a mechanism observed. Anyone
picking this up should count handler entries per dispatch before believing it.

## Jack Bros, solved as a causal chain (fix measured, not shippable yet)

`VBRECOMP_COUNT_FUNC=<hex>` counts entries to one function and the interrupt
dispatcher reports it per handler call. That first killed my own hypothesis --
the handler is entered exactly ONCE per dispatch, not 42 times -- and then
produced the real chain.

The VIP handler makes six sequential `jal`s. We enter them:

| callee | entries per dispatch |
|---|---|
| 07F006CA | 1 |
| 07F00722 | 2 |
| 07F0076C | 4 |
| 07F00812 | 4 |
| 07F008C2 | 4 |
| 07F0091C | 4 |

Doubling. The cause is a SPURIOUS SPLIT: sub_07F006CA was cut at 0x07F006E4, an
address **no jal in the entire ROM targets** -- the genuine routine runs to its
`jmp [r31]` at 0x07F00720, i.e. 0x07F006CA-0x07F00722. The emitter turns the
resulting boundary into `VB_JUMP(...); /* falls through */`, a TRANSFER, so the
drain runs the remainder of the real function and the caller then continues,
executing everything after it twice, and again at the next split.

Each extra pass also runs the handler's epilogue, which pops 4 bytes more than
its prologue pushed (the balance belongs to the vector stub's `st.w r1`). That
is the `IRQ 4 LEAKED SP: 0500FFFC -> 050100A4 (delta 168)` and why SP ends up
past the top of WRAM -- the first differing word against hardware.

### The fix works on the game and costs the corpus

Dropping splits that (a) nothing outside targets, (b) something inside branches
BACKWARD to, (c) are owned by another function, (d) are not `entry` hints, and
then MERGING the owner's range back:

| | before | after |
|---|---|---|
| sub_07F006CA range | 0x07F006CA-0x07F006E4 | 0x07F006CA-**0x07F00722** (correct) |
| SP | 0x050100A4 (outside WRAM) | **0x0500FFF8** (hardware's exact value) |
| IRQ SP leak | 168 bytes | **none** |
| non-zero CHR writes | 0 | **857** |
| conformance | - | 77/77 |

118 splits dropped in Jack Bros alone. And across the corpus it is advancing
39 -> 34, blank 11 -> 18, Mario Clash lost. Reverted.

All four conditions were needed to keep conformance green, and each was added
only after a measured failure -- the last being that dropping without merging
leaves the owner truncated so its branch is still a cross-function transfer.
The remaining error is in which splits qualify, not in the diagnosis: the
boundary this computes for sub_07F006CA is provably the right one.

### Two more conditions tried, both still net-negative

The suspicion was that the drop condition was too loose, so a fifth was added:
the split must also LACK A PROLOGUE (no `addi -N, sp` / `st.w r31, off[sp]`
in its first few instructions), on the theory that a real function entry is
identifiable even when nothing happens to target it. It changes nothing:
117 merges instead of 118, sub_07F006CA still gets its correct range, and the
corpus still reads 34 advancing / 18 blank with Mario Clash lost.

Then the merge itself was narrowed. Extending the owner to the candidate's
`end_addr` propagates an over-estimate -- `end_addr` is a HIGH-WATER MARK, not
an extent -- so the owner was instead extended only to the last ROM byte
`byte_owner` actually attributes to it. Also unchanged: **65 -> 59 rendering,
11 -> 17 blank**, the same six-to-seven game loss.

That the loss survives both refinements says it is not the drop predicate and
not the merge arithmetic. Something else depends on splits existing at
addresses nothing targets -- most likely the interrupt handler discovery, which
harvests entry points from exactly this population. Removing 117 of them from
one game removes them corpus-wide.

Nineteen measured negatives now. The Jack Bros diagnosis stands unchanged and
is the most complete causal chain in this file; what does not stand is any
version of acting on it by deleting splits.

## Transfers into a function's interior were silently dropped

`vb_recomp_call_inner` binary-searches the function table for an EXACT match.
A transfer to an address inside a function -- which is most of what a game
computes at run time -- misses and is counted as a stub. Mario Clash makes
**128,699 of them in a 600-frame run**, and all four hot targets are interior
to a known function:

| target | x/run | |
|---|---|---|
| 07FF5898 | 127,714 | interior to sub_07FF588C [07FF588C-07FF5C50] |
| 07F000B2 | 591 | interior to sub_07F0002A [07F0002A-07F013A0] |
| 07F00DCC | 197 | interior to sub_07F0002A |
| 07F00A3E | 197 | interior to sub_07F0002A |

So the recompiler was throwing away a hundred thousand real control transfers
per run in a game that renders. `vb_interior_owner` does NOT cover this -- it
only maps confirmed functions whose first byte another function traced, and it
exists purely to detect re-entry for unwinding.

### The fix, and the two guards it needed

Each function now takes an entry address parked in `vb_recomp_entry` and turns
it into a `goto` via a dispatch block at the bottom; on a table miss the
dispatcher hands the address to the greatest function below it. The switch over
the labels that function actually declared IS the range check, so a bogus
address still lands on the default and is counted exactly as before.

Two things broke first, and both are worth knowing:

1. **Consume the parked entry BEFORE `vb_interrupt_check()`.** That call can run
   an entire interrupt handler, and every function it enters would otherwise see
   the pending transfer's target, fail to match it, and discard it.
2. **Never route an interior entry while `PSW.EP` is set.** Diverting a handler
   mid-flight abandons its C frame, so it never reaches RETI and EP stays set
   forever. Mario Clash wedged exactly that way -- `IRQ 1 LEAKED SP: 0500FFC4 ->
   0500FFC0 (delta -4)`, the vector stub's single push, then `INT blocked
   ... EP=1` for the rest of the run and a blank screen.

### And one thing that must NOT be done

Only branch targets are labels, so the 127,714-per-run target is still not
addressable. Labelling **every instruction boundary** makes it addressable and
grows Mario Clash's generated C from 492k to 810k lines -- and hangs
conformance. The stub is a safety net: a computed jump to a garbage or data
address currently does nothing, and making every byte a valid entry point turns
those into real execution and an infinite loop. Do not remove that net without
something else validating the target.

### Measured: neutral, and kept anyway

65 rendering / 11 blank before and after, with the **identical** eleven blanks,
77/77 conformance, ~3,200 of Mario Clash's dropped transfers now routed. It buys
no game. It is kept because it removes a real hole in the control-flow model
rather than because it moved the number, and because any future work on interior
addresses needs it to exist.

## Galactic Pinball: a function was made out of a call's arguments -- FIXED

GP was writing 1,232 non-zero CHR bytes and 1,024 BGMap entries and still
drawing nothing, because it wrote **no WAM entries at all** -- no world
attributes, so the renderer had nothing to draw. The heartbeat's `last block
entered: 0x07F51728` named the spot exactly.

`sub_07F5168C` ends with `jal 0x07F417B8`, and that callee is a long-call
helper: it reads its arguments INLINE from the bytes after the call site --

```
07F417BE: ld.w  0[r31], r21
07F417C2: ld.w  4[r31], r22
...
07F417D2: add   8, r31        <- return PAST the 8 argument bytes
07F417D4: jmp   [r31]
```

Those eight argument bytes at 0x07F51728 had a FUNCTION created on them by the
split pass, so they were decoded as code. The first halfword is 0x9800, which
decodes as `bp .` -- a branch to itself. `sub_07F5168C` then fell straight into
it and Galactic Pinball spun there for the rest of the run. Everything after
that point in the function -- including the three `jal`s that write the world
attributes -- never ran.

### Two general fixes, no per-game hint

The recompiler already understood inline call arguments, but only from a
hand-written `skip <target> <n>` hint, and nobody had written one for this game.

1. **Detect skip-after-call helpers.** The `add N, r31` immediately before
   `jmp [r31]` names the argument size exactly. A linear scan over jal targets
   before analysis registers them, because the trace needs the amount while it
   is walking the CALLERS.
2. **A split point must be an instruction boundary.** `safe_split_point` already
   walked the function; it now steps over inline arguments after a jal and
   refuses any cut that never coincided with a decoded instruction. A cut inside
   an instruction (or inside data) leaves both halves decoding as garbage.

`sub_07F5168C` goes from 0x07F5168C-0x07F51728 (156 bytes, ending in the
arguments) to 0x07F5168C-0x07F51846 (442 bytes, running through them), the
bogus function is gone, and:

| | before | after |
|---|---|---|
| non-zero CHR writes | 1,232 | **9,442** |
| WAM writes | **none** | yes (`other` range now 0x04610-0x3DFFE) |
| framebuffer pixels | 0 | **1,625** |

**Corpus: 65 -> 66 rendering, 11 -> 10 blank, nothing lost, 77/77 conformance.**

Worth remembering as a class: `last block entered` pointing at a function whose
first instruction is a branch to itself means the decode is on data, not that
the game is waiting for something.

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
