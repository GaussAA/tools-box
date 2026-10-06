# Pre-commit gate: run every automatic check that needs no build.
#
# Why this exists: the eight checks were reachable only through CI or by typing
# run_all.ps1 by hand. On a machine where nobody ran them, the natural outcome was
# "commit now, let CI tell me" -- and CI is minutes away and shared. docs/error_ledger.md
# records a real instance: verify_format was skipped locally, CI caught 12 violations
# in 3 files, and the fix had to be a separate formatting-only commit.
#
# Which checks run here (the ones that are seconds and need no build):
#   verify_whitespace, verify_format, verify_conventions, verify_filesize,
#   verify_coretest, verify_translations, verify_docs
#
# Deliberately NOT run:
#   verify_naming -- needs build/compile_commands.json (clang-tidy). It stays in CI
#   and in run_all.ps1; a hook that requires a full build before every commit would
#   train people to use --no-verify, which is worse than not having the hook.
#
# Nothing here is a substitute for CI: this machine may have a different clang-format
# or a stale build. CI remains the authority.
#
# NOTE: this file must keep its UTF-8 BOM (see verify_whitespace.ps1).
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\verify\pre_commit.ps1
# Exit code 0 = clean, 1 = at least one check failed (git aborts the commit).

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$here = $PSScriptRoot
Set-Location $repo

$checks = @(
  "verify_whitespace",
  "verify_format",
  "verify_conventions",
  "verify_filesize",
  "verify_coretest",
  "verify_translations",
  "verify_docs"
)

$failed = @()

Write-Output "pre-commit: running $($checks.Count) checks (no build required)"
Write-Output ""

foreach ($name in $checks) {
  $script = Join-Path $here ($name + ".ps1")
  if (-not (Test-Path $script -PathType Leaf)) {
    Write-Output ("MISSING: {0}" -f $name)
    $failed += $name
    continue
  }

  & powershell -NoProfile -ExecutionPolicy Bypass -File $script
  if ($LASTEXITCODE -ne 0) {
    $failed += $name
    Write-Output ("FAILED:  {0}" -f $name)
  } else {
    Write-Output ("passed:  {0}" -f $name)
  }
}

Write-Output ""
if ($failed.Count -gt 0) {
  Write-Output ("COMMIT BLOCKED: {0} check(s) failed -- {1}" -f $failed.Count, ($failed -join ", "))
  Write-Output "Fix them, or (rarely) commit with --no-verify. CI runs the same checks"
  Write-Output "plus the build, so --no-verify only moves the failure later."
  exit 1
}

Write-Output "pre-commit: all checks passed"
exit 0
