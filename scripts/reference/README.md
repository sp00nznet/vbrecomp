# Reference comparison

Every other check here measures vbrecomp against itself. These measure it
against **real hardware behaviour**, using BizHawk's Mednafen Virtual Boy core
as the oracle — which is the one thing the corpus work kept needing and did not
have.

```powershell
$bh  = "G:\recomp\snes\bizhawk\EmuHawk.exe"
$rom = "roms\Jack Bros. (USA).vb"
& $bh "$rom" --lua="scripts\reference\fingerprint.lua"
# then read %TEMP%\vbpc_out.txt
```

`fingerprint.lua` samples, every 30 frames: how much of WRAM and of CHR VRAM is
non-zero. `probe.lua` does the same for one specific WRAM range. Both write to
`%TEMP%` and exit the emulator when done, so they run unattended.

## Memory domains

The core names them `DRAM` (the 64 KB the VB calls WRAM), `CHR RAM` (the
character half of VIP memory), `Framebuffer`, `ROM` and `CARTRAM`. Matching on
"wram"/"vram" finds nothing — use the exact names.

Memory-write callbacks (`event.on_bus_write`, `event.onmemorywrite`) do **not**
fire for this waterbox core, and `emu.getregisters()` returns an empty table, so
PC is unavailable. What works is sampling memory per frame. That is enough to
locate a divergence in time, which is what the vbrecomp-side tools
(`vb_recomp_trace`, `VBRECOMP_WATCH`, `VBRECOMP_WWATCH`) then localise in code.

## What it has already settled

**Jack Bros.** vbrecomp reports `CHR=0/8192` — 8,192 character writes, all zero
— and a blank screen. The chase from the vbrecomp side led to a WRAM buffer at
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
