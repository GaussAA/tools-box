# Runs every automatic check in scripts/verify, in the order they should run.
#
# Why this exists: the nine checks were only ever invoked one by one, by hand, from
# whatever paragraph of docs/workflow.md the reader happened to find first. On a
# machine that has never run them the natural outcome is "ran two of them and moved
# on". One command that runs all of them, in order, and reports a single verdict
# costs nothing and removes that failure mode.
#
# What it does NOT do: the UI and real-download scripts (verify_shell, verify_recent,
# verify_videodl*) need a real window and a real network, so they stay manual -- this
# script prints their list at the end instead of pretending to cover them.
#
# NOTE: this file must keep its UTF-8 BOM (see verify_whitespace.ps1).
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\verify\run_all.ps1
# Exit code 0 = every automatic check passed, 1 = at least one failed.

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$here = $PSScriptRoot

# name -> whether it needs a build first
$checks = @(
  @{ Name = "verify_whitespace";        NeedsBuild = $false }
  @{ Name = "verify_format";            NeedsBuild = $false }
  @{ Name = "verify_conventions";       NeedsBuild = $false }
  @{ Name = "verify_moduleboundaries";  NeedsBuild = $false }
  @{ Name = "verify_filesize";          NeedsBuild = $false }
  @{ Name = "verify_coretest";          NeedsBuild = $true  }
  @{ Name = "verify_translations";      NeedsBuild = $false }
  @{ Name = "verify_docs";              NeedsBuild = $false }
  @{ Name = "verify_naming";            NeedsBuild = $true  }
)

$compileCommands = Join-Path $repo "build/compile_commands.json"
$failed = @()

Write-Output ("repo = " + $repo)
Write-Output ""

foreach ($check in $checks) {
  $name = $check.Name
  $script = Join-Path $here ($name + ".ps1")
  if (-not (Test-Path $script -PathType Leaf)) {
    Write-Output ("SKIP: {0} (script not found)" -f $name)
    continue
  }

  if ($check.NeedsBuild -and -not (Test-Path $compileCommands)) {
    # Bailing out loudly is the point: verify_naming silently checks nothing when
    # the moc files are missing, so "no build" must not look like "all good".
    Write-Output ("SKIP: {0} needs build/compile_commands.json -- build first (cmake --build --preset debug)" -f $name)
    $failed += $name
    continue
  }

  # verify_coretest reads the Debug build products (the core libraries' symbol
  # tables and the test object files) rather than compile_commands.json, so the
  # generic NeedsBuild test above does not cover it. Same principle: a gate that
  # cannot see its input must say so rather than pass. Scan the whole build tree
  # (not just build\tests): since plan D3, a test registered from a module puts
  # its object file next to that module's sources (build\plugins\..., build\app\...).
  if ($name -eq "verify_coretest") {
    $debugObjs = @(Get-ChildItem (Join-Path $repo "build") -Recurse -Filter "tst_*.cpp.obj" -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -match '\\Debug\\' })
    if ($debugObjs.Count -eq 0) {
      Write-Output "SKIP: verify_coretest needs a Debug build (build\**\Debug\tst_*.cpp.obj) -- run scripts\build_verify.ps1 first"
      $failed += $name
      continue
    }
  }

  Write-Output ("=== {0} ===" -f $name)
  & powershell -NoProfile -ExecutionPolicy Bypass -File $script
  $code = $LASTEXITCODE
  if ($code -ne 0) {
    $failed += $name
    Write-Output ("FAIL: {0} (exit {1})" -f $name, $code)
  } else {
    Write-Output ("PASS: {0}" -f $name)
  }
  Write-Output ""
}

Write-Output "--- manual regression (needs a real window / real network) ---"
foreach ($manual in @("verify_shell", "verify_recent", "verify_videodl", "verify_videodl2",
                      "verify_videodl_download", "verify_videodl_fetch", "verify_videodl_platform",
                      "verify_videodl_reuse", "verify_videodl_status", "verify_videodl_logread")) {
  Write-Output ("  scripts/verify/{0}.ps1" -f $manual)
}

Write-Output ""
if ($failed.Count -gt 0) {
  Write-Output ("FAILED: {0} check(s) -- {1}" -f $failed.Count, ($failed -join ", "))
  exit 1
}
Write-Output "PASSED: all automatic checks"
exit 0
