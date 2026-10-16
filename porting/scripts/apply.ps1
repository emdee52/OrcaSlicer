<#
.SYNOPSIS
  Re-applies the port's patch series to a fresh upstream tree - the fallback route in
  PORTING_GUIDELINES.md section 8.

.DESCRIPTION
  Applies OrcaSlicer/porting/patches/NN_<ID>.patch in numeric order with `git apply`, aborting at
  the first patch that does not apply and reporting the patch, the ID, the manifest row for that
  ID, and git's own message (which names the file and hunk).

  This is the fallback for a raw upstream drop; the preferred route is rebasing the port branch.
  Because the patches touch upstream files only, run this against a tree where those files are
  upstream's - on a tree that already carries the port it stops on the first patch, which is the
  intended behaviour, not a bug.

  porting/ must exist in the tree the patches are applied to (it is tracked in this repo, so a
  rebase carries it; for a raw drop, restore it first - see section 8).

.EXAMPLE
  powershell -File OrcaSlicer/porting/scripts/apply.ps1 -DryRun   # report only, change nothing
  powershell -File OrcaSlicer/porting/scripts/apply.ps1
#>
[CmdletBinding()]
param(
    # Check every patch without touching the tree.
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$root    = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$patches = Join-Path $root 'porting/patches'
$manifest = Join-Path $root 'porting/manifest.tsv'

if (-not (Test-Path $patches)) { Write-Error "no patch directory at $patches"; exit 2 }

# PowerShell turns a native command's stderr into a terminating error under
# $ErrorActionPreference='Stop', which would abort before the failure report. Capture instead.
function Invoke-Git([string[]]$gitArgs) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & git -C $root @gitArgs 2>&1
        return [pscustomobject]@{ Code = $LASTEXITCODE; Out = @($out) }
    }
    finally { $ErrorActionPreference = $prev }
}

# --- manifest rows, for the report -------------------------------------------------------------
$rows = @{}
if (Test-Path $manifest) {
    $header = $null
    foreach ($line in Get-Content -LiteralPath $manifest) {
        if ($null -eq $header) { $header = $line; continue }
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $c = $line -split "`t"
        if ($c.Count -lt 3) { continue }
        $rows[$c[0].Trim()] = [pscustomobject]@{ Id = $c[0].Trim(); Title = $c[1]; Status = $c[2] }
    }
}

# A patch file is named NN_<id>.patch; ids in patch names may carry extra suffixes
# (23_PF-10-paint-fix3), so fall back to the longest manifest id that prefixes it.
function Resolve-Row([string]$id) {
    if ($rows.ContainsKey($id)) { return $rows[$id] }
    $candidate = $id
    while ($candidate.Contains('-')) {
        $candidate = $candidate.Substring(0, $candidate.LastIndexOf('-'))
        if ($rows.ContainsKey($candidate)) { return $rows[$candidate] }
    }
    return $null
}

$series = Get-ChildItem -LiteralPath $patches -Filter '*.patch' |
          Where-Object { $_.Name -match '^(\d+)_.+\.patch$' } |
          Sort-Object Name

if ($series.Count -eq 0) { Write-Error "no NN_<ID>.patch files in $patches"; exit 2 }

Write-Host ("applying {0} patch(es) from porting/patches{1}" -f $series.Count, $(if ($DryRun) { ' (dry run)' } else { '' }))

$n = 0
foreach ($p in $series) {
    $null = $p.Name -match '^(\d+)_(.+)\.patch$'
    $id  = $Matches[2]
    $row = Resolve-Row $id
    $rowText = if ($null -ne $row) { "{0} [{1}] {2}" -f $row.Id, $row.Status, $row.Title } else { "no manifest row for '$id'" }

    $n++
    $check = Invoke-Git @('apply', '--check', $p.FullName)
    if ($check.Code -ne 0) {
        Write-Host ""
        Write-Host ("FAILED at patch {0}/{1}: {2}" -f $n, $series.Count, $p.Name) -ForegroundColor Red
        Write-Host ("  id            : {0}" -f $id)
        Write-Host ("  manifest row  : {0}" -f $rowText)
        Write-Host ("  git apply says:") -ForegroundColor Red
        foreach ($line in $check.Out) { Write-Host ("    {0}" -f $line) }
        Write-Host ""
        Write-Host "Resolve the conflict in that file against porting/notes/<ID>.md (which records the" -ForegroundColor Yellow
        Write-Host "original intent), then re-run this script from the first patch that did not apply." -ForegroundColor Yellow
        exit 1
    }

    if (-not $DryRun) {
        $applied = Invoke-Git @('apply', $p.FullName)
        if ($applied.Code -ne 0) {
            Write-Host ("FAILED applying {0}: {1}" -f $p.Name, ($applied.Out -join ' ')) -ForegroundColor Red
            exit 1
        }
    }
    Write-Host ("  {0,-32} ok   {1}" -f $p.Name, $rowText)
}

Write-Host ""
if ($DryRun) { Write-Host "dry run: every patch applies cleanly." -ForegroundColor Green }
else         { Write-Host "all patches applied." -ForegroundColor Green }
exit 0
