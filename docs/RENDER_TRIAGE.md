# Render triage

What the whole corpus draws through the generic driver, and the categories the
failures fall into. Regenerate with:

```powershell
./scripts/render_survey.ps1              # numbers + corpus/_timeline/<slug>/
./scripts/render_survey.ps1 -Diagnose    # + heartbeat detail, ~10x slower
```

Each ROM boots headless for 900 frames and is sampled at frames 300, 600 and
900. The verdict is read off those screenshots (a blank 384x224 frame is an
all-black PNG of exactly 3,619 bytes), not off a debug counter — so it does not
depend on running the game slowly enough for the heartbeat to print, which is
what used to make this survey disagree with `build_all.ps1`.

## Where the corpus stands

| | Count | What it means |
|---|---|---|
| Advancing | 6 | A different frame at each sample point: the game is running. |
| Drawing, then frozen | 35 | One image, byte-identical at 300/600/900. |
| Nothing on screen | 34 | Every sample blank; no character data ever written. |
| **Draws something** | **41 / 76** | |

The headline is the split inside that 41. Only six games reach new state
between frame 300 and frame 900. The renderer is in better shape than the
"renders" number suggested and the *liveness* is in worse shape: thirty-five
games draw one correct screen and then stop, which no single-number metric
distinguishes from a game that is playing.

## Categories

### 1. Stuck on the boot warning screen — 35 games

Waterworld, Mario Clash, Bound High, Teleroboxer, V-Tetris, Panic Bomber and
most of the retail set draw the *IMPORTANT: READ INSTRUCTION AND PRECAUTION
BOOKLETS* screen correctly, then never advance.

Evidence (`-Diagnose`, Waterworld):

```
HB: INTPND=703E INTENB=4000 DPSTTS=0042 XPSTTS=0072 SCR=84 SP=0500FFAC
  hot read 0x05000030 x1990397
  hot read 0x0500FFC0 x1990397
  VRAM writes: CHR=7646/8192 BGMap=320 OAM=0
```

The game spins on two WRAM addresses two million times — a flag its VIP
interrupt handler is supposed to set. `INTPND` stays at `0x703E` for the whole
run, so nothing is clearing it: `vb_vip_frame_advance()` re-raises every frame
bit each frame, and the handler either never runs or never gets to its
`INTCLR` write. No unimplemented calls are counted for this game, so the
dispatch holes are ruled out — it is the interrupt path itself.

This is the single biggest lever in the corpus.

### 2. Polling a display phase that never arrives — part of the 34 blanks

Golf reads `DPSTTS` (0x0005F820) 1,248,381 times and writes no character data
at all (`CHR=0/0`). `vb_vip_frame_advance()` toggles the display status bits
rather than cycling them through the real phase sequence, so a game waiting on
a specific phase order waits forever. Space Squash and Elevated Speed show the
same shape (they load CHR but never get a world drawn).

### 3. Garbled second character set

Bound High and V-Tetris render the English warning text correctly and the
Japanese text below it as wrong tiles. One character set lands and the other
does not, so this is the transfer of a specific tile range — not the renderer,
which is drawing exactly what it was given.

### 4. Partial glyph rows

Tron VB draws the top strokes of its logo and loses the bottoms. Hamburgers
renders a full bitmap title correctly but corrupts the small text at bottom
left. Rows within a tile are being dropped.

### 5. World placement

V-Tetris draws its text block flush left where it should be centred; SD Gundam
has a column of stray tiles down the left edge. World X/Y or BGMap offsets are
wrong for some worlds and right for others in the same frame.

## What is already ruled out

- **Unimplemented calls.** Stub functions, unfound JAL callees and unresolved
  indirect jumps are counted at run time now (`vb_recomp_stub`, printed by the
  heartbeat). The stalled games sampled so far hit none of them.
- **Instruction semantics** for everything `tools/conformance.py` covers: the
  ALU and its flags, immediate extension, shifts and their carry-out, the
  64-bit multiply/divide pair, every `setf` condition, load sign-extension, the
  float unit, the bit-string block copy, and branch/call/return shapes.
