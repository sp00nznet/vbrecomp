# Broad corpus sweep: for every ROM in roms/, run v810recomp to generate C,
# then syntax-check the generated C with MSVC (/Zs, no link). Produces a
# STATUS.md matrix and corpus_results.json.
#
# Sequential by design (resource safety: never run builds in parallel).
# Each tool / compiler invocation has a timeout so a pathological ROM can't
# stall the whole sweep.
param(
    [string]$RomDir     = (Join-Path $PSScriptRoot "..\roms"),
    [string]$CorpusDir  = (Join-Path $PSScriptRoot "..\corpus"),
    [string]$Tool       = (Join-Path $PSScriptRoot "..\build\Debug\v810recomp.exe"),
    [string]$IncludeDir = (Join-Path $PSScriptRoot "..\include"),
    [int]$RecompTimeout = 60,
    [int]$CompileTimeout = 180,
    [switch]$NoCompile
)
$ErrorActionPreference = "Stop"
$RomDir     = [System.IO.Path]::GetFullPath($RomDir)
$CorpusDir  = [System.IO.Path]::GetFullPath($CorpusDir)
$Tool       = [System.IO.Path]::GetFullPath($Tool)
$IncludeDir = [System.IO.Path]::GetFullPath($IncludeDir)

if (-not (Test-Path $Tool)) { throw "v810recomp not built: $Tool" }
New-Item -ItemType Directory -Force -Path $CorpusDir | Out-Null

# --- Import MSVC environment once (needed for cl /Zs system includes) ---
$cl = $null
if (-not $NoCompile) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath  = & $vswhere -latest -property installationPath
    $vcvars  = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    # Run vcvars with its (noisy, sometimes stderr-writing) output swallowed
    # inside cmd; only `set`'s stdout reaches PowerShell so Stop-pref is safe.
    $envDump = cmd /c "`"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $envDump) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
    }
    $cl = (Get-Command cl.exe -ErrorAction SilentlyContinue).Source
    if (-not $cl) { throw "cl.exe not on PATH after vcvars import" }
    Write-Host "Using cl: $cl"
}

# Run a process with a timeout. Returns @{ Code; TimedOut; StdOut; StdErr }.
function Invoke-WithTimeout {
    param([string]$Exe, [string[]]$ArgList, [int]$TimeoutSec, [string]$WorkDir = $PWD)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    # Windows PowerShell 5.1 (.NET Framework) has no ArgumentList; build a
    # quoted argument string so paths with spaces survive.
    $psi.Arguments = ($ArgList | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join ' '
    $psi.WorkingDirectory = $WorkDir
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $p = [System.Diagnostics.Process]::Start($psi)
    # Read async to avoid pipe-buffer deadlock
    $outTask = $p.StandardOutput.ReadToEndAsync()
    $errTask = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        try { $p.Kill($true) } catch {}
        return @{ Code = -999; TimedOut = $true; StdOut = $outTask.Result; StdErr = $errTask.Result }
    }
    return @{ Code = $p.ExitCode; TimedOut = $false; StdOut = $outTask.Result; StdErr = $errTask.Result }
}

function Get-Slug([string]$name) {
    $s = [System.IO.Path]::GetFileNameWithoutExtension($name)
    $s = $s -replace '[^A-Za-z0-9]+', '_'
    return $s.Trim('_')
}

$roms = Get-ChildItem -Path $RomDir -Filter *.vb -File | Sort-Object Name
Write-Host "Sweeping $($roms.Count) ROMs..."
$results = @()
$i = 0
foreach ($rom in $roms) {
    $i++
    $slug = Get-Slug $rom.Name
    $outDir = Join-Path $CorpusDir $slug
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    $rec = [ordered]@{
        name = $rom.BaseName; slug = $slug; size = $rom.Length
        recomp = "fail"; funcs = 0; recompMsg = ""
        compile = "skip"; errors = 0; warnings = 0; firstError = ""
    }

    $r = Invoke-WithTimeout -Exe $Tool -ArgList @($rom.FullName, $outDir) -TimeoutSec $RecompTimeout
    if ($r.TimedOut) {
        $rec.recomp = "timeout"
    } elseif ($r.Code -ne 0) {
        $rec.recomp = "fail"
        $rec.recompMsg = ($r.StdErr -split "`n" | Select-Object -Last 1).Trim()
    } else {
        $genC = Join-Path $outDir "recomp_funcs.c"
        $genH = Join-Path $outDir "recomp_funcs.h"
        if (Test-Path $genC) {
            $rec.recomp = "ok"
            if (Test-Path $genH) {
                $rec.funcs = (Select-String -Path $genH -Pattern '^void vb_func_' -AllMatches).Count
            }
        }
    }

    if (-not $NoCompile -and $rec.recomp -eq "ok") {
        $genC = Join-Path $outDir "recomp_funcs.c"
        $c = Invoke-WithTimeout -Exe $cl `
            -ArgList @("/nologo", "/c", "/Zs", "/I", $IncludeDir, "/W1", $genC) `
            -TimeoutSec $CompileTimeout -WorkDir $outDir
        $allOut = ($c.StdOut + "`n" + $c.StdErr)
        $errLines  = @($allOut -split "`n" | Where-Object { $_ -match ': error ' })
        $warnLines = @($allOut -split "`n" | Where-Object { $_ -match ': warning ' })
        $rec.errors = $errLines.Count
        $rec.warnings = $warnLines.Count
        if ($c.TimedOut) {
            $rec.compile = "timeout"
        } elseif ($c.Code -eq 0 -and $errLines.Count -eq 0) {
            $rec.compile = "ok"
        } else {
            $rec.compile = "fail"
            if ($errLines.Count -gt 0) { $rec.firstError = ($errLines[0]).Trim() }
        }
    }

    $results += [pscustomobject]$rec
    $tag = "{0,-3} {1,-45} rec:{2,-7} fn:{3,-5} cc:{4,-7} err:{5}" -f `
        $i, ($rom.BaseName.Substring(0, [Math]::Min(45, $rom.BaseName.Length))), `
        $rec.recomp, $rec.funcs, $rec.compile, $rec.errors
    Write-Host $tag
}

# --- Emit results.json + STATUS.md ---
$jsonPath = Join-Path $CorpusDir "results.json"
$results | ConvertTo-Json -Depth 4 | Out-File -Encoding utf8 $jsonPath

$recOk = ($results | Where-Object { $_.recomp -eq "ok" }).Count
$ccOk  = ($results | Where-Object { $_.compile -eq "ok" }).Count
$total = $results.Count

$md = @()
$md += "# vbrecomp corpus status"
$md += ""
$md += "Generated by ``scripts/sweep.ps1``. One row per ROM in ``roms/``."
$md += ""
$md += "- Total ROMs: **$total**"
$md += "- Recompiled (C generated): **$recOk / $total**"
$md += "- Compiles clean (cl /Zs, 0 errors): **$ccOk / $total**"
$md += ""
$md += "| # | ROM | KB | Recomp | Funcs | Compile | Errors | First error |"
$md += "|---|-----|----|--------|-------|---------|--------|-------------|"
$row = 0
foreach ($r in ($results | Sort-Object @{e={$_.compile -ne 'ok'}}, @{e={$_.errors}; Descending=$true}, name)) {
    $row++
    $fe = $r.firstError -replace '\|', '\|'
    if ($fe.Length -gt 80) { $fe = $fe.Substring(0,80) }
    $md += "| $row | $($r.name) | $([int]($r.size/1024)) | $($r.recomp) | $($r.funcs) | $($r.compile) | $($r.errors) | $fe |"
}
$mdPath = Join-Path $PSScriptRoot "..\STATUS.md"
($md -join "`n") | Out-File -Encoding utf8 $mdPath
Write-Host ""
Write-Host "Recomp ok: $recOk/$total   Compile ok: $ccOk/$total"
Write-Host "Wrote $([System.IO.Path]::GetFullPath($mdPath)) and $jsonPath"
