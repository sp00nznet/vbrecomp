# Build EVERY recompiled ROM via the generic driver, boot each headless, and
# capture where it gets to (+ a screenshot). Writes corpus/_shots/ and a report.
#
# Sequential by design (resource safety: never build in parallel).
param(
    [string]$Root         = (Join-Path $PSScriptRoot ".."),
    [string]$Corpus       = (Join-Path $PSScriptRoot "..\corpus"),
    [string]$BuildDir     = (Join-Path $PSScriptRoot "..\build_all"),
    [string]$ShotDir      = (Join-Path $PSScriptRoot "..\corpus\_shots"),
    [string]$RomDir       = (Join-Path $PSScriptRoot "..\roms"),
    [int]$Frames          = 600,
    [int]$BuildTimeoutSec = 240,
    [int]$RunTimeoutSec   = 40,
    [string]$SDL2Dir      = "C:/vcpkg/installed/x64-windows/share/sdl2",
    [string]$SDL2Dll      = "C:/vcpkg/installed/x64-windows/bin/SDL2.dll"
)
$ErrorActionPreference = "Stop"
$Root = [System.IO.Path]::GetFullPath($Root)
$Corpus = [System.IO.Path]::GetFullPath($Corpus)
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$ShotDir = [System.IO.Path]::GetFullPath($ShotDir)
$RomDir = [System.IO.Path]::GetFullPath($RomDir)
New-Item -ItemType Directory -Force -Path $ShotDir | Out-Null

# slug -> original ROM name, from the sweep's results.json
$meta = @{}
$resJson = Join-Path $Corpus "results.json"
if (Test-Path $resJson) {
    foreach ($r in (Get-Content $resJson -Raw | ConvertFrom-Json)) { $meta[$r.slug] = $r.name }
}

if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Host "Configuring corpus build..."
    cmake -S $Root -B $BuildDir -DSDL2_DIR=$SDL2Dir -DVBRECOMP_CORPUS_DIR=$Corpus -DVBRECOMP_BUILD_TESTS=OFF *>&1 |
        Select-Object -Last 2
} else {
    Write-Host "Reusing existing $BuildDir configuration."
}

# All corpus targets (subdirs with generated code)
$slugs = Get-ChildItem -Path $Corpus -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName "recomp_funcs.c") } |
    Select-Object -ExpandProperty Name | Sort-Object
Write-Host "$($slugs.Count) games to build."

function Invoke-Timed([string]$Exe, [string[]]$ArgList, [int]$Timeout, [hashtable]$EnvVars) {
    foreach ($k in $EnvVars.Keys) { Set-Item "env:$k" $EnvVars[$k] }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $psi.Arguments = ($ArgList | ForEach-Object { '"' + ($_ -replace '"','\"') + '"' }) -join ' '
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $so = $p.StandardOutput.ReadToEndAsync(); $se = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($Timeout * 1000)) {
        try { $p.Kill($true) } catch {}
        return @{ Code = $null; TimedOut = $true; Out = $so.Result; Err = $se.Result }
    }
    return @{ Code = $p.ExitCode; TimedOut = $false; Out = $so.Result; Err = $se.Result }
}

$results = @()
$dllCopied = $false
$i = 0
foreach ($slug in $slugs) {
    $i++
    $rec = [ordered]@{ slug=$slug; name=($meta[$slug]); build="fail"; ran="no"; shot="no"; shotKB=0; note="" }

    # --- build (one target at a time, single MSBuild process) ---
    $exe = Join-Path $BuildDir "games\Release\corpus_$slug.exe"
    if (Test-Path $exe) { Remove-Item $exe -Force }
    # Plain single-target build (inherently sequential: one project, cl one
    # file at a time). No parallel flag — passing one mangles cmake's msbuild
    # project resolution under PowerShell.
    $buildOut = (& cmake --build $BuildDir --config Release --target "corpus_$slug" 2>&1 | Out-String)
    if (Test-Path $exe) { $rec.build = "ok" }
    else { $rec.build = "fail"; $rec.note = (($buildOut -split "`n" | Where-Object { $_ -match ': error' } | Select-Object -First 1)) }

    # --- run headless + screenshot ---
    if ($rec.build -eq "ok") {
        if (-not $dllCopied) { Copy-Item $SDL2Dll (Split-Path $exe) -Force; $dllCopied = $true }
        $romName = $meta[$slug]
        $rom = if ($romName) { Join-Path $RomDir "$romName.vb" } else { $null }
        if ($rom -and (Test-Path $rom)) {
            $shot = Join-Path $ShotDir "$slug.png"
            if (Test-Path $shot) { Remove-Item $shot -Force }
            $env:PATH = (Split-Path $SDL2Dll) + ";" + $env:PATH
            $r = Invoke-Timed $exe @($rom) $RunTimeoutSec @{
                VBRECOMP_HEADLESS="1"; VBRECOMP_HEADLESS_FRAMES="$Frames"; VBRECOMP_SHOT_PATH=$shot }
            if ($r.TimedOut -or $r.Code -eq 0) { $rec.ran = "yes" }
            else { $rec.ran = "crash"; $rec.note = "exit 0x{0:X}" -f $r.Code }
            if (Test-Path $shot) {
                $rec.shot = "yes"; $rec.shotKB = [int]((Get-Item $shot).Length / 1024)
            }
        } else { $rec.note = "ROM not found" }
    }

    $results += [pscustomobject]$rec
    Write-Host ("{0,-3} {1,-44} build:{2,-8} ran:{3,-6} shot:{4,-4} {5}KB {6}" -f `
        $i, $slug.Substring(0,[Math]::Min(44,$slug.Length)), $rec.build, $rec.ran, $rec.shot, $rec.shotKB, $rec.note)
}

# --- report ---
$results | ConvertTo-Json -Depth 3 | Out-File -Encoding utf8 (Join-Path $Corpus "bootstatus.json")
$built = ($results | Where-Object { $_.build -eq "ok" }).Count
$ran   = ($results | Where-Object { $_.ran -eq "yes" }).Count
$drew  = ($results | Where-Object { [int]$_.shotKB -gt 3 }).Count   # >3KB png ~ non-blank
$md = @("# Boot status (generic driver)", "",
    "Built and booted headless via the generic driver, $Frames frames each.", "",
    "- Builds: **$built / $($results.Count)**",
    "- Run without crashing: **$ran / $($results.Count)**",
    "- Rendered non-blank output: **~$drew / $($results.Count)** (screenshot >3KB)", "",
    "| # | Game | Builds | Runs | Screenshot KB | Notes |",
    "|---|------|--------|------|---------------|-------|")
$n = 0
foreach ($r in $results) {
    $n++
    $md += "| $n | $($r.name) | $($r.build) | $($r.ran) | $($r.shotKB) | $($r.note) |"
}
($md -join "`n") | Out-File -Encoding utf8 (Join-Path $Root "BOOTSTATUS.md")
Write-Host ""
Write-Host "Built $built/$($results.Count), ran $ran, ~$drew rendered. Report: BOOTSTATUS.md, shots: $ShotDir"
