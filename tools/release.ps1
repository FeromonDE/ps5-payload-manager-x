<#
.SYNOPSIS
  Cut a PS5 Payload Manager X release by creating and pushing an x-v* tag.

.DESCRIPTION
  Computes the next fork version from the existing x-v* tags, verifies the tree
  is clean and on main and the tag is free, then creates an annotated tag and
  pushes ONLY that tag (release.yml does the rest). The fork version is
  independent of upstream's MENU_VERSION.

.EXAMPLE
  ./tools/release.ps1 -Bump patch              # 1.2.3 -> 1.2.4 (stable)
  ./tools/release.ps1 -Bump minor -Pre beta    # -> 1.3.0-beta.1
  ./tools/release.ps1 -Pre beta                # continue the current pre-release line
  ./tools/release.ps1 -Promote                 # current pre-release -> stable
  ./tools/release.ps1 -Version 1.2.3-rc.1      # that exact version
  ./tools/release.ps1 -Bump patch -DryRun      # show what would happen
#>
[CmdletBinding(DefaultParameterSetName = 'bump')]
param(
  [Parameter(ParameterSetName = 'bump')][ValidateSet('patch', 'minor', 'major')][string]$Bump,
  [Parameter(ParameterSetName = 'bump')][Parameter(ParameterSetName = 'pre')][ValidateSet('alpha', 'beta', 'rc')][string]$Pre,
  [Parameter(ParameterSetName = 'promote')][switch]$Promote,
  [Parameter(ParameterSetName = 'explicit')][string]$Version,
  [switch]$DryRun,
  [switch]$Yes
)
$ErrorActionPreference = 'Stop'
$PREFIX = 'x-v'
Set-Location (Join-Path $PSScriptRoot '..')

function Fail($m) { Write-Error $m; exit 1 }

# Latest x-v tag by semver (suffix '-' so beta.10 > beta.9).
$tags = git -c versionsort.suffix=- tag -l "$PREFIX*" --sort=-v:refname
$latest = if ($tags) { ($tags -split "`n")[0].Trim() } else { '' }
$cur = if ($latest) { $latest.Substring($PREFIX.Length) } else { '0.0.0' }

function Parse($v) {
  if ($v -notmatch '^(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta|rc)\.(\d+))?$') { Fail "Cannot parse version '$v'" }
  [pscustomobject]@{ Major = [int]$Matches[1]; Minor = [int]$Matches[2]; Patch = [int]$Matches[3]; Pre = $Matches[4]; PreN = if ($Matches[5]) { [int]$Matches[5] } else { 0 } }
}
$c = Parse $cur

switch ($PSCmdlet.ParameterSetName) {
  'explicit' { $next = $Version }
  'promote' {
    if (-not $c.Pre) { Fail "Current version $cur is already stable; nothing to promote." }
    $next = "$($c.Major).$($c.Minor).$($c.Patch)"
  }
  'pre' {
    # Continue a pre-release line on the current (unreleased) target.
    if ($c.Pre -eq $Pre) { $next = "$($c.Major).$($c.Minor).$($c.Patch)-$Pre.$($c.PreN + 1)" }
    else { $next = "$($c.Major).$($c.Minor).$($c.Patch)-$Pre.1" }
  }
  'bump' {
    $M = $c.Major; $m = $c.Minor; $p = $c.Patch
    switch ($Bump) {
      'major' { $M++; $m = 0; $p = 0 }
      'minor' { $m++; $p = 0 }
      'patch' { $p++ }
      default { if (-not $Pre) { Fail "Specify -Bump patch|minor|major (and optionally -Pre)." } }
    }
    $next = "$M.$m.$p"
    if ($Pre) { $next = "$next-$Pre.1" }
  }
}

$tag = "$PREFIX$next"
Parse $next | Out-Null   # validate shape

Write-Host "Current: $(if ($latest) { $latest } else { '(none)' })"
Write-Host "Next:    $tag"

# Preconditions (skipped on dry run so you can preview from anywhere).
if (-not $DryRun) {
  $branch = (git rev-parse --abbrev-ref HEAD).Trim()
  if ($branch -ne 'main') { Fail "Must be on 'main' to release (on '$branch')." }
  if (git status --porcelain) { Fail "Working tree is not clean." }
  if (git tag -l $tag) { Fail "Tag $tag already exists." }
}

if ($DryRun) { Write-Host "[dry run] Would create and push annotated tag $tag"; exit 0 }

if (-not $Yes) {
  $ans = Read-Host "Create and push $tag ? (y/N)"
  if ($ans -notmatch '^[Yy]') { Write-Host "Aborted."; exit 0 }
}

git tag -a $tag -m "PS5 Payload Manager X $next"
git push fork $tag
Write-Host "Pushed $tag. release.yml will build and publish it."
