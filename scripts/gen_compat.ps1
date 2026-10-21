# SPDX-License-Identifier: MIT
# Generate COMPATIBILITY.md from the sweep's corpus/results.json plus a curated
# runtime-status map (boots/renders/playable notes live here, in version
# control, so the file is deterministic and re-generatable without losing
# hand-tracked state). ASCII-only by design (Windows PowerShell parses .ps1 as
# the system codepage without a BOM, so non-ASCII literals break it).
param(
    [string]$Results = (Join-Path $PSScriptRoot "..\corpus\results.json"),
    [string]$Survey  = (Join-Path $PSScriptRoot "..\corpus\render_survey.json"),
    [string]$Out     = (Join-Path $PSScriptRoot "..\COMPATIBILITY.md")
)
$ErrorActionPreference = "Stop"

# Render-survey results (scripts/render_survey.ps1), keyed by ROM name. Lets the
# Boots/Renders columns auto-fill from the headless boot probe so the table
# tracks reality across all ROMs without hand-editing each one. UTF-8 may carry
# a BOM from PS5.1 Out-File, so strip it before parsing.
# NB: PowerShell variable names are case-insensitive, so this map must NOT be
# named $survey -- that would alias (and clobber) the $Survey path parameter.
$surveyMap = @{}
if (Test-Path $Survey) {
    $raw = [IO.File]::ReadAllText($Survey)
    $i = $raw.IndexOfAny([char[]]@('[','{'))   # skip any BOM/whitespace prefix
    if ($i -ge 0) {
        # Assign before foreach: PS5.1 ConvertFrom-Json emits a JSON array as a
        # single pipeline object, so iterating the pipeline inline runs once with
        # the whole array. Binding to a variable unrolls it into elements.
        $parsed = $raw.Substring($i) | ConvertFrom-Json
        foreach ($s in $parsed) { $surveyMap[$s.name] = $s }
    }
}

# Curated runtime status, keyed by ROM base name. Update as games progress.
# boots / renders / playable in {yes, wip, no, "-"(untested)}.
$curated = @{
    "Mario's Tennis (Japan, USA) (En)" = @{ boots="yes"; renders="wip"; playable="wip"; note="Demo mode runs; title/court/sprites render; gameplay upper-BG corruption (block rendering)" }
    "Space Squash (Japan)"             = @{ boots="yes"; renders="yes"; playable="wip"; note="In game (2026-10-21): auto-pause prompt, GET READY, a scored rally, the continue screen, with Start/A input only" }
    "Golf (USA)"                       = @{ boots="yes"; renders="wip"; playable="no";  note="Past the warning and auto-pause screens to the Nintendo and T&E SOFT logos; later screens are striped garbage" }
    "Red Alarm (USA)"                  = @{ boots="yes"; renders="wip"; playable="no";  note="Boots to title; wireframe framebuffer populated, pixel layout WIP; 3D scene does not animate" }
    "Galactic Pinball (Japan, USA) (En)" = @{ boots="yes"; renders="wip"; playable="no"; note="Renders since the inline-call-argument fix; world attributes now written, image still garbled" }
    "Mario Clash (Japan, USA) (En)"      = @{ boots="yes"; renders="wip"; playable="no"; note="Boots via the generic driver (no hand-written glue); programs VIP + world attributes, advancing frames" }
}

# These three draw nothing on real hardware either, re-checked on BizHawk's
# Mednafen VB core at 1800 frames with the same buttons held. They are not
# failures of this project and are excluded from the achievable denominator.
$hwBlank = @(
    'Snowball Wars (World) (Demo 1) (Aftermarket) (Homebrew)',
    'Snowball Wars (World) (Demo 2) (Aftermarket) (Homebrew)',
    'Snowball Wars (World) (Demo 3) (Aftermarket) (Homebrew)'
)

# Motion comes from the screenshots render_survey.ps1 leaves in
# corpus/_timeline/<slug>/: identical samples mean the game drew once and
# stopped, differing samples mean it is still reaching new state.
$timeline = Join-Path (Split-Path $Results) '_timeline'
function Motion($slug, $cat) {
    if ($cat -ne 'RENDERS') { return 'blank' }
    $dir = Join-Path $timeline $slug
    if (-not (Test-Path $dir)) { return '-' }
    $h = @{}
    foreach ($n in 300,600,900) {
        $f = Join-Path $dir ('f.png.{0:d4}.png' -f $n)
        if (Test-Path $f) { $h[(Get-FileHash $f -Algorithm MD5).Hash] = $true }
    }
    if ($h.Count -eq 0) { return '-' }
    if ($h.Count -eq 1) { return 'frozen' }
    return 'advancing'
}

$rows = Get-Content $Results -Raw | ConvertFrom-Json

function Category($name) {
    if ($name -match "Aftermarket|Homebrew") { return "Homebrew" }
    if ($name -match "Proto|Beta|Demo")      { return "Proto / Demo" }
    return "Retail"
}

$order = @("Retail", "Proto / Demo", "Homebrew")
$md = @()
$md += "# Compatibility"
$md += ""
$md += "Living status of every ROM in the library. **Recompiles** = v810recomp emits C; **Compiles** = that C builds clean (MSVC ``/Zs``) - both columns come from ``scripts/sweep.ps1`` (see [STATUS.md](STATUS.md)). **Boots / Renders / Motion** come from the headless survey (``scripts/render_survey.ps1``); **Playable** and the notes are hand-tracked. **Motion** is ``advancing`` when the frame still changes between samples at 300/600/900, ``frozen`` when the game drew once and stopped."
$md += ""
$md += "Status values: ``yes`` | ``wip`` (partial) | ``no`` | ``-`` (untested)."
$md += ""

$recOk = ($rows | Where-Object { $_.recomp -eq "ok" }).Count
$ccOk  = ($rows | Where-Object { $_.compile -eq "ok" }).Count
$renderCount = 0; $advCount = 0; $frozenCount = 0; $blankCount = 0
foreach ($r in $rows) {
    $sv = $surveyMap[$r.name]
    $cat = "-"; if ($sv) { $cat = $sv.cat }
    $slug = $r.slug
    $m = Motion $slug $cat
    switch ($m) {
        "advancing" { $advCount++;    $renderCount++ }
        "frozen"    { $frozenCount++; $renderCount++ }
        "blank"     { $blankCount++ }
    }
}
$ourBlank = $blankCount - $hwBlank.Count
$ceiling  = $rows.Count - $hwBlank.Count

$md += "## Where this has got to"
$md += ""
$md += "| | |"
$md += "|---|---|"
$md += "| ROMs in the library | **$($rows.Count)** |"
$md += "| Recompile to C | **$recOk / $($rows.Count)** |"
$md += "| That C compiles clean | **$ccOk / $($rows.Count)** |"
$md += "| Put something on screen | **$renderCount / $($rows.Count)** |"
$md += "| ... still reaching new state at frame 900 | **$advCount** |"
$md += "| ... drew once and froze | **$frozenCount** |"
$md += "| Blank | **$blankCount** ($($hwBlank.Count) blank on real hardware too, so **$ourBlank** are ours) |"
$md += ""
$md += "Three ROMs draw nothing on the reference emulator either -- verified on"
$md += "BizHawk's Mednafen VB core at 1800 frames with the same buttons held, where"
$md += "they never load a single character byte, let alone draw one. So the"
$md += "achievable denominator is **$ceiling**, not $($rows.Count)."
$md += ""
$md += "Every one of these numbers is regenerated from a run, not hand-maintained:"
$md += "``scripts/sweep.ps1`` -> ``scripts/build_all.ps1`` -> ``scripts/render_survey.ps1`` -> this file."
$md += "See [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) to reproduce it."
$md += ""

foreach ($cat in $order) {
    $catRows = @($rows | Where-Object { (Category $_.name) -eq $cat } | Sort-Object name)
    if ($catRows.Count -eq 0) { continue }
    $md += "## $cat ($($catRows.Count))"
    $md += ""
    $md += "| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Motion | Playable | Notes |"
    $md += "|------|----|-------|------------|----------|-------|---------|--------|----------|-------|"
    foreach ($r in $catRows) {
        $c = $curated[$r.name]
        # Auto-derive Boots/Renders from the headless render survey; curated
        # entries override (they carry hand-verified nuance + notes).
        $sv = $surveyMap[$r.name]
        $autoBoots = "-"; $autoRenders = "-"
        if ($sv) {
            switch ($sv.cat) {
                "RENDERS"            { $autoBoots = "yes"; $autoRenders = "yes" }
                "LOADED-NOT-DRAWING" { $autoBoots = "yes"; $autoRenders = "wip" }
                "CHR-NO-WORLD"       { $autoBoots = "yes"; $autoRenders = "wip" }
                default              { $autoBoots = "-";   $autoRenders = "no"  }
            }
        }
        $boots    = if ($c) { $c.boots }    else { $autoBoots }
        $renders  = if ($c) { $c.renders }  else { $autoRenders }
        $playable = if ($c) { $c.playable } else { "-" }
        $note     = if ($c) { $c.note }     else { "" }
        $rec = if ($r.recomp -eq "ok")  { "yes" } else { "no" }
        $cc  = if ($r.compile -eq "ok") { "yes" } else { "no" }
        $motion = Motion $r.slug $(if ($sv) { $sv.cat } else { "-" })
        if ($hwBlank -contains $r.name) {
            $motion = "blank (hw)"
            if (-not $note) { $note = "Blank on real hardware too -- not ours" }
        }
        $md += "| $($r.name) | $([int]($r.size/1024)) | $($r.funcs) | $rec | $cc | $boots | $renders | $motion | $playable | $note |"
    }
    $md += ""
}

($md -join "`n") | Out-File -Encoding utf8 $Out
Write-Host "Wrote $([System.IO.Path]::GetFullPath($Out)) ($($rows.Count) ROMs)"
