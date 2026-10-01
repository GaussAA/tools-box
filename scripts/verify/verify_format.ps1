# Formatting check: every tracked C++ source must match .clang-format exactly.
# This is what makes the one-time normalization durable. Without it the tree drifts
# back within a few edits and the normalization is wasted.
#
# The first normalization is commit 73b1e4a, registered in .git-blame-ignore-revs
# (git blame would otherwise attribute every reformatted line to it). Any FUTURE
# formatting-only commit must be registered there too - see docs/workflow.md 3.1.
#
# clang-format ships with Visual Studio, so it is never "not installed" on a machine
# that can build this project. If it cannot be found the check fails loudly rather
# than silently passing.
#
# NOTE: this file must keep its UTF-8 BOM. Windows PowerShell 5.1 reads a BOM-less
# script with the system ANSI codepage (936 / GB2312 here), which mangles UTF-8 Chinese
# comments badly enough to break parsing. verify_whitespace.ps1 enforces the BOM.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\verify\verify_format.ps1
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

# ---------- locate clang-format ----------
$fromPath = Get-Command clang-format -ErrorAction SilentlyContinue
$vsPaths = @(
  "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe",
  "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\bin\clang-format.exe",
  "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe"
)
$cf = $null
if ($fromPath) { $cf = $fromPath.Source }
if (-not $cf) { foreach ($p in $vsPaths) { if (Test-Path $p) { $cf = $p; break } } }

Write-Output ("repo         = " + $repo)
if (-not $cf) {
  Write-Output "FAIL: clang-format not found (looked on PATH and in the Visual Studio LLVM dirs)"
  Write-Output "DONE(FAILED 1): clang-format not found"
  exit 1
}
Write-Output ("clang-format = " + $cf)
Write-Output ("version      = " + ((& $cf --version) 2>&1))

$files = @(& git -C $repo ls-files "*.cpp" "*.h" "*.hpp")
Write-Output ("scope        = " + $files.Count + " C++ file(s)")

# ---------- dry run ----------
# --dry-run --Werror emits one diagnostic per violation on stderr and exits non-zero
# if any file would change. Nothing is written.
#
# The diagnostics come back on stderr, and with $ErrorActionPreference = "Stop" a
# native command writing to stderr is turned into a terminating error - which would
# make the script die on its first violation instead of reporting it. Relax the
# preference just for this call, then restore it.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$raw = & $cf --dry-run --Werror @files 2>&1
$cfExit = $LASTEXITCODE
$ErrorActionPreference = $prevEap

$violations = @()
$hitFiles = @()
foreach ($line in @($raw)) {
  if ($line -match '^(.+?):(\d+):(\d+): (?:error|warning): code should be clang-formatted') {
    $rel = $matches[1] -replace [regex]::Escape($repo + [IO.Path]::DirectorySeparatorChar), ''
    $rel = $rel -replace '\\', '/'
    $violations += ("{0}:{1}:{2}" -f $rel, $matches[2], $matches[3])
    if ($hitFiles -notcontains $rel) { $hitFiles += $rel }
  }
}
Write-Output ("clang-format exit = " + $cfExit)

Check ($files.Count -gt 0)      ("found C++ sources to check ({0})" -f $files.Count)
Check ($violations.Count -eq 0) ("all sources match .clang-format ({0} violation(s) in {1} file(s))" -f $violations.Count, $hitFiles.Count)

if ($hitFiles.Count -gt 0) {
  Write-Output "-- files needing formatting"
  $hitFiles | ForEach-Object { Write-Output ("   " + $_) }
  Write-Output "-- how to fix"
  Write-Output ("   & `"" + $cf + "`" -i " + ($hitFiles -join " "))
  Write-Output "   then, if the commit is formatting-only, add its hash to .git-blame-ignore-revs"
}

if ($failed.Count -gt 0) {
  Write-Output ("DONE(FAILED {0}): {1}" -f $failed.Count, ($failed -join "; "))
  exit 1
}
Write-Output "DONE(ALL PASS)"