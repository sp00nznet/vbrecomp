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
    "Red Alarm (USA)"                  = @{ boots="yes"; renders="wip"; playable="no";  note="Boots to title; wireframe framebuffer populated, pixel layout WIP; 3D scene does not animate" }
    "Galactic Pinball (Japan, USA) (En)" = @{ boots="yes"; renders="wip"; playable="no"; note="First pixels on screen; boots through 4-state dispatch, settles ~frame 527" }
    "Mario Clash (Japan, USA) (En)"      = @{ boots="yes"; renders="wip"; playable="no"; note="Boots via the generic driver (no hand-written glue); programs VIP + world attributes, advancing frames" }
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
$md += "Living status of every ROM in the library. **Recompiles** = v810recomp emits C; **Compiles** = that C builds clean (MSVC ``/Zs``) - both columns come from ``scripts/sweep.ps1`` (see [STATUS.md](STATUS.md)). **Boots / Renders / Playable** are hand-tracked runtime status."
$md += ""
$md += "Status values: ``yes`` | ``wip`` (partial) | ``no`` | ``-`` (untested)."
$md += ""

$recOk = ($rows | Where-Object { $_.recomp -eq "ok" }).Count
$ccOk  = ($rows | Where-Object { $_.compile -eq "ok" }).Count
$md += "**$($rows.Count) ROMs - $recOk recompile, $ccOk compile clean.**"
$md += ""

foreach ($cat in $order) {
    $catRows = @($rows | Where-Object { (Category $_.name) -eq $cat } | Sort-Object name)
    if ($catRows.Count -eq 0) { continue }
    $md += "## $cat ($($catRows.Count))"
    $md += ""
    $md += "| Game | KB | Funcs | Recompiles | Compiles | Boots | Renders | Playable | Notes |"
    $md += "|------|----|-------|------------|----------|-------|---------|----------|-------|"
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
        $md += "| $($r.name) | $([int]($r.size/1024)) | $($r.funcs) | $rec | $cc | $boots | $renders | $playable | $note |"
    }
    $md += ""
}

($md -join "`n") | Out-File -Encoding utf8 $Out
Write-Host "Wrote $([System.IO.Path]::GetFullPath($Out)) ($($rows.Count) ROMs)"
