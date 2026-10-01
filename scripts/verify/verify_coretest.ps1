# Enforces docs/workflow.md section 5: "every source under */core/ must have a Qt Test
# case". That rule was until now only a sentence in a document -- nothing checked it,
# so a new core module could be added with no test and every check would stay green.
#
# Why this instead of line coverage: real coverage needs a tool this project does not
# have. Visual Studio Community does not ship Code Coverage (Enterprise only), and the
# alternatives (OpenCppCoverage, clang-cl + llvm-cov) would add a third-party toolchain
# to every developer machine and to CI. This check is the honest, mechanically
# verifiable lower bound: the module is referenced by a test at all. "The test exists"
# and "the test is any good" are different questions -- the latter stays with review.
#
# NOTE: this file must keep its UTF-8 BOM (see verify_whitespace.ps1).
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\verify\verify_coretest.ps1
# Exit code 0 = clean, 1 = violations found.

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$git = (Get-Command git -ErrorAction SilentlyContinue)
if (-not $git) { Write-Output "FAIL: git not found on PATH"; exit 1 }

$coreSources = @(& git -C $repo ls-files "app/core/*.cpp" "plugins/*/core/*.cpp") `
             + @(& git -C $repo ls-files --others --exclude-standard "app/core/*.cpp" "plugins/*/core/*.cpp")
$coreSources = @($coreSources | Sort-Object -Unique)

$testSources = @(& git -C $repo ls-files "tests/*.cpp") `
             + @(& git -C $repo ls-files --others --exclude-standard "tests/*.cpp")

Write-Output ("repo        = " + $repo)
Write-Output ("core files  = " + $coreSources.Count)
Write-Output ("test files  = " + $testSources.Count)

# Read as UTF-8 through .NET on purpose: PowerShell 5.1's Get-Content decodes BOM-less
# UTF-8 with the system ANSI codepage, which mangles the Chinese comments these files
# are full of (the same trap that once made verify_filesize count a file 88 lines short).
$testText = ""
foreach ($rel in $testSources) {
  $path = Join-Path $repo $rel
  if (Test-Path $path -PathType Leaf) {
    $testText += [System.IO.File]::ReadAllText($path)
  }
}

$missing = @()

foreach ($rel in $coreSources) {
  $path = Join-Path $repo $rel
  if (-not (Test-Path $path -PathType Leaf)) { continue }

  # The convention is one class per file, so the header carries the same stem.
  $stem = [System.IO.Path]::GetFileNameWithoutExtension($rel)
  $header = $stem + ".h"

  if ($testText -match ("(core/)?$([regex]::Escape($stem))\.h")) {
    Write-Output ("OK:     {0} <- {1}" -f $rel, $header)
  } else {
    $missing += $rel
    Write-Output ("MISSING:{0} (no test includes {1})" -f $rel, $header)
  }
}

if ($missing.Count -gt 0) {
  Write-Output ""
  Write-Output ("FAIL: {0} core file(s) have no test case" -f $missing.Count)
  Write-Output "Add tests/tst_<module>.cpp, register it with toolbox_add_test(), and update the table in docs/workflow.md section 5."
  exit 1
}

Write-Output ("PASS: every core/ source is covered by a test ({0} file(s))" -f $coreSources.Count)
exit 0
