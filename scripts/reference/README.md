## compare.ps1 -- the whole procedure in one command

```powershell
./scripts/reference/compare.ps1 -Slug Jack_Bros_USA -RefFrame 90 -OurFrame 202
```

Dumps hardware WRAM at `-RefFrame`, dumps ours at `-OurFrame`, and prints the
non-zero counts, the 256-byte pages hardware has content in and we do not, and
the first differing words. That is exactly the sequence that found Jack Bros'
task table, Capitan Sevilla 3D's half-built object and Null Pointer Mockup's
first-global divergence -- typed out by hand each time, which is how the wrong
memory domain went unnoticed for as long as it did.

The two frame numbers are NOT the same clock: a hardware frame is 20 ms of real
CPU, ours is `VBRECOMP_FRAME_TICKS` interrupt checks. Pick `-OurFrame` well past
the point the game has settled and read "hardware has this, we do not" as the
signal rather than expecting equality.

﻿# Reference comparison

Every other check here measures vbrecomp against itself. These measure it
against **real hardware behaviour**, using BizHawk's Mednafen Virtual Boy core
as the oracle â€” which is the one thing the corpus work kept needing and did not
have.

```powershell
$bh  = "G:\recomp\snes\bizhawk\EmuHawk.exe"
$rom = "roms\Jack Bros. (USA).vb"
& $bh "$rom" --lua="scripts\reference\dumpwram.lua"
# then read %TEMP%\vbpc_out.txt
```

`fingerprint.lua` samples, every 30 frames: how much of WRAM and of CHR VRAM is
non-zero. `probe.lua` does the same for one specific WRAM range. Both write to
`%TEMP%` and exit the emulator when done, so they run unattended.

## Memory domains -- read the right one

`scripts/reference/domains.lua` lists them with sizes and how much of each is
non-zero, because getting this wrong produces confident, wrong conclusions.
For Jack Bros at frame 90:

```
WRAM        size=0x10000   <- the CPU's 64 KB work RAM. This is the one.
CARTRAM     size=0x10000
ROM         size=0x100000
CHR RAM     size=0x8000
DRAM        size=0x20000   <- the VIP's BGMap bank, NOT work RAM
Framebuffer size=0x18000
```

**`DRAM` is not the VB's WRAM.** It is 0x20000 bytes of VIP BGMap memory.
Reading its first 64 KB returns the empty low BGMap segments, which is exactly
what made Jack Bros' RAM look untouched on hardware and sent a whole line of
enquiry the wrong way -- the note below that said "WRAM is entirely zero on
real hardware too" was reading VIP memory. Against the real WRAM domain, Jack
Bros has 975 non-zero bytes at frame 90 and we have 591.

Memory-write callbacks (`event.on_bus_write`, `event.onmemorywrite`) do not
fire for this waterbox core, and `emu.getregisters()` returns an empty table,
so PC is unavailable. Sampling memory per frame is what works.

## What it has already settled

**Jack Bros.** vbrecomp reports `CHR=0/8192` â€” 8,192 character writes, all zero
â€” and a blank screen. The chase from the vbrecomp side led to a WRAM buffer at
`0x05003800` that `sub_07F0091C` copies into VRAM and that only ever takes two
writes, which looked like the fault.

The reference disproves that in one run: **WRAM is entirely zero on real
hardware too**, at every frame, so the empty buffer is normal and that whole
line of enquiry was misdirected. The real divergence is narrow and dated:

| | frame 1 | frame 30 | frame 60+ |
|---|---|---|---|
| reference CHR non-zero | 0 | 0 | **81** |
| vbrecomp CHR non-zero | 0 | 0 | **0** |

So the game loads character data between frames 30 and 60, and vbrecomp does
not. Both write plenty of zeros; only the reference ever writes tiles. That is
the window to search, and it is a much smaller question than "why is the screen
blank".

## Diffing memory state

`dumpwram.lua` writes the reference's whole 64 KB of WRAM at a chosen frame;
the driver's `VBRECOMP_WRAM_DUMP` / `VBRECOMP_WRAM_FRAME` do the same on our
side. Diffing the two says *where* a divergence lives, not just when.

```powershell
$env:VBREF_FRAME="10"; $env:VBREF_OUT="$env:TEMP\ref.bin"
& $bh "$rom" --lua="scripts\reference\dumpwram.lua"
$env:VBRECOMP_WRAM_DUMP="$env:TEMP\ours.bin"; $env:VBRECOMP_WRAM_FRAME="10"
build_all\games\Release\corpus_<slug>.exe "$rom"
```

### The conversion factor

Measured with Waterworld, which renders correctly on both sides, by finding
where the two agree on how much WRAM is non-zero:

| | non-zero WRAM bytes |
|---|---|
| reference, frame 120 | 320 |
| vbrecomp, frame 240 | 278 |
| vbrecomp, frame 480 | 480 |

So reference frame 120 lands near our frame 270: **one hardware frame is roughly
2.25 of ours**, at the default `VBRECOMP_FRAME_TICKS=20000`. Multiply a
reference frame number by ~2.25 before comparing state, or raise the tick count
to about 45,000 to bring the two into step.

Worth knowing: raising the tick count does NOT make the blank games draw — 2000,
5000, 60000 and 200000 were all swept against all 29, 0 of 29 — so pacing is a
measurement convenience here, not a fix.

**Caveat that remains:** the two frame counts are not naturally calibrated. A reference
frame is 20 ms of real CPU; ours is `VBRECOMP_FRAME_TICKS` interrupt checks
(20,000 by default), and the relationship between them has never been measured.
So "at frame N" is approximate across the two, and a diff shows *a* divergence
rather than *the first* one. Calibrating that â€” by finding the tick count where
a game that works on both sides reaches the same state â€” would make these diffs
exact, and is the obvious next improvement.

The domain is live and sparse, not empty: Waterworld, which renders correctly on
both sides, has 320 non-zero WRAM bytes at reference frame 120. So the near-zero
WRAM that Jack Bros and Vertical Force show early is real behaviour, not a
readback artifact. Vertical Force under vbrecomp has 8,932 non-zero WRAM bytes
at our frame 10 against the reference's 0 â€” we are doing work the hardware is
not, which is a different failure from being stuck.
