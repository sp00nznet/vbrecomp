# Extract all Virtual Boy ROMs from the No-Intro archive set into roms/.
# Each archive holds a single same-named .vb; extraction is flat (no-intro
# names don't collide). Sequential, no parallelism (resource safety).
param(
    [string]$Src = "Z:\roms\Nintendo Virtual Boy\vb complete",
    [string]$Dst = (Join-Path $PSScriptRoot "..\roms")
)
$ErrorActionPreference = "Stop"
$sevenzip = "C:\Program Files\7-Zip\7z.exe"
if (-not (Test-Path $sevenzip)) { throw "7z not found at $sevenzip" }

$Dst = [System.IO.Path]::GetFullPath($Dst)
New-Item -ItemType Directory -Force -Path $Dst | Out-Null

$archives = Get-ChildItem -Path $Src -File | Where-Object { $_.Extension -in '.7z', '.zip' }
Write-Host "Extracting $($archives.Count) archives from $Src -> $Dst"

$n = 0
foreach ($a in $archives) {
    $n++
    & $sevenzip e -y -o"$Dst" "$($a.FullName)" "*.vb" > $null 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  [$n] FAILED: $($a.Name)"
    }
}

$vbs = Get-ChildItem -Path $Dst -Filter *.vb -File
Write-Host "Done. $($vbs.Count) .vb files in $Dst"
