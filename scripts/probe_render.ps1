# Run-only probe: boot each already-built corpus exe at a higher frame budget
# and record which ones draw visible output. Reuses build_all/ exes (no rebuild).
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build_all"),
    [string]$ShotDir  = (Join-Path $PSScriptRoot "..\corpus\_shots1500"),
    [string]$RomDir   = (Join-Path $PSScriptRoot "..\roms"),
    [string]$Corpus   = (Join-Path $PSScriptRoot "..\corpus"),
    [int]$Frames      = 1500,
    [int]$RunTimeoutSec = 30,
    [string]$SDL2Dll  = "C:/vcpkg/installed/x64-windows/bin/SDL2.dll"
)
$ErrorActionPreference = "Stop"
$ShotDir = [System.IO.Path]::GetFullPath($ShotDir)
$RomDir  = [System.IO.Path]::GetFullPath($RomDir)
New-Item -ItemType Directory -Force $ShotDir | Out-Null

$meta = @{}
foreach ($r in (Get-Content (Join-Path $Corpus "results.json") -Raw | ConvertFrom-Json)) { $meta[$r.slug] = $r.name }
$relDir = Join-Path $BuildDir "games\Release"
Copy-Item $SDL2Dll $relDir -Force
$env:PATH = (Split-Path $SDL2Dll) + ";" + $env:PATH

$exes = Get-ChildItem (Join-Path $relDir "corpus_*.exe") | Sort-Object Name
$rendered = @()
$i = 0
foreach ($exe in $exes) {
    $i++
    $slug = $exe.BaseName -replace '^corpus_',''
    $rom = Join-Path $RomDir ("{0}.vb" -f $meta[$slug])
    if (-not (Test-Path $rom)) { continue }
    $shot = Join-Path $ShotDir "$slug.png"
    if (Test-Path $shot) { Remove-Item $shot -Force }
    $env:VBRECOMP_HEADLESS="1"; $env:VBRECOMP_HEADLESS_FRAMES="$Frames"; $env:VBRECOMP_SHOT_PATH=$shot
    $p = Start-Process -FilePath $exe.FullName -ArgumentList "`"$rom`"" -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput "$env:TEMP\probe_out.txt" -RedirectStandardError "$env:TEMP\probe_err.txt"
    if (-not $p.WaitForExit($RunTimeoutSec * 1000)) { try { $p.Kill($true) } catch {} }
    $bytes = if (Test-Path $shot) { (Get-Item $shot).Length } else { 0 }
    $drew = $bytes -gt 3800
    if ($drew) { $rendered += $meta[$slug] }
    Write-Host ("{0,-3} {1,-44} {2,7}b {3}" -f $i, $slug.Substring(0,[Math]::Min(44,$slug.Length)), $bytes, $(if($drew){"DREW"}else{""}))
}
Write-Host ""
Write-Host "Rendered at $Frames frames: $($rendered.Count)"
$rendered | ForEach-Object { Write-Host "  $_" }
