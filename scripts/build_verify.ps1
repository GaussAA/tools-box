# Full verification build for tools-box on Qt 6.12 (Ninja Multi-Config).
# Reconstructs the MSVC environment by hand (the sandboxed PS session does not load
# the user PATH), points at the VS-bundled cmake + ninja, then configures via the
# CMake preset, builds + tests both Debug and Release, and finally deploys the Qt
# runtime so the output is double-clickable right after a full wipe.
$log = "$PSScriptRoot\bv.log"
"" | Set-Content -Encoding UTF8 $log
function emit($t){ if($t -ne $null){ $t.ToString() | Out-File -Append -Encoding UTF8 -FilePath $log } }

$vs      = "C:\Program Files\Microsoft Visual Studio\18\Community"
$msvc    = "$vs\VC\Tools\MSVC\14.51.36231"
$sdkRoot = "C:\Program Files (x86)\Windows Kits\10"
$sdkVer  = "10.0.26100.0"
$cmakeBin= "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$ninjaBin= "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$cmake   = "$cmakeBin\cmake.exe"
$ctest   = "$cmakeBin\ctest.exe"
$src     = "C:\WorkSpace\ProjectSpace\tools-box"

$clBin   = "$msvc\bin\Hostx64\x64"
$rcBin   = "$sdkRoot\bin\$sdkVer\x64"
$msbuild = "$vs\MSBuild\Current\Bin\amd64"

$env:Path  = "$clBin;$rcBin;$msbuild;$cmakeBin;$ninjaBin;" + $env:Path
$env:INCLUDE = @(
  "$msvc\include",
  "$msvc\ATLMFC\include",
  "$sdkRoot\Include\$sdkVer\ucrt",
  "$sdkRoot\Include\$sdkVer\shared",
  "$sdkRoot\Include\$sdkVer\um",
  "$sdkRoot\Include\$sdkVer\winrt"
) -join ";"
$env:LIB = @(
  "$msvc\lib\x64",
  "$msvc\ATLMFC\lib\x64",
  "$sdkRoot\Lib\$sdkVer\ucrt\x64",
  "$sdkRoot\Lib\$sdkVer\um\x64"
) -join ";"

emit "cmake: $cmake  exists=$(Test-Path $cmake)"
emit "ninja: $ninjaBin\ninja.exe  exists=$(Test-Path "$ninjaBin\ninja.exe")"
& $cmake --version 2>&1 | ForEach-Object { emit $_ }
& "$ninjaBin\ninja.exe" --version 2>&1 | ForEach-Object { emit "ninja $_" }

Remove-Item -Recurse -Force "$src\build" -ErrorAction SilentlyContinue

Push-Location $src
emit "=== configure (preset default) ==="
& $cmake --preset default 2>&1 | ForEach-Object { emit $_ }
if ($LASTEXITCODE -ne 0) { emit "CONFIGURE FAILED ($LASTEXITCODE)"; Pop-Location; exit 2 }

emit "=== build Debug ==="
& $cmake --build "$src\build" --config Debug 2>&1 | ForEach-Object { emit $_ }
if ($LASTEXITCODE -ne 0) { emit "BUILD DEBUG FAILED ($LASTEXITCODE)"; Pop-Location; exit 2 }

emit "=== build Release ==="
& $cmake --build "$src\build" --config Release 2>&1 | ForEach-Object { emit $_ }
if ($LASTEXITCODE -ne 0) { emit "BUILD RELEASE FAILED ($LASTEXITCODE)"; Pop-Location; exit 2 }

# -j：让 CTest 并行跑各测试可执行文件。测试之间是隔离的（各自 exe、各自的临时目录），
# 所以并行安全。本项目的测试一共只要几秒，收益本来就小 —— 之所以还是加上，是因为
# 「串行跑」是没人特意想过的那一侧：并行是默认正确的做法，串行才需要理由。
emit "=== ctest Debug (parallel) ==="
& $ctest --test-dir "$src\build" -C Debug -j --output-on-failure 2>&1 | ForEach-Object { emit $_ }
$dbg = $LASTEXITCODE

emit "=== ctest Release (parallel) ==="
& $ctest --test-dir "$src\build" -C Release -j --output-on-failure 2>&1 | ForEach-Object { emit $_ }
$rel = $LASTEXITCODE

Pop-Location

# The Qt runtime never comes back on its own: it is produced by the optional deploy
# target (windeployqt), not by the default build. This script wipes the whole build
# tree first (the previously deployed runtime included), and an output directory
# without Qt6*.dll / platforms/ makes the exe exit silently on double-click -- the
# most common form of "the program does not react" (error ledger #1). Instead of
# leaving a reminder nobody follows, run the deploy right here so a full verification
# always ends with a double-clickable output.
emit "=== deploy Qt runtime (Release) ==="
& $cmake --build "$src\build" --config Release --target deploy 2>&1 | ForEach-Object { emit $_ }
if ($LASTEXITCODE -ne 0) {
  # windeployqt queries its qtpaths helper through a pipe; restricted environments
  # (the development sandbox this script is often run from) block that and windeployqt
  # dies with "Unable to query qtpaths: pipe:". That is an environment limitation, not
  # a wrong deploy recipe. Fall back to copying the same runtime set by hand, derived
  # from the CMake cache instead of a hardcoded Qt path.
  emit "deploy target failed ($LASTEXITCODE) -- falling back to manual runtime copy"
  $cacheLine = Select-String -Path "$src\build\CMakeCache.txt" -Pattern "^Qt6_DIR:PATH=(.+)$" |
               Select-Object -First 1
  if (-not $cacheLine) { emit "FAIL: Qt6_DIR not found in CMakeCache.txt"; exit 3 }
  $qtPrefix = $cacheLine.Matches[0].Groups[1].Value.Trim() -replace "/lib/cmake/Qt6/?$", ""
  $qtBin = Join-Path $qtPrefix "bin"
  $binDir = "$src\build\bin\Release"

  # Core DLLs (Release build, no "d" suffix), same list windeployqt would deliver
  # for this project's dependencies (Widgets + Network + Svg).
  foreach ($dll in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll",
                     "Qt6Network.dll", "Qt6Svg.dll")) {
    Copy-Item (Join-Path $qtBin $dll) $binDir -Force
  }
  # Qt plugins must keep their subdirectory layout or Qt won't find them. Note the
  # source base is <prefix>\plugins, NOT <prefix>\bin -- the bin directory holds only
  # the top-level DLLs.
  $qtPlugins = Join-Path $qtPrefix "plugins"
  foreach ($p in @(@("platforms", "qwindows.dll"),
                   @("styles", "qmodernwindowsstyle.dll"),
                   @("tls", "qschannelbackend.dll"),
                   @("imageformats", "qico.dll"),
                   @("imageformats", "qsvg.dll"))) {
    New-Item -ItemType Directory -Force -Path (Join-Path $binDir $p[0]) | Out-Null
    Copy-Item (Join-Path $qtPlugins ($p[0] + "\" + $p[1])) (Join-Path $binDir $p[0]) -Force
  }
  # Qt's own Chinese widget strings: qtbase_<locale>.qm since Qt 6 (deviation 9.11 --
  # the old qt_ prefix is a 99-byte empty shell). Loaded from the exe-side
  # translations/ directory, which is main.cpp's first search location.
  $trDir = Join-Path $binDir "translations"
  New-Item -ItemType Directory -Force -Path $trDir | Out-Null
  Copy-Item (Join-Path $qtPrefix "translations\qtbase_zh_CN.qm") $trDir -Force
  # App-local VC runtime (the deploy target does the same via POST_BUILD step).
  Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT\*.dll" -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Item $_.FullName $binDir -Force }
  emit "manual runtime copy done (from $qtPrefix)"
}

# Qt6Core.dll must exist now no matter which path delivered it. A missing runtime
# after this point is a FAIL, not a note: the script promised this artifact, and
# quietly shipping without it is exactly the "fake green" shape. The check covers the
# three things the app cannot start (or start localized) without: a core DLL, the
# window platform plugin, and the Qt-base Chinese translation (deviation 9.11).
$binDir = "$src\build\bin\Release"
foreach ($must in @("Qt6Core.dll", "platforms\qwindows.dll", "translations\qtbase_zh_CN.qm")) {
  if (-not (Test-Path (Join-Path $binDir $must))) {
    emit "FAIL: $binDir has no $must after deploy -- deploy did not deliver."
    Write-Output "FAIL: $must missing in $binDir after deploy (see bv.log)"
    exit 3
  }
}

emit "=== DONE (ctest Debug=$dbg Release=$rel) ==="
# ctest exit codes used to be logged but never surfaced in the script exit code --
# red tests and the script still ended 0.
if ($dbg -ne 0 -or $rel -ne 0) { exit 1 }
exit 0
