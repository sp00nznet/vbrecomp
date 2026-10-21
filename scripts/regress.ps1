# SPDX-License-Identifier: MIT
# Fast regression check for a recompiler change.
#
# The full loop is sweep + build_all + render_survey: about an hour, and all it
# returns is a score. Every control-flow attempt in this codebase has failed for
# want of a check that says WHICH games moved, quickly enough to iterate. This
# rebuilds a fixed panel of games whose behaviour is known, runs each headless,
# and diffs the result against a baseline saved in corpus/regress_baseline.json
# (gitignored: it records results from your own ROMs).
#
#   ./scripts/regress.ps1              # check the panel against the baseline
#   ./scripts/regress.ps1 -Save        # record current behaviour as the baseline
#   ./scripts/regress.ps1 -Panel all   # every built game (slow, but still no rebuild)
#
# Each game is classified from the screenshots it leaves, exactly as
# render_survey.ps1 does it, so the verdicts mean the same thing:
#   advancing - the frame changes between samples
#   frozen    - draws one image and never changes it
#   blank     - every sample is an empty 384x224 frame
#   dead      - produced no frames at all (crash or hang)
param(
    [string]$Root     = (Join-Path $PSScriptRoot ".."),
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build_all"),
    [string]$Corpus   = (Join-Path $PSScriptRoot "..\corpus"),
    [string]$RomDir   = (Join-Path $PSScriptRoot "..\roms"),
    [string]$Tool     = (Join-Path $PSScriptRoot "..\build\Release\v810recomp.exe"),
    [int]$Frames      = 600,
    [int]$ShotEvery   = 200,
    [int]$TimeoutSec  = 45,
    [string]$Press    = "1000,0004",
    [string]$Panel    = "default",
    [switch]$Save,
    [string]$SDL2Dll  = "C:/vcpkg/installed/x64-windows/bin/SDL2.dll"
)
$ErrorActionPreference = "Stop"
$Root = [System.IO.Path]::GetFullPath($Root); $Corpus = [System.IO.Path]::GetFullPath($Corpus)
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir); $RomDir = [System.IO.Path]::GetFullPath($RomDir)

# The panel. One game per behaviour worth protecting, chosen because each has
# been the thing that broke (or the thing that moved) in a past attempt.
$panelGames = @(
    "Waterworld_USA"                    # advancing; the input-gate fix
    "Mario_Clash_Japan_USA_En"          # advancing; died under the trampoline
    "Teleroboxer_Japan_USA_En"          # frozen;    died under the trampoline
    "Mario_s_Tennis_Japan_USA_En"       # blank;     entry returns after 0 frames
    "Golf_USA"                          # blank;     drew on the tail-jump branch
    "Virtual_Bowling_Japan"             # blank;     entry returns
    "V_Tetris_Japan"                    # frozen
    "Panic_Bomber_USA"                  # frozen
    "Bad_Apple_World_Demo_Hardware_Version_Aftermarket_Homebrew"  # advancing
    "Red_Alarm_Japan"                   # renders only with NO input held
    "Bound_High_Japan_En_Proto"         # drew the warning screen, regressed to blank
    "SD_Gundam_Dimension_War_Japan"     # the ROM that looked like a recompiler hang
)

$meta = @{}
$parsed = Get-Content (Join-Path $Corpus "results.json") -Raw | ConvertFrom-Json
foreach ($r in $parsed) { $meta[$r.slug] = $r.name }

if ($Panel -eq "all") {
    $games = @(Get-ChildItem (Join-Path $BuildDir "games\Release") -Filter "corpus_*.exe" |
               ForEach-Object { $_.BaseName -replace '^corpus_','' } | Sort-Object)
} else {
    $games = $panelGames
}

$rel = Join-Path $BuildDir "games\Release"
$env:PATH = (Split-Path $SDL2Dll) + ";" + $env:PATH
$env:VBRECOMP_HEADLESS = "1"; $env:VBRECOMP_HEADLESS_FRAMES = "$Frames"
$env:VBRECOMP_SHOT_EVERY = "$ShotEvery"
if ($Press) { $env:VBRECOMP_PRESS = $Press } else { Remove-Item env:VBRECOMP_PRESS -EA SilentlyContinue }
foreach ($v in @("VBRECOMP_HEARTBEAT","VBRECOMP_WATCH","VBRECOMP_PEEK")) {
    Remove-Item "env:$v" -EA SilentlyContinue
}

$results = @{}
foreach ($slug in $games) {
    $rom = Join-Path $RomDir ("{0}.vb" -f $meta[$slug])
    if (-not (Test-Path $rom)) { continue }

    # Regenerate and rebuild only this game, so the check reflects the current
    # recompiler rather than whatever was built last.
    if ($Panel -ne "all") {
        # Native tools write progress to stderr, and under
        # ErrorActionPreference=Stop PowerShell turns that into a terminating
        # error. Drop to Continue around the native calls only.
        $prevEA = $ErrorActionPreference; $ErrorActionPreference = "Continue"
        & $Tool $rom (Join-Path $Corpus $slug) 2>&1 | Out-Null
        Get-Process -Name "corpus_$slug" -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
        $buildOut = (& cmake --build $BuildDir --config Release --target "corpus_$slug" 2>&1 | Out-String)
        $ErrorActionPreference = $prevEA
        if ($buildOut -match ': error|LNK\d') {
            $results[$slug] = "BUILD-FAIL"
            Write-Host ("{0,-46} BUILD-FAIL" -f $slug); continue
        }
    }

    $d = Join-Path $env:TEMP "regress\$slug"
    if (Test-Path $d) { Remove-Item $d -Recurse -Force }
    New-Item -ItemType Directory -Force $d | Out-Null
    $env:VBRECOMP_SHOT_PATH = (Join-Path $d "f.png")

    $p = Start-Process -FilePath (Join-Path $rel "corpus_$slug.exe") -ArgumentList "`"$rom`"" `
        -PassThru -WindowStyle Hidden -RedirectStandardError "$env:TEMP\regress_err.txt" `
        -RedirectStandardOutput "$env:TEMP\regress_out.txt"
    if (-not $p.WaitForExit($TimeoutSec * 1000)) { try { $p.Kill() } catch {} }

    # Some games render only with NO input held -- Red Alarm draws its title and
    # goes blank when buttons are pressed -- so a blank result is retried without
    # input before it counts as blank. Missing this hid a Red Alarm regression
    # for most of a day.
    $shots = @(Get-ChildItem $d -Filter "f.png.*.png" -EA SilentlyContinue | Sort-Object Name)
    if ($shots.Count -gt 0 -and ($shots | Where-Object { $_.Length -gt 3800 }).Count -eq 0 -and $Press) {
        Remove-Item env:VBRECOMP_PRESS -EA SilentlyContinue
        Remove-Item $d -Recurse -Force -EA SilentlyContinue
        New-Item -ItemType Directory -Force $d | Out-Null
        $p2 = Start-Process -FilePath (Join-Path $rel "corpus_$slug.exe") -ArgumentList "`"$rom`"" `
            -PassThru -WindowStyle Hidden -RedirectStandardError (Join-Path $env:TEMP "regress_err.txt") `
            -RedirectStandardOutput (Join-Path $env:TEMP "regress_out.txt")
        if (-not $p2.WaitForExit($TimeoutSec * 1000)) { try { $p2.Kill() } catch {} }
        $env:VBRECOMP_PRESS = $Press
        $shots = @(Get-ChildItem $d -Filter "f.png.*.png" -EA SilentlyContinue | Sort-Object Name)
    }
    $expected = [Math]::Floor($Frames / $ShotEvery)
    $hashes = @($shots | ForEach-Object { (Get-FileHash $_.FullName -Algorithm MD5).Hash })
    $allBlank = ($shots.Count -gt 0) -and (($shots | Where-Object { $_.Length -gt 3800 }).Count -eq 0)

    $verdict = if ($shots.Count -lt $expected) { "dead" }
               elseif ($allBlank)              { "blank" }
               elseif (($hashes | Select-Object -Unique).Count -gt 1) { "advancing" }
               else { "frozen" }
    $results[$slug] = $verdict
    Write-Host ("{0,-46} {1}" -f $slug, $verdict)
}

$baselinePath = Join-Path $Corpus "regress_baseline.json"
if ($Save) {
    $results | ConvertTo-Json -Depth 3 | Out-File -Encoding utf8 $baselinePath
    Write-Host ""; Write-Host "Baseline saved: $baselinePath"
    exit 0
}

if (-not (Test-Path $baselinePath)) {
    Write-Host ""; Write-Host "No baseline yet. Run with -Save to record one."
    exit 0
}
$rawB = [IO.File]::ReadAllText($baselinePath)
$i = $rawB.IndexOfAny([char[]]@('{','['))
$base = $rawB.Substring($i) | ConvertFrom-Json

$better = 0; $worse = 0
$rank = @{ "dead" = 0; "blank" = 1; "frozen" = 2; "advancing" = 3; "BUILD-FAIL" = -1 }
Write-Host ""; Write-Host "=== Changes against baseline ==="
foreach ($slug in $results.Keys | Sort-Object) {
    $was = $base.$slug
    $now = $results[$slug]
    if (-not $was -or $was -eq $now) { continue }
    $dir = if ($rank[$now] -gt $rank[$was]) { $better++; "BETTER" } else { $worse++; "WORSE " }
    Write-Host ("  {0}  {1,-44} {2} -> {3}" -f $dir, $slug, $was, $now)
}
if ($better -eq 0 -and $worse -eq 0) { Write-Host "  (none)" }
Write-Host ""
Write-Host "$better better, $worse worse, $($results.Count) games checked."
if ($worse -gt 0) { exit 1 }
