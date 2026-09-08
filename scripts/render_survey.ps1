# Survey every built corpus game's RENDER state, to find the next shared renderer
# bugs. For each game (heartbeat + periodic shots on), capture:
#   - CHR loaded?           (VRAM-write stats: nonzero CHR writes)
#   - worlds with content?  (render-chain: an enabled L/R world whose BGMap has cells)
#   - framebuffer drawn?    (driver: max non-zero framebuffer pixels at a shot)
# and categorize:
#   RENDERS            a screenshot came out non-blank
#   LOADED-NOT-DRAWING blank, but CHR>0 and a content world exists  (RENDERER BUG)
#   CHR-NO-WORLD       blank, CHR>0, no world with content          (RENDERER BUG)
#   NO-GRAPHICS        blank and no CHR at all                      (game-state stall)
#   NO-FRAMES          never reached a shot frame before the timeout
#
# The CHR / world / unimplemented-call columns need -Diagnose (they come from
# the runtime heartbeat, which is ~10x slower); the RENDERS verdict does not.
# Screenshots land in corpus/_timeline/<slug>/ as a filmstrip per game.
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build_all"),
    [string]$RomDir   = (Join-Path $PSScriptRoot "..\roms"),
    [string]$Corpus   = (Join-Path $PSScriptRoot "..\corpus"),
    [int]$Frames      = 900,
    [int]$ShotEvery   = 300,
    # Slow games (heavy status-register polling) need wall-clock to reach a
    # shot frame; a tight timeout kills them first and undercounts renders
    # (e.g. Mario Clash/Teleroboxer draw 1762px by frame 300 but were killed
    # at 22s and logged as fb=0). Give every game time to reach >=1 shot.
    [int]$TimeoutSec  = 45,
    # Where each game's filmstrip lands. The driver already writes a shot every
    # $ShotEvery frames; this survey used to point them all at one scratch file
    # and throw them away, so the only evidence a game rendered was a pixel
    # count. Keeping them is what lets a graphical fault be SEEN and sorted into
    # a category, instead of inferred from a number.
    [string]$ShotDir  = (Join-Path $PSScriptRoot "..\corpus\_timeline"),
    [string]$SDL2Dll  = "C:/vcpkg/installed/x64-windows/bin/SDL2.dll",
    # Turn on the runtime heartbeat: CHR writes, render chain, hot reads and
    # unimplemented calls, at roughly a tenth of the speed. Use it on the games
    # that did not draw, not on the whole corpus.
    [switch]$Diagnose
)
$ErrorActionPreference = "Stop"
$ShotDir = [System.IO.Path]::GetFullPath($ShotDir)
New-Item -ItemType Directory -Force $ShotDir | Out-Null
$meta = @{}
foreach ($r in (Get-Content (Join-Path $Corpus "results.json") -Raw | ConvertFrom-Json)) { $meta[$r.slug] = $r.name }
$rel = Join-Path $BuildDir "games\Release"
Copy-Item $SDL2Dll $rel -Force
$env:PATH = (Split-Path $SDL2Dll) + ";" + $env:PATH
$env:VBRECOMP_HEADLESS = "1"; $env:VBRECOMP_HEADLESS_FRAMES = "$Frames"
$env:VBRECOMP_SHOT_EVERY = "$ShotEvery"
# The heartbeat is diagnosis, not measurement. It tracks every memory read and
# periodically dumps the hardware state, which slows a game by roughly an order
# of magnitude -- enough that most never reached even the first shot frame
# before the timeout, and were then recorded as having drawn nothing. Running
# it by default is what made this survey disagree with build_all.ps1 and move
# between runs. Off unless asked for; -Diagnose turns it on (and with it the
# CHR / world / unimplemented-call detail) for the games worth chasing.
if ($Diagnose) { $env:VBRECOMP_HEARTBEAT = "1" } else { Remove-Item env:VBRECOMP_HEARTBEAT -EA SilentlyContinue }

$exes = Get-ChildItem (Join-Path $rel "corpus_*.exe") | Sort-Object Name
$results = @()
$i = 0
foreach ($exe in $exes) {
    $i++
    $slug = $exe.BaseName -replace '^corpus_',''
    $rom = Join-Path $RomDir ("{0}.vb" -f $meta[$slug])
    if (-not (Test-Path $rom)) { continue }
    $err = "$env:TEMP\rs_$slug.txt"
    # <ShotDir>\<slug>\f.png for the final frame, plus f.png.<frame>.png for
    # each periodic shot, so one game's whole run reads as a filmstrip in one
    # directory.
    $gameShots = Join-Path $ShotDir $slug
    if (Test-Path $gameShots) { Remove-Item $gameShots -Recurse -Force }
    New-Item -ItemType Directory -Force $gameShots | Out-Null
    $env:VBRECOMP_SHOT_PATH = (Join-Path $gameShots "f.png")
    $p = Start-Process -FilePath $exe.FullName -ArgumentList "`"$rom`"" -PassThru -WindowStyle Hidden `
        -RedirectStandardError $err -RedirectStandardOutput "$env:TEMP\rs_out.txt"
    # Kill() takes no argument on .NET Framework (Windows PowerShell 5.1):
    # the Kill($true) tree-kill overload is .NET Core only, so it threw, the
    # catch swallowed it, and the timed-out process survived -- holding its
    # .exe open and burning CPU for the rest of the run, which is enough to
    # time out the games that come after it. Recompiled games spawn nothing,
    # so a plain Kill() is all that was ever needed.
    if (-not $p.WaitForExit($TimeoutSec * 1000)) { try { $p.Kill() } catch {} }

    $lines = Get-Content $err -EA SilentlyContinue
    # CHR nonzero writes (last VRAM-writes line)
    $chr = 0
    $vw = $lines | Select-String "VRAM writes: CHR=(\d+)/" | Select-Object -Last 1
    if ($vw -and $vw -match "CHR=(\d+)/") { $chr = [int]$matches[1] }
    # any enabled world (L1 or R1) with BGMap cells?
    $contentWorld = ($lines | Select-String "^  W\d+ HEAD=.* (L1|R1) .* NZ=([1-9]\d*) " | Measure-Object).Count
    # Whether the game drew comes from the screenshots it left behind, not from
    # a stderr line only the heartbeat prints. A blank 384x224 frame is exactly
    # 3619 bytes; anything meaningfully larger is pixels. Reading it off the
    # PNGs means the measurement does not depend on running the game slowly.
    $shots = @(Get-ChildItem $gameShots -Filter *.png -EA SilentlyContinue | Sort-Object Name)
    $biggest = 0
    foreach ($sh in $shots) { if ($sh.Length -gt $biggest) { $biggest = $sh.Length } }
    $drew = $biggest -gt 3800
    # Unimplemented calls the run hit (heartbeat only): the analysis gap that is
    # most likely keeping this game where it is.
    $topStub = ""
    $st = $lines | Select-String "^    sub_([0-9A-F]{8}) x(\d+)" | Select-Object -First 1
    if ($st -and $st -match "sub_([0-9A-F]{8}) x(\d+)") { $topStub = "sub_$($matches[1]) x$($matches[2])" }

    $cat = if ($drew) { "RENDERS" }
           elseif ($chr -gt 0 -and $contentWorld -gt 0) { "LOADED-NOT-DRAWING" }
           elseif ($chr -gt 0) { "CHR-NO-WORLD" }
           elseif ($shots.Count -eq 0) { "NO-FRAMES" }
           else { "NO-GRAPHICS" }
    $results += [pscustomobject]@{ slug=$slug; name=$meta[$slug]; chr=$chr; world=$contentWorld;
                                   shots=$shots.Count; bytes=$biggest; stub=$topStub; cat=$cat }
    Write-Host ("{0,-3} {1,-42} shots={2,-3} px={3,-7} {4,-18} {5}" -f $i, $slug.Substring(0,[Math]::Min(42,$slug.Length)), $shots.Count, $biggest, $cat, $topStub)
}
Write-Host ""
Write-Host "=== Category counts ==="
$results | Group-Object cat | Sort-Object Count -Descending | ForEach-Object { Write-Host ("  {0,-20} {1}" -f $_.Name, $_.Count) }
$results | ConvertTo-Json -Depth 3 | Out-File -Encoding utf8 (Join-Path $Corpus "render_survey.json")
Write-Host "Timelines: $ShotDir\<slug>\ (one PNG every $ShotEvery frames)"
Write-Host ""
Write-Host "=== LOADED-NOT-DRAWING (likely shared renderer bugs) ==="
$results | Where-Object { $_.cat -eq "LOADED-NOT-DRAWING" } | ForEach-Object { Write-Host ("  " + $_.name) }
