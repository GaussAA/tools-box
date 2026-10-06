# Points git at the hooks that live in this repository (.githooks/) instead of the
# per-clone .git/hooks/ directory.
#
# Why a script instead of copying files into .git/hooks: git never version-controls
# .git/hooks, so anything placed there vanishes on a fresh clone and is invisible in
# review. Putting the hooks in .githooks/ and setting core.hooksPath makes them part
# of the tree: they are reviewed, and they survive a re-clone.
#
# Why it still has to be run by hand once per clone: core.hooksPath is a local
# repository setting; git has no mechanism for a clone to inherit it from the
# repository. That is a git limitation, not a choice. So: run this once after cloning.
#
# NOTE: this file must keep its UTF-8 BOM (see verify_whitespace.ps1).
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\install_hooks.ps1
# Exit code 0 = hooks are active.

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repo

$hooksDir = ".githooks"
$hook = Join-Path $hooksDir "pre-commit"

if (-not (Test-Path $hook -PathType Leaf)) {
  Write-Output "FAIL: $hook not found (are you running this from the repository?)"
  exit 1
}

# Git executes hooks through /bin/sh on Windows, so the file needs the executable
# bit in git's index. core.filemode is false on this filesystem, so git update-index
# is the way to set it -- a plain chmod would not be recorded.
& git update-index --add --chmod=+x $hook
if ($LASTEXITCODE -ne 0) {
  Write-Output "WARNING: could not mark $hook executable; git may refuse to run it."
}

& git config core.hooksPath $hooksDir
if ($LASTEXITCODE -ne 0) {
  Write-Output "FAIL: git config core.hooksPath failed"
  exit 1
}

$current = (& git config --get core.hooksPath).Trim()
if ($current -ne $hooksDir) {
  Write-Output "FAIL: core.hooksPath did not stick (got '$current')"
  exit 1
}

Write-Output "hooks      = $hooksDir (now used by git for this clone)"
Write-Output "pre-commit = whitespace / format / conventions / filesize / coretest / translations / docs"
Write-Output ""
Write-Output "Verify it is live:  git config --get core.hooksPath   (expect .githooks)"
Write-Output "Try it:             git commit --allow-empty -m test  (then reset)"
Write-Output "Note: this is a per-clone setting. After a fresh clone, run this script again."
exit 0
