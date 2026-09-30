# Whitespace / encoding check for every tracked file.
# Enforces the rules that .editorconfig declares and that are editor-independent:
#   - LF only, no CR byte
#   - exactly one trailing newline
#   - no trailing whitespace on any line
#   - no tab characters in source files (indent_style = space)
# Covers docs/coding-standards.md section 12, row "charset / indent / final newline".
# The clang-format layer is deliberately NOT checked here; see docs/workflow.md 3.1.
#
# NOTE: keep every literal at the PowerShell level ASCII. PowerShell 5.1 parses a
# BOM-less .ps1 as ANSI, so Chinese in comments/strings corrupts the token stream.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\verify\verify_whitespace.ps1
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

# .editorconfig switches trailing-whitespace trimming off for patches.
$noTrimSuffixes = @(".patch", ".diff")
# Tabs are only forbidden where the source actually uses spaces.
$tabCheckedSuffixes = @(".cpp", ".h", ".ps1", ".cmake", ".json", ".yml", ".yaml", ".md", ".svg")
$tabCheckedNames = @("CMakeLists.txt")

$tracked = @(& git -C $repo ls-files)
Write-Output ("repo        = " + $repo)
Write-Output ("tracked     = " + $tracked.Count + " files")

$checked = 0
$crFiles = @()
$finalNewlineFiles = @()
$blankTailFiles = @()
$trailingFiles = @()
$trailingCount = 0
$tabFiles = @()

foreach ($rel in $tracked) {
  $path = Join-Path $repo $rel
  if (-not (Test-Path $path -PathType Leaf)) { continue }

  $bytes = [System.IO.File]::ReadAllBytes($path)
  if ($bytes.Length -eq 0) { continue }
  # Skip anything that looks binary (NUL byte in the first 4 KB).
  $probe = [Math]::Min($bytes.Length, 4096)
  $isBinary = $false
  for ($i = 0; $i -lt $probe; $i++) { if ($bytes[$i] -eq 0) { $isBinary = $true; break } }
  if ($isBinary) { continue }
  $checked++

  # 1. LF only
  if ($bytes -contains 13) { $crFiles += $rel }

  # 2. exactly one trailing newline
  if ($bytes[$bytes.Length - 1] -ne 10) {
    $finalNewlineFiles += $rel
  } elseif ($bytes.Length -ge 2 -and $bytes[$bytes.Length - 2] -eq 10) {
    $blankTailFiles += $rel
  }

  $ext = [System.IO.Path]::GetExtension($rel).ToLowerInvariant()
  $name = [System.IO.Path]::GetFileName($rel)
  $lines = [System.IO.File]::ReadAllLines($path)

  # 3. trailing whitespace
  if ($noTrimSuffixes -notcontains $ext) {
    $hits = 0
    foreach ($line in $lines) {
      if ($line -match '[ \t]+$') { $hits++ }
    }
    if ($hits -gt 0) { $trailingFiles += ($rel + " (" + $hits + " line(s))"); $trailingCount += $hits }
  }

  # 4. no tabs
  if ($tabCheckedSuffixes -contains $ext -or $tabCheckedNames -contains $name) {
    foreach ($line in $lines) {
      if ($line.Contains("`t")) { $tabFiles += $rel; break }
    }
  }
}

Check ($checked -gt 0)                          ("scanned {0} text files" -f $checked)
Check ($crFiles.Count -eq 0)                    ("LF only, no CR byte ({0} offender(s))" -f $crFiles.Count)
Check ($finalNewlineFiles.Count -eq 0)          ("file ends with a newline ({0} offender(s))" -f $finalNewlineFiles.Count)
Check ($blankTailFiles.Count -eq 0)             ("no blank line at end of file ({0} offender(s))" -f $blankTailFiles.Count)
Check ($trailingCount -eq 0)                    ("no trailing whitespace ({0} line(s))" -f $trailingCount)
Check ($tabFiles.Count -eq 0)                   ("no tab characters in source ({0} offender(s))" -f $tabFiles.Count)

foreach ($group in @(
    @{ Label = "CR byte";           Items = $crFiles },
    @{ Label = "no final newline";  Items = $finalNewlineFiles },
    @{ Label = "blank line at EOF"; Items = $blankTailFiles },
    @{ Label = "trailing space";    Items = $trailingFiles },
    @{ Label = "tab character";     Items = $tabFiles })) {
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
