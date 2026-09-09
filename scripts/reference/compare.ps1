# SPDX-License-Identifier: MIT
# Diff a game's WRAM against real hardware, and say where it first diverges.
#
# This is the procedure that located Jack Bros' task table (its scheduler never
# resumes a task), Capitan Sevilla 3D's half-built object (+8 and +16 null where
# hardware has pointers), and Null Pointer Mockup's divergence at the very first
# global. It was a sequence of hand-typed steps every time, which is how the
# wrong memory domain went unnoticed for so long -- see reference/README.md.
#
#   ./scripts/reference/compare.ps1 -Slug Jack_Bros_USA -RefFrame 90 -OurFrame 202
#
# Hardware frames and ours are NOT the same clock (ours is VBRECOMP_FRAME_TICKS
# interrupt checks); pick -OurFrame well past the point the game has settled and
# treat "hardware has this, we do not" as the signal, not exact equality.
param(
    [Parameter(Mandatory=$true)][string]$Slug,
    [int]$RefFrame = 90,
    [int]$OurFrame = 450,
    [string]$Press  = "303C",
    [int]$Pages     = 12,
    [int]$Words     = 10,
    [string]$Root   = "",
    [string]$BizHawk = "G:\recomp\snes\bizhawk\EmuHawk.exe"
)
$ErrorActionPreference = "Stop"
if (-not $Root) { $Root = Join-Path $PSScriptRoot "..\.." }
$Root = [System.IO.Path]::GetFullPath($Root)

$meta = @{}
foreach ($r in (Get-Content (Join-Path $Root "corpus\results.json") -Raw | ConvertFrom-Json)) {
    $meta[$r.slug] = $r.name
}
if (-not $meta.ContainsKey($Slug)) { throw "unknown slug: $Slug" }
$rom = Join-Path $Root "roms\$($meta[$Slug]).vb"
$exe = Join-Path $Root "build_all\games\Release\corpus_$Slug.exe"
if (-not (Test-Path $rom)) { throw "ROM not found: $rom" }
if (-not (Test-Path $exe)) { throw "not built: $exe" }

$refBin = Join-Path $env:TEMP "cmp_ref_$Slug.bin"
$ourBin = Join-Path $env:TEMP "cmp_our_$Slug.bin"
Remove-Item $refBin, $ourBin -ErrorAction SilentlyContinue

Write-Host "hardware: frame $RefFrame ..." -NoNewline
$env:VBREF_FRAME = "$RefFrame"; $env:VBREF_OUT = $refBin
$lua = Join-Path $PSScriptRoot "dumpwram.lua"
$p = Start-Process -FilePath $BizHawk -ArgumentList @("`"$rom`"", "--lua=$lua") -PassThru
if (-not $p.WaitForExit(180000)) { try { $p.Kill() } catch {} }
if (-not (Test-Path $refBin)) { throw "no hardware dump -- is BizHawk at $BizHawk?" }
Write-Host " ok"

Write-Host "ours:     frame $OurFrame ..." -NoNewline
$env:VBRECOMP_HEADLESS = "1"
$env:VBRECOMP_HEADLESS_FRAMES = "$([Math]::Max($OurFrame + 150, 600))"
$env:VBRECOMP_WRAM_DUMP = $ourBin
$env:VBRECOMP_WRAM_FRAME = "$OurFrame"
if ($Press) { $env:VBRECOMP_PRESS = $Press } else { Remove-Item env:VBRECOMP_PRESS -EA SilentlyContinue }
Remove-Item env:VBRECOMP_HEARTBEAT -EA SilentlyContinue
$q = Start-Process -FilePath $exe -ArgumentList "`"$rom`"" -PassThru -WindowStyle Hidden
if (-not $q.WaitForExit(300000)) { try { $q.Kill() } catch {} }
if (-not (Test-Path $ourBin)) { throw "no dump from $exe" }
Write-Host " ok`n"

$py = @"
import struct
from collections import Counter
ref = open(r'$refBin','rb').read()
our = open(r'$ourBin','rb').read()
print('non-zero WRAM: hardware %d, ours %d' % (sum(1 for b in ref if b), sum(1 for b in our if b)))
diff = [i for i,(a,b) in enumerate(zip(ref,our)) if a and not b]
print('bytes hardware has that we do not: %d' % len(diff))
nzref = sum(1 for b in ref if b); nzour = sum(1 for b in our if b)
if nzref <= nzour:
    print()
    print('  !! hardware has no more state than we do at this frame.')
    print('     -RefFrame is probably TOO EARLY -- the game has not done its')
    print('     work yet, so "we match" here means nothing. Raise it and')
    print('     re-run before drawing any conclusion.')
if not diff:
    raise SystemExit
print()
print('pages:')
for pg,n in sorted(Counter(i>>8 for i in diff).items())[:${Pages}]:
    print('  0x%08X  %3d' % (0x05000000+(pg<<8), n))
print()
print('first differing words:')
seen=set(); shown=0
for i in diff:
    base=(i//4)*4
    if base in seen: continue
    seen.add(base)
    print('  0x%08X  hardware=%08X  ours=%08X'
          % (0x05000000+base, struct.unpack_from('<I',ref,base)[0],
             struct.unpack_from('<I',our,base)[0]))
    shown += 1
    if shown >= ${Words}: break
"@
$py | python -
