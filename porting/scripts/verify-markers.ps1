<#
.SYNOPSIS
  Checks the port's markers in upstream files against the marker format in
  PORTING_GUIDELINES.md section 2.

.DESCRIPTION
  Scans tracked sources (never porting/patches or porting/notes, which quote markers as text) and
  reports:

  Hard failures - a real defect in the marker bookkeeping:
    * BEGIN/END pairing per file and ID: a stray "END", a mismatched pair, or a "BEGIN" with no
      matching "END". A block opened twice without closing is a failure too.
    * an [ORCAPORT FILE] stamp on a file no manifest row lists in new_files.

  Warnings - hygiene the check cannot judge on its own (use -Strict to fail on them):
    * an [ORCAPORT:<ID>] whose ID matches no manifest.tsv row and no row's markers column.
      The manifest has no rows for the later phases (EF-*, HF-*, SNAP-*, CUT-*, MV-*, SU-8..13),
      so this is the list of features still to be written into the manifest.
    * a "// ORCAPORT:" comment without the bracket form, which no script can find.

  [BUILDFIX:<id>] is reported separately and is never counted as port code; each id must be named
  in porting/BUILD.md.

.EXAMPLE
  powershell -File OrcaSlicer/porting/scripts/verify-markers.ps1
  powershell -File OrcaSlicer/porting/scripts/verify-markers.ps1 -Strict
#>
[CmdletBinding()]
param(
    # Treat warnings (untraceable ids, legacy comment-form markers) as failures.
    [switch]$Strict
)

$ErrorActionPreference = 'Stop'

$root     = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$manifest = Join-Path $root 'porting/manifest.tsv'
$buildDoc = Join-Path $root 'porting/BUILD.md'

if (-not (Test-Path $manifest)) { Write-Error "no manifest at $manifest"; exit 2 }

# --- manifest: ids, markers columns, and new_files as glob patterns -----------------------------
$knownIds     = New-Object System.Collections.Generic.HashSet[string]
$newFileGlobs = New-Object System.Collections.ArrayList

function ConvertTo-GlobRegex([string]$glob) {
    # {a,b} alternation, then escape the rest, then * within a path segment. Returns a list.
    $m = [regex]::Match($glob, '^(.*)\{([^}]*)\}(.*)$')
    if ($m.Success) {
        $out = @()
        foreach ($alt in ($m.Groups[2].Value -split ',')) {
            $out += ConvertTo-GlobRegex ($m.Groups[1].Value + $alt + $m.Groups[3].Value)
        }
        return $out
    }
    $rx = [regex]::Escape($glob) -replace '\\\*', '[^/]*'
    return @('^' + $rx + '$')
}

$header = $null
foreach ($line in Get-Content -LiteralPath $manifest) {
    if ($null -eq $header) { $header = $line; continue }
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $c = $line -split "`t"
    if ($c.Count -lt 13) { continue }
    [void]$knownIds.Add($c[0].Trim())
    foreach ($m in ($c[10] -split '[;,]')) { if ($m.Trim()) { [void]$knownIds.Add($m.Trim()) } }
    $prevEntry = ''
    foreach ($f in ($c[6] -split ';')) {
        # The column is human-written in places: entries may carry a size annotation
        # ("...AlignStack.cpp (53574 B)") or be a bare extension meaning the previous path.
        $e = $f.Trim() -replace '\s*\([^)]*\)\s*$', ''
        if (-not $e) { continue }
        if ($e.StartsWith('.') -and $prevEntry) { $e = [System.IO.Path]::ChangeExtension($prevEntry, $e) }
        if ($e -match '^[,.]*$') { continue }
        $prevEntry = $e
        foreach ($rx in (ConvertTo-GlobRegex $e)) { [void]$newFileGlobs.Add($rx) }
    }
}

function Test-NewFileListed([string]$file) {
    # A manifest row may name a new file with or without its extension (an OrcaMCP entry covers
    # both the .cpp and the .hpp), so try both spellings.
    $candidates = @($file)
    $ext = [System.IO.Path]::GetExtension($file)
    if ($ext) { $candidates += $file.Substring(0, $file.Length - $ext.Length) }
    foreach ($rx in $newFileGlobs) { foreach ($c in $candidates) { if ($c -match $rx) { return $true } } }
    return $false
}

# --- scan --------------------------------------------------------------------------------------
# Native stderr becomes a terminating error under $ErrorActionPreference='Stop'; capture instead.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $grep = @(& git -C $root grep -n -E '\[ORCAPORT|\[BUILDFIX|// *ORCAPORT:' -- src tests resources scripts localization/open)
}
finally { $ErrorActionPreference = $prevEap }
if ($grep.Count -eq 0) { $grep = @() }

$failures = New-Object System.Collections.ArrayList
$warnings = New-Object System.Collections.ArrayList
$buildfix = @{}
$stamps   = @{}
$open     = @{}          # "file|id" -> line of the open BEGIN
$idsSeen  = New-Object System.Collections.Generic.HashSet[string]
$blocks   = 0

foreach ($hit in $grep) {
    if ($hit -notmatch '^(.*?):(\d+):(.*)$') { continue }
    $file = $Matches[1]; $lineno = [int]$Matches[2]; $text = $Matches[3]

    # A comment that meant to be a marker but is not in the bracket form.
    if ($text -match '//\s*(ORCAPORT|BUILDFIX):' -and $text -notmatch '\[(ORCAPORT|BUILDFIX)') {
        [void]$warnings.Add("$file`:$lineno uses '// ORCAPORT:' - no script can find it; use [ORCAPORT:<ID>]")
        continue
    }

    if ($text -match '\[BUILDFIX:([^\]]+)\]') {
        $bid = $Matches[1].Trim()
        if (-not $buildfix.ContainsKey($bid)) { $buildfix[$bid] = $file }
        continue
    }
    if ($text -match '\[ORCAPORT FILE\]') {
        if (-not $stamps.ContainsKey($file)) { $stamps[$file] = $lineno }
        continue
    }
    if ($text -notmatch '\[ORCAPORT:([^\]]+)\]') { continue }
    $id = $Matches[1].Trim()
    if ($id -match '^<.*>$') { continue }            # "<ID>" placeholder in the guidelines
    [void]$idsSeen.Add($id)

    # Marker tokens only: "] BEGIN" and "] END" at the end of the line. Prose such as
    # "Hybrid paths end with" or a closing-brace comment is not a block END.
    $isBegin = $text -match '\]\s*BEGIN\b'
    $isEnd   = $text -match '\]\s*END\s*$'
    $key     = "$file|$id"

    if ($isBegin) {
        if ($open.ContainsKey($key)) {
            [void]$failures.Add("$file`:$lineno opens [ORCAPORT:$id] again while the block from line $($open[$key]) is still open")
        } else {
            $open[$key] = $lineno
            $blocks++
        }
    }
    elseif ($isEnd) {
        if (-not $open.ContainsKey($key)) {
            [void]$failures.Add("$file`:$lineno closes [ORCAPORT:$id] with no matching BEGIN")
        } else {
            $open.Remove($key)
        }
    }
}

foreach ($k in $open.Keys) {
    $parts = $k -split '\|'
    [void]$failures.Add("$($parts[0]):$($open[$k]) opens [ORCAPORT:$($parts[1])] and never closes it")
}

foreach ($id in $idsSeen) {
    if (-not $knownIds.Contains($id)) {
        [void]$warnings.Add("[ORCAPORT:$id] has no manifest.tsv row and no row lists it in its markers column")
    }
}
foreach ($f in $stamps.Keys) {
    if (-not (Test-NewFileListed $f)) {
        [void]$failures.Add("$f carries an [ORCAPORT FILE] stamp but no manifest row lists it in new_files")
    }
}
foreach ($bid in $buildfix.Keys) {
    if (-not (Select-String -LiteralPath $buildDoc -SimpleMatch $bid -Quiet)) {
        [void]$failures.Add("[BUILDFIX:$bid] ($($buildfix[$bid])) is not named in porting/BUILD.md")
    }
}

# --- report ------------------------------------------------------------------------------------
Write-Host ""
Write-Host ("port markers : {0} ids, {1} BEGIN/END block(s), {2} new-file stamp(s)" -f $idsSeen.Count, $blocks, $stamps.Count)
if ($buildfix.Count -gt 0) {
    Write-Host ("BUILDFIX     : {0} id(s), toolchain workarounds, must not be counted as port code: {1}" -f `
        $buildfix.Count, (($buildfix.Keys | Sort-Object) -join ', '))
}
if ($warnings.Count -gt 0) {
    Write-Host ("`n{0} warning(s):" -f $warnings.Count) -ForegroundColor Yellow
    foreach ($w in $warnings) { Write-Host "  $w" -ForegroundColor Yellow }
    Write-Host "  (add the missing manifest rows / fix the comment form; -Strict makes these fail)" -ForegroundColor DarkGray
}
if ($failures.Count -gt 0) {
    Write-Host ("`n{0} failure(s):" -f $failures.Count) -ForegroundColor Red
    foreach ($f in $failures) { Write-Host "  $f" -ForegroundColor Red }
}

if ($failures.Count -gt 0 -or ($Strict -and $warnings.Count -gt 0)) {
    Write-Host "`nFAILED" -ForegroundColor Red
    exit 1
}
Write-Host "`nOK: every marker is paired, and every new-file stamp is listed in the manifest." -ForegroundColor Green
exit 0
