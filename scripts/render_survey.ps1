# Survey every built corpus game's RENDER state, to find the next shared renderer
# bugs. For each game (heartbeat + periodic shots on), capture:
#   - CHR loaded?           (VRAM-write stats: nonzero CHR writes)
#   - worlds with content?  (render-chain: an enabled L/R world whose BGMap has cells)
#   - framebuffer drawn?    (driver: max non-zero framebuffer pixels at a shot)
# and categorize:
#   RENDERS            fb > threshold
#   LOADED-NOT-DRAWING fb==0 but CHR>0 and a content world exists  (RENDERER BUG)
#   NO-GRAPHICS        CHR==0                                       (game-state stall)
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build_all"),
    [string]$RomDir   = (Join-Path $PSScriptRoot "..\roms"),
    [string]$Corpus   = (Join-Path $PSScriptRoot "..\corpus"),
    [int]$Frames      = 1200,
    [int]$ShotEvery   = 300,
    [int]$TimeoutSec  = 22,
    [string]$SDL2Dll  = "C:/vcpkg/installed/x64-windows/bin/SDL2.dll"
)
$ErrorActionPreference = "Stop"
$meta = @{}
foreach ($r in (Get-Content (Join-Path $Corpus "results.json") -Raw | ConvertFrom-Json)) { $meta[$r.slug] = $r.name }
$rel = Join-Path $BuildDir "games\Release"
Copy-Item $SDL2Dll $rel -Force
$env:PATH = (Split-Path $SDL2Dll) + ";" + $env:PATH
$env:VBRECOMP_HEADLESS = "1"; $env:VBRECOMP_HEADLESS_FRAMES = "$Frames"
$env:VBRECOMP_HEARTBEAT = "1"; $env:VBRECOMP_SHOT_EVERY = "$ShotEvery"

$exes = Get-ChildItem (Join-Path $rel "corpus_*.exe") | Sort-Object Name
$results = @()
$i = 0
foreach ($exe in $exes) {
    $i++
    $slug = $exe.BaseName -replace '^corpus_',''
    $rom = Join-Path $RomDir ("{0}.vb" -f $meta[$slug])
    if (-not (Test-Path $rom)) { continue }
    $err = "$env:TEMP\rs_$slug.txt"
    $env:VBRECOMP_SHOT_PATH = "$env:TEMP\rs_shot.png"
    $p = Start-Process -FilePath $exe.FullName -ArgumentList "`"$rom`"" -PassThru -WindowStyle Hidden `
        -RedirectStandardError $err -RedirectStandardOutput "$env:TEMP\rs_out.txt"
    if (-not $p.WaitForExit($TimeoutSec * 1000)) { try { $p.Kill($true) } catch {} }

    $lines = Get-Content $err -EA SilentlyContinue
    # CHR nonzero writes (last VRAM-writes line)
    $chr = 0
    $vw = $lines | Select-String "VRAM writes: CHR=(\d+)/" | Select-Object -Last 1
    if ($vw -and $vw -match "CHR=(\d+)/") { $chr = [int]$matches[1] }
    # any enabled world (L1 or R1) with BGMap cells?
    $contentWorld = ($lines | Select-String "^  W\d+ HEAD=.* (L1|R1) .* NZ=([1-9]\d*) " | Measure-Object).Count
    # max framebuffer non-zero pixels
    $fb = 0
    foreach ($m in ($lines | Select-String "non-zero framebuffer pixels")) {
        if ($m -match "(\d+) non-zero") { $v=[int]$matches[1]; if ($v -gt $fb) { $fb = $v } }
    }
    $cat = if ($fb -gt 30) { "RENDERS" }
           elseif ($chr -gt 0 -and $contentWorld -gt 0) { "LOADED-NOT-DRAWING" }
           elseif ($chr -gt 0) { "CHR-NO-WORLD" }
           else { "NO-GRAPHICS" }
    $results += [pscustomobject]@{ slug=$slug; name=$meta[$slug]; chr=$chr; world=$contentWorld; fb=$fb; cat=$cat }
    Write-Host ("{0,-3} {1,-42} CHR={2,-5} worlds={3,-3} fb={4,-6} {5}" -f $i, $slug.Substring(0,[Math]::Min(42,$slug.Length)), $chr, $contentWorld, $fb, $cat)
}
Write-Host ""
Write-Host "=== Category counts ==="
$results | Group-Object cat | Sort-Object Count -Descending | ForEach-Object { Write-Host ("  {0,-20} {1}" -f $_.Name, $_.Count) }
$results | ConvertTo-Json -Depth 3 | Out-File -Encoding utf8 (Join-Path $Corpus "render_survey.json")
Write-Host ""
Write-Host "=== LOADED-NOT-DRAWING (likely shared renderer bugs) ==="
$results | Where-Object { $_.cat -eq "LOADED-NOT-DRAWING" } | ForEach-Object { Write-Host ("  " + $_.name) }
