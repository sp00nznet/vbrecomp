# Cross-validate v810recomp's function discovery against an independent Ghidra
# analysis using the V810 processor module (github.com/20Enderdude20/Ghidra_v810_v830).
#
# Loads the ROM as raw binary at its true VB base, lets Ghidra analyze it, exports
# the functions it found, and diffs against our generated recomp_funcs.h:
#   - functions Ghidra found that we MISSED
#   - functions we emit that Ghidra thinks are DATA / not code
#
# Usage:
#   ./scripts/ghidra_xval.ps1 -Rom "roms\Golf (USA).vb" -OursHeader "corpus\Golf_USA\recomp_funcs.h"
param(
    [Parameter(Mandatory=$true)][string]$Rom,
    [string]$OursHeader,
    [string]$GhidraHome = "C:\tools\ghidra\ghidra_12.0.3_PUBLIC",
    [string]$WorkDir = (Join-Path $PSScriptRoot "..\corpus\_ghidra")
)
$ErrorActionPreference = "Stop"
$Rom = [System.IO.Path]::GetFullPath($Rom)
if (-not (Test-Path $Rom)) { throw "ROM not found: $Rom" }
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

$romSize = (Get-Item $Rom).Length
$base = 0x08000000 - $romSize          # VB maps ROM at the top of the 07 region
$baseHex = "0x{0:X8}" -f $base
$slug = ([System.IO.Path]::GetFileNameWithoutExtension($Rom) -replace '[^A-Za-z0-9]+','_').Trim('_')
$funcsOut = Join-Path $WorkDir "$slug.ghidra.tsv"
$headless = Join-Path $GhidraHome "support\analyzeHeadless.bat"

Write-Host "ROM: $Rom ($romSize bytes), base $baseHex"
Write-Host "Running Ghidra headless analysis (V810)..."

& $headless $WorkDir "xval_$slug" `
    -import $Rom `
    -processor "V810:LE:32:default" `
    -loader BinaryLoader -loader-baseAddr $baseHex `
    -scriptPath (Join-Path $PSScriptRoot "ghidra") `
    -preScript seed_entry.py `
    -postScript export_functions.py $funcsOut `
    -deleteProject 2>&1 | Where-Object { $_ -match "export_functions|ERROR|Exception|functions" } | Select-Object -First 20

if (-not (Test-Path $funcsOut)) { throw "Ghidra produced no function list" }

# Ghidra-discovered function addresses
$ghidra = @{}
foreach ($line in Get-Content $funcsOut) {
    $p = $line -split "`t"
    if ($p.Length -ge 1 -and $p[0] -match '^[0-9A-Fa-f]{8}$') { $ghidra[$p[0].ToUpper()] = $true }
}
Write-Host "Ghidra found $($ghidra.Count) functions."

if ($OursHeader -and (Test-Path $OursHeader)) {
    $ours = @{}
    foreach ($m in (Select-String -Path $OursHeader -Pattern 'vb_func_([0-9A-Fa-f]{8})' -AllMatches).Matches) {
        $ours[$m.Groups[1].Value.ToUpper()] = $true
    }
    Write-Host "We emit $($ours.Count) functions."
    $missed = @($ghidra.Keys | Where-Object { -not $ours.ContainsKey($_) } | Sort-Object)
    $extra  = @($ours.Keys   | Where-Object { -not $ghidra.ContainsKey($_) } | Sort-Object)
    Write-Host ""
    Write-Host "Ghidra-found but WE MISSED: $($missed.Count)"
    $missed | Select-Object -First 15 | ForEach-Object { Write-Host "  $_" }
    Write-Host "WE emit but Ghidra DIDN'T (possible data / over-split): $($extra.Count)"
    $extra | Select-Object -First 15 | ForEach-Object { Write-Host "  $_" }
}
