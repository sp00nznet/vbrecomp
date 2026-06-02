# Generate COMPATIBILITY.md from the sweep's corpus/results.json plus a curated
# runtime-status map (boots/renders/playable notes live here, in version
# control, so the file is deterministic and re-generatable without losing
# hand-tracked state). ASCII-only by design (Windows PowerShell parses .ps1 as
# the system codepage without a BOM, so non-ASCII literals break it).
param(
    [string]$Results = (Join-Path $PSScriptRoot "..\corpus\results.json"),
    [string]$Out     = (Join-Path $PSScriptRoot "..\COMPATIBILITY.md")
)
$ErrorActionPreference = "Stop"

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
        $boots    = if ($c) { $c.boots }    else { "-" }
        $renders  = if ($c) { $c.renders }  else { "-" }
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
