# SPDX-License-Identifier: MIT
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
    # -T host=x64 is REQUIRED here, not a tuning knob. The Visual Studio
    # generator defaults to the 32-bit hosted compiler, and the largest
    # generated files (Mario VB, Null Pointer Mockup, BLOX: hundreds of
    # thousands of lines in one translation unit) exhaust its 2 GB address
    # space with "C1060: compiler is out of heap space". The 64-bit host
    # compiles the same files without trouble.
    #
    # Each -D must be QUOTED. PowerShell parses a bare argument that starts
    # with a dash in parameter mode and does not expand $variables inside it,
    # so the unquoted form hands cmake the literal string "$Corpus" -- which
    # configures cleanly and silently generates no corpus targets at all.
    cmake -S $Root -B $BuildDir -T host=x64 "-DSDL2_DIR=$SDL2Dir" "-DVBRECOMP_CORPUS_DIR=$Corpus" "-DVBRECOMP_BUILD_TESTS=OFF" *>&1 |
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
        # Kill() takes no argument on .NET Framework (Windows PowerShell 5.1):
        # the Kill($true) tree-kill overload is .NET Core only, so it threw, the
        # catch swallowed it, and the timed-out process survived -- holding its
        # .exe open and burning CPU for the rest of the run, which is enough to
        # time out the games that come after it. Recompiled games spawn nothing,
        # so a plain Kill() is all that was ever needed.
        try { $p.Kill() } catch {}
        return @{ Code = $null; TimedOut = $true; Out = $so.Result; Err = $se.Result }
    }
    return @{ Code = $p.ExitCode; TimedOut = $false; Out = $so.Result; Err = $se.Result }
}

$results = @()
$i = 0
foreach ($slug in $slugs) {
    $i++
    $rec = [ordered]@{ slug=$slug; name=($meta[$slug]); build="fail"; ran="no"; shot="no"; shotBytes=0; rendered="no"; note="" }

    # --- build (one target at a time, single MSBuild process) ---
    $exe = Join-Path $BuildDir "games\Release\corpus_$slug.exe"
    # A previous run's game may still be alive and holding this .exe open, in
    # which case Remove-Item throws and (under ErrorActionPreference=Stop) takes
    # the whole sweep down partway through. Games are headless and disposable,
    # so stop any instance of this one first.
    Get-Process -Name "corpus_$slug" -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
    if (Test-Path $exe) { Remove-Item $exe -Force -EA SilentlyContinue }
    # Plain single-target build (inherently sequential: one project, cl one
    # file at a time). No parallel flag — passing one mangles cmake's msbuild
    # project resolution under PowerShell.
    #
    # /nodeReuse:false matters over 76 invocations: MSBuild leaves its worker
    # nodes alive for reuse by default, and they accumulate across the run
    # until the machine is out of memory and cl cannot even load c2.dll.
    $buildOut = (& cmake --build $BuildDir --config Release --target "corpus_$slug" -- /nodeReuse:false 2>&1 | Out-String)
    if (Test-Path $exe) { $rec.build = "ok" }
    else { $rec.build = "fail"; $rec.note = (($buildOut -split "`n" | Where-Object { $_ -match ': error' } | Select-Object -First 1)) }

    # --- run headless + screenshot ---
    if ($rec.build -eq "ok") {
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
                $rec.shot = "yes"
                $rec.shotBytes = (Get-Item $shot).Length
                # A blank 384x224 frame (all black) is exactly 3619 bytes; anything
                # meaningfully larger means the game drew something.
                if ($rec.shotBytes -gt 3800) { $rec.rendered = "yes" }
            }
        } else { $rec.note = "ROM not found" }
    }

    $results += [pscustomobject]$rec
    Write-Host ("{0,-3} {1,-44} build:{2,-8} ran:{3,-6} rendered:{4,-4} {5}b {6}" -f `
        $i, $slug.Substring(0,[Math]::Min(44,$slug.Length)), $rec.build, $rec.ran, $rec.rendered, $rec.shotBytes, $rec.note)
}

# --- report ---
$results | ConvertTo-Json -Depth 3 | Out-File -Encoding utf8 (Join-Path $Corpus "bootstatus.json")
$built = @($results | Where-Object { $_.build -eq "ok" }).Count
$ran   = @($results | Where-Object { $_.ran -eq "yes" }).Count
$drew  = @($results | Where-Object { $_.rendered -eq "yes" }).Count
$md = @("# Boot status (generic driver)", "",
    "Every recompiled ROM built with the shared generic driver (no game-specific code) and booted headless for $Frames frames. Screenshots in ``corpus/_shots/``.", "",
    "- Builds: **$built / $($results.Count)**",
    "- Runs without crashing: **$ran / $($results.Count)**",
    "- Drew visible output: **$drew / $($results.Count)** (non-blank screenshot)",
    "",
    "Most games run but don't render under the naive driver yet -- they need per-game boot/timing tuning (as the lead games got). 'Drew visible output' marks the ones that already show something.",
    "",
    "| # | Game | Builds | Runs | Drew | Notes |",
    "|---|------|--------|------|------|-------|")
$n = 0
foreach ($r in ($results | Sort-Object @{e={$_.rendered -ne 'yes'}}, @{e={$_.build -eq 'ok'}}, name)) {
    $n++
    $md += "| $n | $($r.name) | $($r.build) | $($r.ran) | $($r.rendered) | $($r.note) |"
}
($md -join "`n") | Out-File -Encoding utf8 (Join-Path $Root "BOOTSTATUS.md")
Write-Host ""
Write-Host "Built $built/$($results.Count), ran $ran, ~$drew rendered. Report: BOOTSTATUS.md, shots: $ShotDir"
