# Enforces docs/workflow.md section 5: "every source under */core/ must have a Qt Test
# case". That rule was until now only a sentence in a document -- nothing checked it,
# so a new core module could be added with no test and every check would stay green.
#
# Since 2026-10-07 it covers the orchestration layer as well (the *_orch static
# libraries, see docs/architecture.md section 3.1). Those files do not live under a
# core/ directory -- they hold user-visible tr() strings, so by the MVP table in
# architecture section 3 they belong to the View side -- which means the directory
# pattern cannot find them. They are recognised by being listed in a "<name>_orch"
# static library in CMake instead.
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

# ---------- orchestration layer: sources listed in a <name>_orch static library ----------
# Why parse CMake instead of using a directory pattern: this layer sits next to the
# plugin entry point, not in core/ -- it holds tr() strings, so by the MVP table in
# docs/architecture.md section 3 it is View-side code. Being named in a "_orch"
# library is the only thing that distinguishes it from the page itself.
$orchSources = @()
$cmakeFiles = @(& git -C $repo ls-files | Where-Object { $_ -eq "CMakeLists.txt" -or $_ -like "*/CMakeLists.txt" })
$repoFull = [System.IO.Path]::GetFullPath($repo)
foreach ($rel in $cmakeFiles) {
  $path = Join-Path $repo $rel
  if (-not (Test-Path $path -PathType Leaf)) { continue }
  $text = [System.IO.File]::ReadAllText($path)
  $dir = [System.IO.Path]::GetDirectoryName($path)
  foreach ($lib in [regex]::Matches($text, '(?s)qt_add_library\s*\(\s*[A-Za-z0-9_]+_orch\s+STATIC(.*?)\)')) {
    foreach ($src in [regex]::Matches($lib.Groups[1].Value, '([A-Za-z0-9_./]+\.cpp)')) {
      $full = [System.IO.Path]::GetFullPath((Join-Path $dir $src.Groups[1].Value))
      if ($full.StartsWith($repoFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        $orchSources += ($full.Substring($repoFull.Length).TrimStart('\', '/') -replace '\\', '/')
      }
    }
  }
}
$orchSources = @($orchSources | Sort-Object -Unique)

# label -> the sources that must be referenced by some test
$groups = @(
  @{ Label = "core"; Sources = $coreSources },
  @{ Label = "orch"; Sources = $orchSources }
)

$testSources = @(& git -C $repo ls-files "tests/*.cpp") `
             + @(& git -C $repo ls-files --others --exclude-standard "tests/*.cpp")

Write-Output ("repo        = " + $repo)
Write-Output ("core files  = " + $coreSources.Count)
Write-Output ("orch files  = " + $orchSources.Count)
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

foreach ($group in $groups) {
  $label = $group.Label
  foreach ($rel in $group.Sources) {
    $path = Join-Path $repo $rel
    if (-not (Test-Path $path -PathType Leaf)) { continue }

    # The convention is one class per file, so the header carries the same stem.
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($rel)
    $header = $stem + ".h"

    if ($testText -match ("(core/)?$([regex]::Escape($stem))\.h")) {
      Write-Output ("OK:     {0} <- {1} [{2}]" -f $rel, $header, $label)
    } else {
      $missing += $rel
      Write-Output ("MISSING:{0} (no test includes {1}) [{2}]" -f $rel, $header, $label)
    }
  }
}

if ($missing.Count -gt 0) {
  Write-Output ""
  Write-Output ("FAIL: {0} core/orch file(s) have no test case" -f $missing.Count)
  Write-Output "Add tests/tst_<module>.cpp, register it with toolbox_add_test(), and update the tables in docs/workflow.md section 5."
  exit 1
}

Write-Output ("PASS: every core/ and orch source is covered by a test ({0} + {1} file(s))" -f $coreSources.Count, $orchSources.Count)
exit 0
