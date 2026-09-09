# SPDX-License-Identifier: MIT
# Profile-guided function discovery.
#
# Static analysis cannot find a jump target the game computes at run time, so
# vb_recomp_call has nothing to dispatch to and the call silently does nothing.
# Vertical Force made 795,657 such calls in a 600-frame run -- three addresses in
# lockstep 265,087 times each, which is a main loop calling all three every
# iteration and getting nothing back.
#
# But the runtime already COUNTS those addresses (vb_recomp_stub, printed by the
# heartbeat). Feeding them back as `entry` hints makes the recompiler emit them,
# which lets the game reach code that computes the NEXT set of targets. So this
# runs the game, harvests what it could not reach, regenerates, and repeats until
# the stubs stop appearing or stop changing.
#
#   ./scripts/discover.ps1 -Slug Vertical_Force_USA
#   ./scripts/discover.ps1 -Slug Vertical_Force_USA -Rounds 12
#
# Hints accumulate in games/hints/<slug>.hints.txt, which is committed: the
# discovery is a property of the game, not of one run.
param(
    [Parameter(Mandatory=$true)][string]$Slug,
    [int]$Rounds     = 8,
    [int]$Frames     = 600,
    [int]$TimeoutSec = 70,
    [string]$Press   = "303C",
    [string]$Root    = "",
    [string]$SDL2Dll = "C:/vcpkg/installed/x64-windows/bin/SDL2.dll"
)
$ErrorActionPreference = "Stop"
# $PSScriptRoot is not bound at param-time once the block carries a Mandatory
# attribute (PS 5.1), so resolve the repo root here instead of in the default.
if (-not $Root) { $Root = Join-Path $PSScriptRoot ".." }
$Root     = [System.IO.Path]::GetFullPath($Root)
$corpus   = Join-Path $Root "corpus"
$buildAll = Join-Path $Root "build_all"
$tool     = Join-Path $Root "build\Release\v810recomp.exe"
$hintDir  = Join-Path $Root "games\hints"
New-Item -ItemType Directory -Force $hintDir | Out-Null
$hints    = Join-Path $hintDir "$Slug.hints.txt"

$meta = @{}
$parsed = Get-Content (Join-Path $corpus "results.json") -Raw | ConvertFrom-Json
foreach ($r in $parsed) { $meta[$r.slug] = $r.name }
$rom = Join-Path $Root "roms\$($meta[$Slug]).vb"
if (-not (Test-Path $rom)) { throw "ROM not found for $Slug" }

if (-not (Test-Path $hints)) {
    "# Profile-guided entry points for $($meta[$Slug])." | Out-File -Encoding ascii $hints
    "# Addresses the game jumps to at run time, harvested by scripts/discover.ps1" |
        Out-File -Encoding ascii -Append $hints
    "# from the runtime's unimplemented-call counter. Regenerate with --hints." |
        Out-File -Encoding ascii -Append $hints
}

$env:PATH = (Split-Path $SDL2Dll) + ";" + $env:PATH
Copy-Item $SDL2Dll (Join-Path $buildAll "games\Release") -Force -EA SilentlyContinue

$known = @{}
foreach ($l in Get-Content $hints) {
    if ($l -match '^\s*entry\s+([0-9A-Fa-f]{8})') { $known[$matches[1].ToUpper()] = $true }
}

for ($round = 1; $round -le $Rounds; $round++) {
    # Regenerate and rebuild with whatever is known so far.
    $prevEA = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    & $tool $rom (Join-Path $corpus $Slug) --hints $hints 2>&1 | Out-Null
    Get-Process -Name "corpus_$Slug" -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
    $buildOut = (& cmake --build $buildAll --config Release --target "corpus_$Slug" 2>&1 | Out-String)
    $ErrorActionPreference = $prevEA
    if ($buildOut -match ': error|LNK\d') {
        Write-Host "round $round : BUILD FAILED"
        ($buildOut -split "`n" | Where-Object { $_ -match ': error|LNK\d' } | Select-Object -First 1)
        break
    }

    # Run with the heartbeat so the stub counter prints.
    $err = Join-Path $env:TEMP "discover_$Slug.txt"
    $env:VBRECOMP_HEADLESS = "1"; $env:VBRECOMP_HEADLESS_FRAMES = "$Frames"
    $env:VBRECOMP_HEARTBEAT = "1"
    if ($Press) { $env:VBRECOMP_PRESS = $Press } else { Remove-Item env:VBRECOMP_PRESS -EA SilentlyContinue }
    Remove-Item env:VBRECOMP_SHOT_PATH -EA SilentlyContinue
    Remove-Item env:VBRECOMP_SHOT_EVERY -EA SilentlyContinue
    $p = Start-Process -FilePath (Join-Path $buildAll "games\Release\corpus_$Slug.exe") `
        -ArgumentList "`"$rom`"" -PassThru -WindowStyle Hidden `
        -RedirectStandardError $err -RedirectStandardOutput (Join-Path $env:TEMP "discover_out.txt")
    if (-not $p.WaitForExit($TimeoutSec * 1000)) { try { $p.Kill() } catch {} }

    $lines = Get-Content $err -EA SilentlyContinue
    $total = 0
    $t = $lines | Select-String "unimplemented calls: (\d+) total" | Select-Object -Last 1
    if ($t -and $t -match "unimplemented calls: (\d+) total") { $total = [int]$matches[1] }

    $new = @()
    foreach ($m in ($lines | Select-String "^\s+sub_([0-9A-F]{8}) x(\d+)")) {
        if ($m -match "sub_([0-9A-F]{8}) x(\d+)") {
            $a = $matches[1].ToUpper()
            if (-not $known.ContainsKey($a)) { $known[$a] = $true; $new += $a }
        }
    }

    $chr = ""
    $v = $lines | Select-String "VRAM writes: CHR=(\d+)/(\d+)" | Select-Object -Last 1
    if ($v -and $v -match "CHR=(\d+)/(\d+).*OAM=(\d+)") { $chr = "CHR=$($matches[1])/$($matches[2]) OAM=$($matches[3])" }

    Write-Host ("round {0}: {1,9} unimplemented, {2} new entr(ies)  {3}" -f $round, $total, $new.Count, $chr)
    if ($new.Count -eq 0) { Write-Host "  nothing new to discover; stopping."; break }
    foreach ($a in $new) { "entry $a" | Out-File -Encoding ascii -Append $hints }
}

Write-Host ""
Write-Host "Hints: $hints ($($known.Count) entries)"
