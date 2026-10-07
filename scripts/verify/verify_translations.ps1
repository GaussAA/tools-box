# Enforces the SOURCE_TARGETS rule for the translation pipeline (root CMakeLists.txt,
# qt_add_translations block; process documented in docs/workflow.md section 4 step 9):
# every target whose sources contain tr() must be listed in SOURCE_TARGETS.
#
# Why this is a script and not a review item: lupdate only scans the targets given in
# SOURCE_TARGETS. Add a new target (a new plugin, a freshly extracted static library)
# and forget to list it, and lupdate does not fail -- it simply stops seeing that
# target's strings, marks the existing English translations "vanished", and the
# pipeline stays green. That happened once when ToolBoxApp was extracted without being
# listed (27 strings went vanished). A sentence in a document did not stop it; this
# check does.
#
# How it works (deliberately conservative):
#   1. Parse the SOURCE_TARGETS list out of the root CMakeLists.txt.
#   2. Find every target definition (add_library / qt_add_library / add_executable /
#      qt_add_executable) outside tests/, together with its explicitly listed
#      .cpp/.h sources. Generated sources (${...}) are skipped.
#   3. A target is "translatable" if any of its listed sources matches \btr\( -- this
#      includes matches inside comments, which errs on the side of demanding a listing.
#   4. FAIL if a translatable target is missing from SOURCE_TARGETS. Listed targets
#      whose sources contain no tr() are reported as NOTE only (harmless: lupdate
#      scans nothing for them).
#
# Known non-issue: sdk/ToolBoxPlugin.h mentions tr() in a doc comment, but ToolBoxSdk
# is an INTERFACE library with no source list, so it is never scanned here. The
# metadata.json convention (hard-coded Chinese, deliberately not tr()-wrapped) is a
# documented decision in docs/workflow.md section 3.3, not a violation of this check.
#
# NOTE: this file must keep its UTF-8 BOM (see verify_whitespace.ps1).
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\verify\verify_translations.ps1
# Exit code 0 = clean, 1 = violations found.

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$git = (Get-Command git -ErrorAction SilentlyContinue)
if (-not $git) { Write-Output "FAIL: git not found on PATH"; exit 1 }

# Read as UTF-8 through .NET on purpose: PowerShell 5.1's Get-Content decodes BOM-less
# UTF-8 with the system ANSI codepage and mangles Chinese comments (see verify_coretest).
function Read-Utf8([string] $path) {
  return [System.IO.File]::ReadAllText($path)
}

# ── 1. SOURCE_TARGETS out of the root CMakeLists.txt ─────────────────────────────
$rootCmakePath = Join-Path $repo "CMakeLists.txt"
$rootText = Read-Utf8 $rootCmakePath

# Balanced-paren scan: extract the full qt_add_translations(...) argument list without
# assuming it is parenthesis-free (regex [^)]* would silently truncate it instead).
$qtIdx = $rootText.IndexOf("qt_add_translations")
if ($qtIdx -lt 0) {
  Write-Output "FAIL: qt_add_translations not found in root CMakeLists.txt"
  exit 1
}
$open = $rootText.IndexOf("(", $qtIdx)
$depth = 0
$close = -1
for ($i = $open; $i -lt $rootText.Length; $i++) {
  if ($rootText[$i] -eq "(") { $depth++ }
  elseif ($rootText[$i] -eq ")") { $depth--; if ($depth -eq 0) { $close = $i; break } }
}
if ($close -lt 0) {
  Write-Output "FAIL: unbalanced parentheses in qt_add_translations block"
  exit 1
}
$block = $rootText.Substring($open + 1, $close - $open - 1)

$stIdx = $block.IndexOf("SOURCE_TARGETS")
if ($stIdx -lt 0) {
  Write-Output "FAIL: SOURCE_TARGETS not found in the qt_add_translations block"
  exit 1
}
$listed = @()
foreach ($line in ($block.Substring($stIdx + "SOURCE_TARGETS".Length) -split "`n")) {
  $t = $line.Trim()
  # A closing paren or the next ALL-CAPS keyword (e.g. RESOURCE_PREFIX) ends the list.
  # -cmatch on purpose: -match is case-insensitive in PowerShell, so [A-Z] would
  # classify "ToolBox" as a keyword and break out of the list immediately.
  if ($t -match "^\)" -or $t -cmatch "^[A-Z][A-Z_]*\b") { break }
  foreach ($tok in ($t -split "\s+")) {
    if ($tok -match "^[A-Za-z0-9_:.-]+$") { $listed += $tok }
  }
}
$listed = @($listed | Sort-Object -Unique)

# ── 2. Targets and their explicitly listed sources (outside tests/) ──────────────
$cmakeFiles = @(& git -C $repo ls-files "*CMakeLists.txt") | Where-Object { $_ -notmatch "(^|/)tests/" }
$cmakeFiles = @($cmakeFiles | Sort-Object -Unique)

$targetRegex = [regex] "(?s)(add_library|qt_add_library|add_executable|qt_add_executable)\s*\(\s*([A-Za-z0-9_.:-]+)\s+([^)]*)\)"

$translatable = @{}
$listedNoTr = @()

foreach ($rel in $cmakeFiles) {
  $cmakeDir = Split-Path (Join-Path $repo $rel) -Parent
  $text = Read-Utf8 (Join-Path $repo $rel)

  foreach ($m in $targetRegex.Matches($text)) {
    $name = $m.Groups[2].Value
    # add_executable(${name} ${name}.cpp) inside function definitions: no real target.
    if ($name -like "*`$*") { continue }

    $hasTr = $false
    foreach ($tok in ($m.Groups[3].Value -split "\s+")) {
      $tok = $tok.Trim()
      if ($tok -notmatch "\.(cpp|h)$") { continue }
      if ($tok -like "*`$*") { continue }  # generated sources cannot be resolved here
      $srcPath = Join-Path $cmakeDir $tok
      if (-not (Test-Path $srcPath -PathType Leaf)) { continue }
      if ((Read-Utf8 $srcPath) -match "\btr\s*\(") { $hasTr = $true; break }
    }

    if ($hasTr) { $translatable[$name] = $rel }
    elseif ($listed -contains $name) { $listedNoTr += $name }
  }
}

Write-Output ("repo            = " + $repo)
Write-Output ("listed targets  = " + $listed.Count + " -> " + ($listed -join ", "))
Write-Output ("translatable    = " + $translatable.Count + " -> " + (($translatable.Keys | Sort-Object) -join ", "))

# ── 3. Verdict ───────────────────────────────────────────────────────────────────
$missing = @($translatable.Keys | Sort-Object | Where-Object { $listed -notcontains $_ })

foreach ($t in ($listedNoTr | Sort-Object -Unique)) {
  Write-Output ("NOTE: {0} is listed in SOURCE_TARGETS but its sources contain no tr() -- harmless, consider dropping" -f $t)
}

if ($missing.Count -gt 0) {
  Write-Output ""
  Write-Output ("FAIL: {0} translatable target(s) missing from SOURCE_TARGETS" -f $missing.Count)
  foreach ($t in $missing) {
    Write-Output ("  - {0} (defined in {1})" -f $t, $translatable[$t])
  }
  Write-Output "lupdate scans only the targets listed in SOURCE_TARGETS; strings of an unlisted target are silently dropped and existing translations go 'vanished'."
  Write-Output "Add the target to the qt_add_translations block in the root CMakeLists.txt, then run: cmake --build build --target update_translations"
  exit 1
}

Write-Output ("PASS: every target containing tr() is listed in SOURCE_TARGETS ({0} target(s) checked)" -f $translatable.Count)
exit 0
