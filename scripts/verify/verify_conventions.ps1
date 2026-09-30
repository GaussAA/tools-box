# Mechanical checks for the conventions that can be tested by reading source text.
# Complements verify_whitespace.ps1 (encoding/whitespace) and the compiler
# (/W4 /WX plus the core libraries linking only Qt6::Core).
#
# Rules enforced here, all of which docs/coding-standards.md section 12 lists:
#   1. no legacy SIGNAL() / SLOT() signal-slot syntax
#   2. no QString("literal") / QString::fromUtf8("literal") / fromLatin1("literal");
#      user-visible strings go through tr() or QStringLiteral()
#   3. no cross-layer includes: plugins/ must not reach into app/, app/ must not
#      reach into plugins/, sdk/ must not reach into either
#   4. no bare string QSettings keys in app/ or plugins/; keys must go through
#      ToolSettings or a named constant so the ui/* vs plugin/<id>/* split holds
#
# NOTE: keep every literal at the PowerShell level ASCII. PowerShell 5.1 parses a
# BOM-less .ps1 as ANSI, so Chinese in comments/strings corrupts the token stream.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\verify\verify_conventions.ps1
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

# Only sources are inspected; CMake files have their own conventions.
$sources = @(& git -C $repo ls-files "*.cpp" "*.h" "*.hpp")
$scan = @()
foreach ($rel in $sources) {
  $path = Join-Path $repo $rel
  if (Test-Path $path -PathType Leaf) { $scan += , @{ Rel = $rel; Path = $path } }
}
Write-Output ("repo      = " + $repo)
Write-Output ("sources   = " + $scan.Count + " files")

function Hits($pattern, $filter) {
  $out = @()
  foreach ($item in $scan) {
    if ($filter -and -not (& $filter $item.Rel)) { continue }
    $m = Select-String -Path $item.Path -Pattern $pattern -AllMatches
    foreach ($one in $m) {
      foreach ($hit in $one.Matches) {
        $out += ("{0}:{1}: {2}" -f $item.Rel, $one.LineNumber, $one.Line.Trim())
      }
    }
  }
  return $out
}

# ---------- 1. legacy signal/slot syntax ----------
$legacy = Hits 'SIGNAL\(|SLOT\(' $null
Check ($legacy.Count -eq 0) ("no legacy SIGNAL()/SLOT() syntax ({0} hit(s))" -f $legacy.Count)

# ---------- 2. string literals built with the wrong constructor ----------
# QStringLiteral(...) does not match: the pattern needs "(" directly after QString.
$badStrings = Hits 'QString\("|QString::fromUtf8\("|QString::fromLatin1\("' $null
Check ($badStrings.Count -eq 0) ("string literals use tr()/QStringLiteral() ({0} hit(s))" -f $badStrings.Count)

# ---------- 3. cross-layer includes ----------
# The leading class is a "not part of a longer word" guard, so that a quoted
# include such as #include "plugins/x.h" and #include "../app/y.h" both match,
# while an unrelated name like myapp/ does not.
$layerRules = @(
  @{ Name = "plugins/ does not include app/";         From = '^plugins/'; To = '(^|[^A-Za-z0-9_])app/' },
  @{ Name = "app/ does not include plugins/";         From = '^app/';     To = '(^|[^A-Za-z0-9_])plugins/' },
  @{ Name = "sdk/ does not include app/ or plugins/"; From = '^sdk/';     To = '(^|[^A-Za-z0-9_])(app|plugins)/' }
)
$crossHits = @()
$crossSummary = @()
foreach ($rule in $layerRules) {
  $fromRe = $rule.From; $toRe = $rule.To
  $found = @()
  foreach ($item in $scan) {
    if ($item.Rel -notmatch $fromRe) { continue }
    $m = Select-String -Path $item.Path -Pattern '^\s*#include\s*"' -AllMatches
    foreach ($one in $m) {
      if ($one.Line -match $toRe) { $found += ("{0}:{1}: {2}" -f $item.Rel, $one.LineNumber, $one.Line.Trim()) }
    }
  }
  $crossSummary += ("{0} ({1} hit(s))" -f $rule.Name, $found.Count)
  $crossHits += $found
}
Check ($crossHits.Count -eq 0) ("no cross-layer includes - " + ($crossSummary -join "; "))

# ---------- 4. bare QSettings keys ----------
$bareKeys = Hits 'QSettings\(\)\s*\.\s*(value|setValue|remove)\s*\(\s*"' $null
Check ($bareKeys.Count -eq 0) ("QSettings keys go through ToolSettings/named constants ({0} hit(s))" -f $bareKeys.Count)

# ---------- report ----------
foreach ($group in @(
    @{ Label = "legacy SIGNAL()/SLOT()";        Items = $legacy },
    @{ Label = "QString(" + [char]34 + "literal" + [char]34 + ")"; Items = $badStrings },
    @{ Label = "cross-layer include";           Items = $crossHits },
    @{ Label = "bare QSettings key";            Items = $bareKeys })) {
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
