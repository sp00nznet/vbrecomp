# SPDX-License-Identifier: MIT
# Quick start (README "Getting Started", route 1). Setup.cmd runs this.
#
# It runs exactly the commands docs/GETTING_STARTED.md documents, steps 1-5, so
# the two routes cannot drift: if you change a command here, change it there.
# Every step is safe to re-run (CMake and the recompile are incremental), so a
# rerun after a failure resumes. Nothing is installed without asking, and the
# ROM is only ever copied from your disk into the gitignored roms/ folder.
#
#   Setup.cmd                       # asks for a ROM
#   Setup.cmd -Rom "D:\Wario Land (Japan, USA).vb"
param([string]$Rom)
$ErrorActionPreference = "Stop"
$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$Log  = Join-Path $Root "setup.log"
Set-Location $Root
"=== setup $(Get-Date -Format s) ===" | Out-File $Log -Encoding utf8

function Fail([string]$msg) {
    Write-Host ""
    Write-Host $msg -ForegroundColor Red
    Write-Host "Details are in $Log"
    exit 1
}

# Native commands go through cmd so their output lands in the log verbatim
# (PowerShell 5.1 wraps native stderr in error records otherwise).
function Run([string]$what, [string]$line, [string]$onFail) {
    Write-Host "-> $what"
    "`n> $line" | Out-File $Log -Append -Encoding utf8
    cmd /c "($line) >> `"$Log`" 2>&1"
    if ($LASTEXITCODE -ne 0) { Fail $onFail }
}

function Ask([string]$q) {
    $a = Read-Host "$q [y/N]"
    return $a -match '^(y|yes)$'
}

function Need-Winget([string]$id, [string]$what, [string]$why, [string]$size, [string]$extra = "") {
    Write-Host ""
    Write-Host "$what is missing. It is needed $why. Download is about $size."
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Fail "Install $what yourself (winget is not available here), then run Setup.cmd again."
    }
    if (-not (Ask "Install $what now with winget?")) { Fail "Install $what, then run Setup.cmd again." }
    Run "installing $what" "winget install --id $id -e --accept-source-agreements --accept-package-agreements $extra" `
        "Installing $what failed. Install it by hand, then run Setup.cmd again."
    Write-Host "$what installed. Close this window and run Setup.cmd again so the new PATH is picked up."
    exit 0
}

Write-Host "vbrecomp quick start"
Write-Host "--------------------"

# --- Prerequisites ----------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = $null
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vs) {
    Need-Winget "Microsoft.VisualStudio.2022.BuildTools" "the Visual Studio 2022 C++ Build Tools" `
        "to compile the recompiler and the recompiled game" "3 GB" `
        '--override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"'
}
Write-Host "ok  MSVC: $vs"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $vsCmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    if (Test-Path (Join-Path $vsCmake "cmake.exe")) { $env:PATH = "$vsCmake;$env:PATH" }
    else { Need-Winget "Kitware.CMake" "CMake" "to configure the build" "40 MB" }
}
Write-Host "ok  CMake"

# `python` is often the Microsoft Store alias, which opens the Store instead of
# running. `py -3` is the real launcher when Python came from python.org.
$py = $null
foreach ($c in @("py -3", "python")) {
    cmd /c "$c --version >nul 2>&1"
    if ($LASTEXITCODE -eq 0) { $py = $c; break }
}
if (-not $py) { Need-Winget "Python.Python.3.12" "Python 3" "to run the instruction conformance check" "30 MB" }
Write-Host "ok  Python ($py)"

$vcpkg = @($env:VCPKG_ROOT, "C:\vcpkg") | Where-Object { $_ -and (Test-Path (Join-Path $_ "vcpkg.exe")) } | Select-Object -First 1
$sdlDir = if ($vcpkg) { Join-Path $vcpkg "installed\x64-windows\share\sdl2" } else { $null }
if (-not ($sdlDir -and (Test-Path $sdlDir))) {
    Write-Host ""
    Write-Host "SDL2 (window, sound, gamepad) is missing. It comes from vcpkg; about 200 MB."
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "Install Git (winget install Git.Git), then run Setup.cmd again." }
    if (-not (Ask "Install vcpkg to C:\vcpkg (if absent) and SDL2 into it?")) { Fail "Install SDL2 with vcpkg (see docs/GETTING_STARTED.md), then run Setup.cmd again." }
    if (-not $vcpkg) {
        $vcpkg = "C:\vcpkg"
        Run "cloning vcpkg" "git clone https://github.com/microsoft/vcpkg C:\vcpkg" "Could not clone vcpkg into C:\vcpkg."
        Run "bootstrapping vcpkg" "C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics" "vcpkg's bootstrap failed."
    }
    Run "installing SDL2" "`"$vcpkg\vcpkg.exe`" install sdl2:x64-windows" "vcpkg could not install SDL2."
    $sdlDir = Join-Path $vcpkg "installed\x64-windows\share\sdl2"
}
Write-Host "ok  SDL2: $sdlDir"

# --- The ROM: the one thing only you have -------------------------------------
$romDir = Join-Path $Root "roms"
New-Item -ItemType Directory -Force -Path $romDir | Out-Null
if (-not $Rom) {
    $have = @(Get-ChildItem $romDir -Filter *.vb -File)
    if ($have.Count -eq 1) { $Rom = $have[0].FullName }
    elseif ($have.Count -gt 1) {
        for ($i = 0; $i -lt $have.Count; $i++) { Write-Host ("  {0,2}. {1}" -f ($i + 1), $have[$i].Name) }
        $n = [int](Read-Host "Which game? (number)")
        if ($n -lt 1 -or $n -gt $have.Count) { Fail "No such number." }
        $Rom = $have[$n - 1].FullName
    } else {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = "Pick your Virtual Boy ROM (.vb)"
        $dlg.Filter = "Virtual Boy ROM (*.vb)|*.vb"
        if ($dlg.ShowDialog() -ne "OK") { Fail "No ROM picked. Run Setup.cmd again, or pass -Rom <path>." }
        $Rom = $dlg.FileName
    }
}
if (-not (Test-Path $Rom)) { Fail "ROM not found: $Rom" }
$romFile = Get-Item $Rom
if ($romFile.DirectoryName -ne $romDir) { Copy-Item $romFile.FullName $romDir -Force }
$romPath = Join-Path $romDir $romFile.Name
# Same slug rule as scripts/sweep.ps1 (Get-Slug), so hints and targets line up.
$slug = (($romFile.BaseName -replace '[^A-Za-z0-9]+', '_')).Trim('_')
Write-Host "ok  ROM: $($romFile.Name)  (slug $slug)"

# --- GETTING_STARTED steps 1-5 ----------------------------------------------
Run "1. building the recompiler" `
    "cmake -S . -B build -T host=x64 -DSDL2_DIR=`"$sdlDir`" && cmake --build build --config Release --target v810recomp" `
    "The recompiler did not build."

Run "2. instruction conformance" "$py tools/conformance.py" `
    "The instruction conformance check failed, so this build would miscompile games. Please report it with the log."

$tool = Join-Path $Root "build\Release\v810recomp.exe"
$out = Join-Path $Root "corpus\$slug"
$hints = Join-Path $Root "games\hints\$slug.hints.txt"
$hintArg = if (Test-Path $hints) { " --hints `"$hints`"" } else { "" }
Run "4. recompiling $($romFile.Name)" "`"$tool`" `"$romPath`" `"$out`"$hintArg" `
    "The recompiler could not translate this ROM. Please report it with the log."

Run "5. building the game" `
    ("cmake -S . -B build_all -T host=x64 -DSDL2_DIR=`"$sdlDir`" -DVBRECOMP_CORPUS_DIR=`"$Root\corpus`" -DVBRECOMP_BUILD_TESTS=OFF" +
     " && cmake --build build_all --config Release --target corpus_$slug") `
    "The recompiled game did not build."

$launcher = Join-Path $Root ("Play " + $romFile.BaseName + ".cmd")
"@`"%~dp0build_all\games\Release\corpus_$slug.exe`" `"%~dp0roms\$($romFile.Name)`"" | Out-File $launcher -Encoding ascii
Write-Host ""
Write-Host "Done. Double-click `"$(Split-Path $launcher -Leaf)`" to play." -ForegroundColor Green
Write-Host "Keys: arrows = left pad, I/J/K/L = right pad, Z = A, X = B, A/S = L/R, Enter = Start, RShift = Select."
