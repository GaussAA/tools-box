# Mechanical checks for the documents under docs/, which are the single source of
# truth for this project (see docs/architecture.md preamble and workflow.md
# section 10). Docs drift silently: a section gets renumbered, a file gets moved,
# and every reference to it rots without anybody noticing. These are the parts of
# "no drift" that a script can decide.
#
# Rules enforced here:
#   1. every relative link target in docs/*.md exists on disk
#   2. every '#anchor' resolves to a heading in the target document
#      (GitHub-flavored slug: lowercase, drop punctuation, spaces to hyphens;
#       Chinese characters are kept as-is)
#   3. no orphan document: every docs/*.md is linked from at least one other doc
#      in the set, so a newly added document cannot be forgotten
#
# NOTE: keep every literal at the PowerShell level ASCII. PowerShell 5.1 parses a
# BOM-less .ps1 as ANSI, so Chinese in comments/strings corrupts the token stream.
# Chinese only ever enters this script as document content read from disk.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\verify\verify_docs.ps1
# Exit code 0 = clean, 1 = violations found.

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$git = (Get-Command git -ErrorAction SilentlyContinue)
if (-not $git) { Write-Output "FAIL: git not found on PATH"; exit 1 }

$failed = @()
function Check($ok, $what) {
  if ($ok) { Write-Output "PASS: $what" }
  else     { Write-Output "FAIL: $what"; $script:failed += $what }
}

$docsRel = @(& git -C $repo ls-files "docs/*.md")
if ($docsRel.Count -eq 0) { Write-Output "FAIL: no documents under docs/"; exit 1 }

# ---------- heading -> anchor, GitHub flavored ----------
$anchorCache = @{}
function Get-Anchors([string]$path) {
  if ($anchorCache.ContainsKey($path)) { return $anchorCache[$path] }
  $set = New-Object System.Collections.Generic.HashSet[string]
  foreach ($line in (Get-Content -LiteralPath $path -Encoding UTF8)) {
    if ($line -match '^(#{1,6})\s+(.+?)\s*$') {
      $slug = $matches[2].ToLowerInvariant()
      $slug = $slug -replace '\[([^\]]*)\]\([^)]*\)', '$1'   # [text](url) -> text
      $slug = $slug -replace '[`*]', ''
      $slug = $slug -replace '[^\p{L}\p{Nd}\- _]', ''
      $slug = $slug -replace ' ', '-'
      [void]$set.Add($slug)
    }
  }
  $anchorCache[$path] = $set
  return $set
}

# ---------- walk every link in every document ----------
$visited = New-Object System.Collections.Generic.HashSet[string]  # link targets, for rule 3
$linkCount = 0
$fileMiss = @()
$anchorMiss = @()

foreach ($rel in $docsRel) {
  $docPath = Join-Path $repo $rel
  if (-not (Test-Path $docPath -PathType Leaf)) { continue }
  $docDir = Split-Path $docPath -Parent

  $lineNo = 0
  foreach ($line in (Get-Content -LiteralPath $docPath -Encoding UTF8)) {
    $lineNo++
    foreach ($m in [regex]::Matches($line, '\[[^\]]*\]\(([^)]+)\)')) {
      $target = $m.Groups[1].Value.Trim()
      if ($target -match '^(https?|mailto|ftp):') { continue }
      if ($target.StartsWith('#')) {
        # same-document anchor
        $linkCount++
        if (-not (Get-Anchors $docPath).Contains($target.Substring(1))) {
          $anchorMiss += ("{0}:{1}: {2} -> {3}" -f $rel, $lineNo, $target, "(same document)")
        }
        continue
      }

      $linkCount++
      $pathPart = $target
      $anchor = ""
      $hash = $target.IndexOf('#')
      if ($hash -ge 0) { $pathPart = $target.Substring(0, $hash); $anchor = $target.Substring($hash + 1) }

      $resolved = [System.IO.Path]::GetFullPath((Join-Path $docDir $pathPart))
      if (-not (Test-Path -LiteralPath $resolved)) {
        $fileMiss += ("{0}:{1}: {2}" -f $rel, $lineNo, $target)
        continue
      }
      if ($pathPart.EndsWith('.md')) {
        [void]$visited.Add("./" + (Split-Path $pathPart -Leaf))
        if ($anchor -and -not (Get-Anchors $resolved).Contains($anchor)) {
          $anchorMiss += ("{0}:{1}: {2} -> {3}" -f $rel, $lineNo, $target, "(heading not found)")
        }
      }
    }
  }
}

Write-Output ("repo      = " + $repo)
Write-Output ("documents = " + $docsRel.Count + " file(s), " + $linkCount + " link(s)")

Check ($fileMiss.Count -eq 0)  ("relative link targets exist ({0} miss)" -f $fileMiss.Count)
Check ($anchorMiss.Count -eq 0) ("anchors resolve to headings ({0} miss)" -f $anchorMiss.Count)

$orphans = @()
foreach ($rel in $docsRel) {
  if (-not $visited.Contains("./" + (Split-Path $rel -Leaf))) { $orphans += $rel }
}
Check ($orphans.Count -eq 0) ("no orphan document ({0} orphan)" -f $orphans.Count)

# ---------- report ----------
foreach ($group in @(
    @{ Label = "missing link target"; Items = $fileMiss },
    @{ Label = "missing anchor";      Items = $anchorMiss },
    @{ Label = "orphan document";     Items = $orphans })) {
  if ($group.Items.Count -gt 0) {
    Write-Output ("-- " + $group.Label)
    $group.Items | Select-Object -First 20 | ForEach-Object { Write-Output ("   " + $_) }
    if ($group.Items.Count -gt 20) { Write-Output ("   ... and {0} more" -f ($group.Items.Count - 20)) }
  }
}

if ($failed.Count -gt 0) {
  Write-Output ("DONE(FAILED {0}): {1}" -f $failed.Count, ($failed -join "; "))
  exit 1
}
Write-Output "DONE(ALL PASS)"
