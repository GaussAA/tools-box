# Full verification build for tools-box on Qt 6.12 (Ninja Multi-Config).
# Reconstructs the MSVC environment by hand (the sandboxed PS session does not load
# the user PATH), points at the VS-bundled cmake + ninja, then configures via the
# CMake preset and builds + tests both Debug and Release.
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

# Qt 运行时不会自己回到输出目录：它是 deploy 目标（windeployqt）拷进去的，而 deploy
# 不在默认构建里。少了 Qt6*.dll 与 platforms/，双击 exe 只会立刻退出 ——「程序没反应、
# 界面不出现」最常见的原因就是这个。本脚本开头会清空整棵 build 树（连之前 deploy 好的
# 运行时一起删），所以这里必须说一句，否则下一个想手工点界面的人会一头雾水。
$binDir = "$src\build\bin\Release"
if (-not (Test-Path "$binDir\Qt6Core.dll")) {
  $note = "NOTE: $binDir has no Qt runtime -- before running the app by hand, do:" + `
          "`n      cmake --build $src\build --config Release --target deploy"
  emit $note
  Write-Output $note   # 也上控制台：只写进 bv.log 的话，跑的人根本不会去看
}

emit "=== DONE (ctest Debug=$dbg Release=$rel) ==="
