# Module boundary gate (plan C of the "modular" work, 2026-10-08).
#
# Three rules, all mechanical:
#
#   R1  No module reaches into another module's sources. verify_conventions
#       checks the *layer* boundaries (plugins vs app vs sdk), but nothing
#       checked the *module* boundaries between plugins -- a plugin reaching
#       into another plugin's core/ is exactly the coupling the per-plugin
#       layout exists to prevent, and it would sail through every existing
#       gate. Includes are resolved against real files (file dir, module root,
#       repo root) instead of matched as strings, so a hit is a file that
#       actually exists inside a foreign module -- not a false positive on
#       some similarly named Qt header.
#
#   R2  Every tst_*.cpp must be registered with toolbox_add_test() from the
#       CMakeLists.txt of the module that owns it. A test file without a
#       registration is a silent hole: it compiles into nothing, ctest runs
#       one case fewer, and no other gate goes red.
#
#   R3  Every tst_*.cpp must live in <module>/tests/ (plan D3's final layout,
#       where even the top-level tests/ directory was dissolved).
#
#   R4  No CMakeLists.txt may put another module's directory on an include
#       path. Plan B made module include directories PRIVATE, so linking a
#       foreign library no longer grants its headers -- R4 closes the
#       remaining hole: explicitly adding the foreign directory by hand.
#       Together they move the module boundary from "a script that notices"
#       to "the compiler refuses" (verified 2026-10-08: a probe target that
#       linked base64_core and included its internal header failed with
#       C1083 before this gate was even consulted).
#
# Needs no build. See docs/architecture.md plan D3 and the C-gate notes.
#
# NOTE: this file must keep its UTF-8 BOM (see verify_whitespace.ps1).

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

$violations = 0
function Check([bool]$ok, [string]$msg) {
  if (-not $ok) {
    Write-Output ("  BOUNDARY HIT: " + $msg)
    $script:violations += 1
  }
}

# ---- module mapping --------------------------------------------------------

# Returns the module id of a repo-relative path (file OR directory), or $null
# for "not in a module". The (/$) alternatives matter: R4 feeds *directory*
# paths like "plugins/videodl" (a CMakeLists.txt minus its filename), which
# the original "plugins/([^/]+)/" form silently rejected -- found because the
# R4 probe verification came back green when it had to be red.
function Get-ModuleOf([string]$rel) {
  if ($rel -match '^plugins/([^/]+)(/|$)') { return "plugins/" + $Matches[1] }
  if ($rel -match '^app(/|$)')  { return "app" }
  if ($rel -match '^sdk(/|$)')  { return "sdk" }
  return $null
}

function Get-ModuleRoot([string]$module) {
  if ($module -eq "app") { return (Join-Path $repo "app") }
  if ($module -eq "sdk") { return (Join-Path $repo "sdk") }
  if ($module -like "plugins/*") { return (Join-Path $repo ($module -replace '/', '\')) }
  return $null
}

# ---- collect sources -------------------------------------------------------

$files = @(& git -C $repo ls-files) + @(& git -C $repo ls-files --others --exclude-standard)
$files = @($files | Where-Object { $_ -match '\.(cpp|h)$' } | Sort-Object -Unique)

Write-Output ("repo        = " + $repo)
Write-Output ("sources     = " + $files.Count + " file(s)")

# ---- R1: no cross-module includes ------------------------------------------

$hits1 = 0
foreach ($f in $files) {
  $fmod = Get-ModuleOf $f
  if (-not $fmod) { continue }
  $fdir = Split-Path (Join-Path $repo $f) -Parent
  $mroot = Get-ModuleRoot $fmod

  $text = [System.IO.File]::ReadAllText((Join-Path $repo $f))
  foreach ($m in [regex]::Matches($text, '#include\s*[<"]([^">]+)[">]')) {
    $inc = $m.Groups[1].Value
    if ($inc -match '\.moc$' -or $inc -match '^ui_') { continue }

    # Resolve against real files: file's own dir, then module root, then repo
    # root -- mirrors how the compiler actually finds quoted and angled
    # includes in this project, so only includes that land in a real file
    # inside a foreign module are counted.
    $target = $null
    foreach ($base in @($fdir, $mroot, $repo)) {
      $cand = Join-Path $base $inc
      if (Test-Path $cand -PathType Leaf) { $target = (Resolve-Path $cand).Path; break }
    }
    if (-not $target) { continue }

    $rel = $target.Substring($repo.Length).TrimStart('\', '/') -replace '\\', '/'
    $tmod = Get-ModuleOf $rel
    if (-not $tmod) { continue }
    if ($tmod -eq $fmod) { continue }
    if ($tmod -eq "sdk") { continue }   # sdk is the one contract channel

    $hits1 += 1
    Check $false ("{0} includes {1} (resolves to {2})" -f $f, $inc, $rel)
  }
}
Write-Output ("R1 cross-module includes : {0} hit(s)" -f $hits1)

# ---- R2 + R3: test files must be registered and live in <module>/tests/ ----

$tstFiles = @($files | Where-Object { $_ -match '(^|/)tst_[^/]+\.cpp$' })

$cmakeFiles = @(& git -C $repo ls-files "*CMakeLists.txt")
$regs = @{}
foreach ($cm in $cmakeFiles) {
  $text = [System.IO.File]::ReadAllText((Join-Path $repo $cm))
  $cmDir = ($cm -replace '/[^/]*$', '')
  foreach ($m in [regex]::Matches($text, 'toolbox_add_test\(\s*(tst_[A-Za-z0-9_]+)')) {
    $n = $m.Groups[1].Value
    if (-not $regs.ContainsKey($n)) { $regs[$n] = @() }
    $regs[$n] += $cmDir
  }
}

$hits23 = 0
$seen = @{}
foreach ($tf in $tstFiles) {
  $seen[$tf] = $true
  $stem = [System.IO.Path]::GetFileNameWithoutExtension($tf)
  $dir = ($tf -replace '/[^/]*$', '')

  # R3: layout
  if ($dir -notmatch '^(app|sdk|plugins/[^/]+)/tests$') {
    $hits23 += 1
    Check $false ("{0} lives outside <module>/tests/ (R3)" -f $tf)
    continue
  }
  $ownerModule = $dir -replace '/tests$', ''

  # R2: registration, and from the module that owns the file
  if (-not $regs.ContainsKey($stem)) {
    $hits23 += 1
    Check $false ("{0} has no toolbox_add_test() registration -- ctest silently runs one case fewer (R2)" -f $tf)
    continue
  }
  if ($regs[$stem] -notcontains $ownerModule) {
    $hits23 += 1
    Check $false ("{0} is registered from {1}, not from its owning module {2} (R2)" -f $tf, ($regs[$stem] -join ", "), $ownerModule)
  }
}

# Ghost registrations: toolbox_add_test() with no file behind it. The CMake
# configure step already fatals on these, but reporting here keeps the gate
# self-contained and the message actionable.
foreach ($name in ($regs.Keys | Sort-Object)) {
  $hasFile = $false
  foreach ($tf in $tstFiles) {
    if ([System.IO.Path]::GetFileNameWithoutExtension($tf) -eq $name) { $hasFile = $true; break }
  }
  if (-not $hasFile) {
    $hits23 += 1
    Check $false ("toolbox_add_test({0}) has no matching tst_*.cpp anywhere (R2)" -f $name)
  }
}
Write-Output ("R2/R3 test registration : {0} hit(s) over {1} test file(s), {2} registration(s)" -f $hits23, $tstFiles.Count, $regs.Count)

# ---- R4: no foreign module directory on any include path --------------------

$hits4 = 0
foreach ($cm in $cmakeFiles) {
  $cmModule = Get-ModuleOf ($cm -replace '/[^/]*$', '')
  if (-not $cmModule) { continue }   # top-level CMakeLists adds subdirectories, not include paths

  $text = [System.IO.File]::ReadAllText((Join-Path $repo $cm))
  foreach ($line in ($text -split "`n")) {
    if ($line -notmatch 'include_directories') { continue }
    foreach ($m in [regex]::Matches($line, '(?<![!\$])\b(plugins/[A-Za-z0-9_-]+|app|sdk)\b')) {
      if ($m.Groups[1].Value -eq $cmModule) { continue }
      $hits4 += 1
      Check $false ("{0} puts foreign module path '{1}' on an include path (R4)" -f $cm, $m.Groups[1].Value)
    }
  }
}
Write-Output ("R4 foreign include paths : {0} hit(s)" -f $hits4)

# ---- verdict ----------------------------------------------------------------

Write-Output ""
if ($violations -gt 0) {
  Write-Output ("FAIL: {0} module boundary violation(s)" -f $violations)
  Write-Output "Cross-module reuse goes through sdk/ToolBoxPlugin.h or a contract change (docs/architecture.md section 7) -- never through another module's internals."
  exit 1
}
Write-Output "DONE(ALL PASS)"
exit 0
