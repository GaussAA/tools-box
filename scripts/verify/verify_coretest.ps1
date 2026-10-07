# Enforces docs/workflow.md section 5 at two levels.
#
# Level 1 (file): every source under */core/ -- plus the orchestration layer named
# by a <name>_orch static library, see docs/architecture.md section 3.1 -- must be
# referenced by a Qt Test case. That rule was until now only a sentence in a
# document, so a new core module could be added with no test and every check would
# stay green.
#
# Level 2 (function): every exported core function must be linked against a test
# binary. The file-level rule answers "is the module touched by a test at all";
# this one answers the sharper question "is each of its functions". The common
# shape of a real coverage hole is a header that got included while one of its
# functions never got exercised, and that is invisible to the file-level rule.
#
# Why the linker and not a parser
# -------------------------------
# The obvious implementation is to parse the headers for declared names. That was
# built and abandoned on 2026-10-07 after seven consecutive silent-failure bugs,
# each of which made the check pass while examining almost nothing: PowerShell 5.1
# splits a multi-line string returned from a function into one element per line;
# -match is case-insensitive by default so [A-Z_]{3,} also matched "decodeOutput";
# a List[string] coerces a hashtable to its literal string form; ArrayList.Add
# returns an index that then surfaces as a "function" named 11; a forward
# declaration "class QDirIterator;" opened a type body that closed again on the
# next line, so the real class body was scanned as namespace scope and every
# private slot became a false positive. Every one of those is a "fake green" --
# the exact shape this project keeps building gates to prevent -- and a gate that
# cries wolf a dozen times gets ignored, which is worse than no gate at all.
#
# So the compiler does the parsing. For each *_core static library we read its
# symbol table (llvm-nm --defined-only --extern-only); the module's own functions
# appear as ?name@namespace@@..., recognisable without demangling, while template
# instantiations pulled in from the standard library do not carry that shape. Then
# we ask the test object files which of those names they reference
# (llvm-nm --undefined-only -- an undefined symbol in a test TU is a function that
# test calls). "A core function nobody calls" is then a fact reported by the
# toolchain rather than a guess made by a regex.
#
# Two mechanical filters keep the noise out:
#   - the name must also appear before a "(" in some core header, which drops the
#     std/Qt template instantiations (begin, allocate, that, ...);
#   - only exported text symbols are considered, so internal helpers and Qt's own
#     symbols never enter the candidate set. Private members of a class are not
#     visible here at all, which is correct: a private slot is dispatched by the
#     meta-object system and can never be called by name.
#
# What this does NOT claim: that a referenced function is meaningfully exercised. A
# name in an object file proves the test links against it; whether the assertions
# pin its behaviour stays a review question. What it does enforce is that no
# exported core function is completely untested.
#
# Why not line coverage: real coverage needs a tool this project does not have,
# and the obvious route is closed. llvm-cov ships with Visual Studio, but MSVC's
# /FUCOVERAGE emits the legacy binary .cov format while llvm-cov only reads
# clang's .profraw/.profdata -- verified by experiment on 2026-10-07 (four flag
# combinations, none produced profile data). The remaining routes are a
# third-party tool in CI (OpenCppCoverage) or switching the toolset to clang-cl,
# which would put a new toolchain on every machine. See
# .workbuddy/g3-coverage-feasibility-2026-10-07.md.
#
# Requirements: the project must be built for Debug, because the test object files
# are the input. When they are missing this check reports FAIL rather than
# skipping -- a gate that quietly passes for want of input is the fake-green shape
# again.
#
# NOTE: this file must keep its UTF-8 BOM (see verify_whitespace.ps1).
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\verify\verify_coretest.ps1
# Exit code 0 = clean, 1 = violations found.

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo "build"

function Find-Tool([string] $name) {
  $cmd = Get-Command $name -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  # Not a recursive scan: Get-ChildItem -Recurse over a Visual Studio install is
  # slow and unreliable (it silently comes back empty on this machine), and the
  # layout here is fixed anyway -- the LLVM tools ship under VC\Tools\Llvm\<arch>\bin.
  $candidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin",
    "C:\Program Files\LLVM\bin"
  )
  foreach ($dir in $candidates) {
    $p = Join-Path $dir $name
    if (Test-Path $p -PathType Leaf) { return $p }
  }
  return $null
}

$git = (Get-Command git -ErrorAction SilentlyContinue)
if (-not $git) { Write-Output "FAIL: git not found on PATH"; exit 1 }

$nm = Find-Tool "llvm-nm.exe"
if (-not $nm) {
  Write-Output "FAIL: llvm-nm.exe not found. It ships with Visual Studio, and without it"
  Write-Output "       the linker cannot tell us which core functions the tests call."
  exit 1
}

# ---------- level 1: file-level ----------
$coreSources = @(& git -C $repo ls-files "app/core/*.cpp" "plugins/*/core/*.cpp") `
             + @(& git -C $repo ls-files --others --exclude-standard "app/core/*.cpp" "plugins/*/core/*.cpp")
$coreSources = @($coreSources | Sort-Object -Unique)

$orchSources = @()
$repoFull = [System.IO.Path]::GetFullPath($repo)
$cmakeFiles = @(& git -C $repo ls-files | Where-Object { $_ -eq "CMakeLists.txt" -or $_ -like "*/CMakeLists.txt" })
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

$testSources = @(& git -C $repo ls-files "tests/*.cpp") `
             + @(& git -C $repo ls-files --others --exclude-standard "tests/*.cpp")

# Read as UTF-8 through .NET on purpose: PowerShell 5.1's Get-Content decodes
# BOM-less UTF-8 with the system ANSI codepage, which mangles the Chinese
# comments these files are full of (the same trap that once made verify_filesize
# count a file 88 lines short).
$testText = ""
foreach ($rel in $testSources) {
  $path = Join-Path $repo $rel
  if (Test-Path $path -PathType Leaf) { $testText += [System.IO.File]::ReadAllText($path) + "`n" }
}

Write-Output ("repo        = " + $repo)
Write-Output ("llvm-nm     = " + $nm)
Write-Output ("core files  = " + $coreSources.Count)
Write-Output ("orch files  = " + $orchSources.Count)
Write-Output ("test files  = " + $testSources.Count)

$missingFiles = @()
foreach ($group in @(@{ L = "core"; S = $coreSources }, @{ L = "orch"; S = $orchSources })) {
  foreach ($rel in $group.S) {
    if (-not (Test-Path (Join-Path $repo $rel) -PathType Leaf)) { continue }
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($rel)
    if ($testText -match ("(core/)?$([regex]::Escape($stem))\.h")) {
      Write-Output ("OK:     {0} <- {1}.h [{2}]" -f $rel, $stem, $group.L)
    } else {
      $missingFiles += $rel
      Write-Output ("MISSING:{0} (no test includes {1}.h) [{2}]" -f $rel, $stem, $group.L)
    }
  }
}

# ---------- level 2: function-level, told by the toolchain ----------
# Headers of core/ plus the orchestration interfaces (which live outside core/
# because they carry tr() strings, see docs/architecture.md section 3.1).
$headerFiles = @(Get-ChildItem (Join-Path $repo "app\core") -Filter *.h -ErrorAction SilentlyContinue)
$headerFiles += @(Get-ChildItem (Join-Path $repo "plugins") -Recurse -Filter *.h -ErrorAction SilentlyContinue |
                  Where-Object { $_.FullName -cmatch '\\core\\' -or $_.Directory.Name -eq "videodl" })
$headerText = ($headerFiles | ForEach-Object { [System.IO.File]::ReadAllText($_.FullName) }) -join "`n"

$libs = @(Get-ChildItem $build -Recurse -Filter "*_core.lib" -ErrorAction SilentlyContinue |
          Where-Object { $_.FullName -cmatch '\\Debug\\' })
if ($libs.Count -eq 0) {
  Write-Output ""
  Write-Output "FAIL: no Debug *_core.lib under build\ -- build Debug before running this"
  Write-Output "       check (scripts\build_verify.ps1 does it); skipping would be a fake green."
  exit 1
}

$candidates = @{}
foreach ($lib in $libs) {
  $raw = & $nm --defined-only --extern-only $lib.FullName 2>&1
  $names = $raw | Where-Object { $_ -cmatch '^[0-9A-Fa-f]+\s+[TW]\s+\?(\w+)@' } |
           ForEach-Object { [regex]::Match($_, '^[0-9A-Fa-f]+\s+[TW]\s+\?(\w+)@').Groups[1].Value } |
           Sort-Object -Unique
  # Keep only names a header actually *declares*. Two conditions, both needed:
  #   - the name is followed by "(" -- drops unrelated short words;
  #   - and something that looks like a return type precedes it on the line --
  #     without this, a name mentioned only in a doc comment ("... then load()")
  #     would pass, and QList::load came in exactly that way. Requiring a type
  #     before the name means the match is a declaration, not prose.
  $own = @($names | Where-Object {
    $n = [regex]::Escape($_)
    $headerText -cmatch ("(?m)^[A-Za-z_][A-Za-z0-9_:<>,\s\*&]{0,60}?\b" + $n + "\s*\(")
  })
  Write-Output ("lib       = {0}: {1} exported, {2} declared in a header" -f $lib.Name, $names.Count, $own.Count)
  foreach ($n in $own) { $candidates[$n] = $lib.BaseName }
}

# Test object files, Debug only. Scanning every configuration would count each
# test twice and, worse, let a Release object stand in for a missing Debug one --
# which makes the check pass on a tree that was never really built for this
# configuration. CI builds Release only, so Debug is the configuration that
# actually exists everywhere; the debug-level script (build_verify.ps1) builds both.
$testObjs = @(Get-ChildItem (Join-Path $build "tests") -Recurse -Filter "tst_*.cpp.obj" -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -cmatch '\\Debug\\' })
if ($testObjs.Count -eq 0) {
  Write-Output ""
  Write-Output "FAIL: no tests\*.cpp.obj under build\ -- build Debug first (scripts\build_verify.ps1)."
  exit 1
}
$called = New-Object System.Collections.Generic.HashSet[string]
foreach ($obj in $testObjs) {
  $raw = & $nm --undefined-only $obj.FullName 2>&1
  foreach ($m in [regex]::Matches(($raw -join "`n"), '\?(\w+)@')) { [void]$called.Add($m.Groups[1].Value) }
}
Write-Output ("test objs = " + $testObjs.Count + " referencing " + $called.Count + " name(s)")

$untested = @()
foreach ($name in ($candidates.Keys | Sort-Object)) {
  if ($called.Contains($name)) {
    Write-Output ("  fn OK   {0}::{1}()" -f $candidates[$name], $name)
  } else {
    $untested += ("{0}::{1}" -f $candidates[$name], $name)
    Write-Output ("  fn MISS {0}::{1}() -- no test links against it" -f $candidates[$name], $name)
  }
}
Write-Output ""
Write-Output ("checked {0} exported core function(s)" -f $candidates.Count)

if ($missingFiles.Count -gt 0 -or $untested.Count -gt 0) {
  if ($missingFiles.Count -gt 0) {
    Write-Output ("FAIL: {0} core/orch file(s) have no test case" -f $missingFiles.Count)
    Write-Output "Add tests/tst_<module>.cpp, register it with toolbox_add_test(), and update the tables in docs/workflow.md section 5."
  }
  if ($untested.Count -gt 0) {
    Write-Output ("FAIL: {0} exported core function(s) are not referenced by any test binary:" -f $untested.Count)
    $untested | ForEach-Object { Write-Output ("       " + $_) }
    Write-Output "Either the function is dead code (delete it), or it is a real coverage hole (add a case that calls it)."
  }
  exit 1
}

Write-Output ("PASS: every core/ and orch source is covered by a test ({0} + {1} file(s)), and all {2} exported function(s) are linked against a test" -f $coreSources.Count, $orchSources.Count, $candidates.Count)
exit 0
