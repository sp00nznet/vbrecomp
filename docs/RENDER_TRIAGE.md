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
| Advancing | 27 | The frame changes between samples: the game is running. |
| Drawing, then frozen | 12 | One image, byte-identical at 300/600/900. |
| Nothing on screen | 36 | Every sample blank; no character data ever written. |

"Renders something" is 39/76, but that number is the less useful of the two:
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

### 2. A different gate — 12 games

3-D BattleSnake, BLOX 2, Ballface, Bound High, Mario Clash, Nester's Funky
Bowling, Panic Bomber, SD Gundam, Teleroboxer, Tobidase! Panibon, Tron VB and
V-Tetris still sit on one frame with those buttons held. Their gate is not the
one Waterworld uses — a different button, a release edge, or a hold length.
Each needs its wait loop read the way Waterworld's was above; `VBRECOMP_WATCH=<hex>`
tallies the link register of every reader of an address, which is how that loop
was found.

### 3. Polling a display phase that never arrives — part of the 36 blanks

Golf reads `DPSTTS` (0x0005F820) 1,248,381 times and writes no character data
at all (`CHR=0/0`). `vb_vip_frame_advance()` toggles the display status bits
rather than cycling them through the real phase sequence, so a game waiting on
a specific phase order waits forever. Space Squash and Elevated Speed show the
same shape.

### 4. Garbled second character set

Bound High and V-Tetris render the English warning text correctly and the
Japanese text below it as wrong tiles. One character set lands and the other
does not, so this is the transfer of a specific tile range — not the renderer,
which is drawing exactly what it was given.

### 5. Partial glyph rows

Tron VB draws the top strokes of its logo and loses the bottoms. Hamburgers
renders a full bitmap title correctly but corrupts the small text at bottom
left. Rows within a tile are being dropped.

### 6. World placement

V-Tetris draws its text block flush left where it should be centred; SD Gundam
has a column of stray tiles down the left edge. Waterworld's round HUD renders
but its play field is empty. World X/Y or BGMap offsets are wrong for some
worlds and right for others in the same frame.

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
