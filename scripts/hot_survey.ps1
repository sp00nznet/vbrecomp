# Survey the hot read addresses across many blank games to find the next common
# boot blocker. Runs each built corpus exe briefly with the heartbeat/tracer on,
# extracts the busiest non-WRAM (register) read, and tallies them.
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build_all"),
    [string]$RomDir   = (Join-Path $PSScriptRoot "..\roms"),
    [string]$Corpus   = (Join-Path $PSScriptRoot "..\corpus"),
    [int]$Frames      = 600,
    [int]$TimeoutSec  = 20,
    [string]$SDL2Dll  = "C:/vcpkg/installed/x64-windows/bin/SDL2.dll"
)
$ErrorActionPreference = "Stop"
$meta = @{}
foreach ($r in (Get-Content (Join-Path $Corpus "results.json") -Raw | ConvertFrom-Json)) { $meta[$r.slug] = $r.name }
$rel = Join-Path $BuildDir "games\Release"
Copy-Item $SDL2Dll $rel -Force
$env:PATH = (Split-Path $SDL2Dll) + ";" + $env:PATH
$env:VBRECOMP_HEADLESS = "1"; $env:VBRECOMP_HEADLESS_FRAMES = "$Frames"; $env:VBRECOMP_HEARTBEAT = "1"

# A representative spread of retail games that currently render blank.
$slugs = @(
    "3_D_Tetris_USA","Teleroboxer_Japan_USA_En","Jack_Bros_USA","Panic_Bomber_USA",
    "Mario_s_Tennis_Japan_USA_En","Vertical_Force_USA","Golf_USA","Nester_s_Funky_Bowling_USA",
    "Space_Squash_Japan","Insmouth_no_Yakata_Japan","Mario_Clash_Japan_USA_En","Virtual_Lab_Japan"
)
$tally = @{}
foreach ($slug in $slugs) {
    $exe = Join-Path $rel "corpus_$slug.exe"
    if (-not (Test-Path $exe)) { Write-Host "skip $slug (no exe)"; continue }
    $rom = Join-Path $RomDir ("{0}.vb" -f $meta[$slug])
    if (-not (Test-Path $rom)) { Write-Host "skip $slug (no rom)"; continue }
    $err = "$env:TEMP\hs_$slug.txt"
    $p = Start-Process -FilePath $exe -ArgumentList "`"$rom`"" -PassThru -WindowStyle Hidden `
        -RedirectStandardError $err -RedirectStandardOutput "$env:TEMP\hs_out.txt"
    if (-not $p.WaitForExit($TimeoutSec * 1000)) { try { $p.Kill($true) } catch {} }
    # Top register-region hot reads (VIP 0x0005Fxxx or hw 0x020000xx), ignore WRAM/ROM.
    $tops = Get-Content $err -EA SilentlyContinue | Select-String "hot read 0x(0005F|020000)" |
        ForEach-Object { if ($_ -match "0x([0-9A-Fa-f]{8}) x(\d+)") { [pscustomobject]@{a=$matches[1];c=[int]$matches[2]} } } |
        Sort-Object c -Descending | Select-Object -First 1
    $top = if ($tops) { $tops.a.ToUpper() } else { "(none)" }
    Write-Host ("{0,-30} top reg poll: {1}" -f $slug, $top)
    if ($top -ne "(none)") { if ($tally.ContainsKey($top)) { $tally[$top]++ } else { $tally[$top] = 1 } }
}
Write-Host ""
Write-Host "=== Common register poll targets (next blocker candidates) ==="
$tally.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { Write-Host ("  0x{0}  in {1} games" -f $_.Key, $_.Value) }
