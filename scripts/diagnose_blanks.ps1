# Diagnose every game that draws nothing, and CLUSTER them by what they poll.
#
# Chasing these one at a time is archaeology; the point of doing all of them in
# one pass is that the top hot-read address is a fingerprint. Games stuck on the
# same address are stuck on the same missing piece of hardware behaviour, so the
# clusters this prints are the shared fixes, ranked by how many ROMs each would
# unblock.
#
# Reads corpus/render_survey.json for the verdict, re-runs the non-drawing games
# with the runtime heartbeat on, and writes corpus/blank_clusters.json.
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build_all"),
    [string]$RomDir   = (Join-Path $PSScriptRoot "..\roms"),
    [string]$Corpus   = (Join-Path $PSScriptRoot "..\corpus"),
    [int]$Frames      = 400,
    [int]$TimeoutSec  = 40,
    [string]$Press    = "303C",
    [string]$SDL2Dll  = "C:/vcpkg/installed/x64-windows/bin/SDL2.dll"
)
$ErrorActionPreference = "Stop"
$Corpus = [System.IO.Path]::GetFullPath($Corpus)
$RomDir = [System.IO.Path]::GetFullPath($RomDir)

# PS5.1 hands a JSON array to the pipeline as ONE object, so bind first.
$meta = @{}
$parsed = Get-Content (Join-Path $Corpus "results.json") -Raw | ConvertFrom-Json
foreach ($r in $parsed) { $meta[$r.slug] = $r.name }

$raw = [IO.File]::ReadAllText((Join-Path $Corpus "render_survey.json"))
$i = $raw.IndexOfAny([char[]]@('[', '{'))
$survey = $raw.Substring($i) | ConvertFrom-Json
$targets = @($survey | Where-Object { $_.cat -ne "RENDERS" } | ForEach-Object { $_.slug })
Write-Host "$($targets.Count) games draw nothing; diagnosing."

$rel = Join-Path $BuildDir "games\Release"
Copy-Item $SDL2Dll $rel -Force
$env:PATH = (Split-Path $SDL2Dll) + ";" + $env:PATH
$env:VBRECOMP_HEADLESS = "1"; $env:VBRECOMP_HEADLESS_FRAMES = "$Frames"
$env:VBRECOMP_HEARTBEAT = "1"; $env:VBRECOMP_PRESS = $Press
Remove-Item env:VBRECOMP_SHOT_PATH -EA SilentlyContinue
Remove-Item env:VBRECOMP_SHOT_EVERY -EA SilentlyContinue
Remove-Item env:VBRECOMP_WATCH -EA SilentlyContinue
Remove-Item env:VBRECOMP_PEEK -EA SilentlyContinue

$results = @()
$n = 0
foreach ($slug in $targets) {
    $n++
    $exe = Join-Path $rel "corpus_$slug.exe"
    $rom = Join-Path $RomDir ("{0}.vb" -f $meta[$slug])
    if (-not (Test-Path $exe) -or -not (Test-Path $rom)) { continue }
    $err = "$env:TEMP\db_$slug.txt"
    $p = Start-Process -FilePath $exe -ArgumentList "`"$rom`"" -PassThru -WindowStyle Hidden `
        -RedirectStandardError $err -RedirectStandardOutput "$env:TEMP\db_out.txt"
    if (-not $p.WaitForExit($TimeoutSec * 1000)) { try { $p.Kill() } catch {} }

    $lines = Get-Content $err -EA SilentlyContinue
    # Busiest read address across the whole run: the fingerprint.
    $top = ""; $topCount = 0
    foreach ($m in ($lines | Select-String "hot read 0x([0-9A-F]{8}) x(\d+)")) {
        if ($m -match "hot read 0x([0-9A-F]{8}) x(\d+)") {
            $c = [int]$matches[2]
            if ($c -gt $topCount) { $topCount = $c; $top = $matches[1] }
        }
    }
    $chr = 0
    $vw = $lines | Select-String "VRAM writes: CHR=(\d+)/" | Select-Object -Last 1
    if ($vw -and $vw -match "CHR=(\d+)/") { $chr = [int]$matches[1] }
    $intenb = ""
    $hb = $lines | Select-String "INTENB=([0-9A-F]{4})" | Select-Object -Last 1
    if ($hb -and $hb -match "INTENB=([0-9A-F]{4})") { $intenb = $matches[1] }
    $stub = ""
    $st = $lines | Select-String "^    sub_([0-9A-F]{8}) x(\d+)" | Select-Object -First 1
    if ($st -and $st -match "sub_([0-9A-F]{8}) x(\d+)") { $stub = "sub_$($matches[1])" }

    $results += [pscustomobject]@{ slug=$slug; name=$meta[$slug]; top=$top;
                                   count=$topCount; chr=$chr; intenb=$intenb; stub=$stub }
    Write-Host ("{0,-3} {1,-42} top=0x{2,-8} x{3,-9} CHR={4,-6} INTENB={5} {6}" -f `
        $n, $slug.Substring(0,[Math]::Min(42,$slug.Length)), $top, $topCount, $chr, $intenb, $stub)
}

$results | ConvertTo-Json -Depth 3 | Out-File -Encoding utf8 (Join-Path $Corpus "blank_clusters.json")
Write-Host ""
Write-Host "=== Clusters by busiest read address (each is one shared fix) ==="
$results | Where-Object { $_.top } | Group-Object top | Sort-Object Count -Descending |
    ForEach-Object {
        Write-Host ("  0x{0}  {1} game(s)" -f $_.Name, $_.Count)
        $_.Group | ForEach-Object { Write-Host ("      " + $_.name) }
    }
