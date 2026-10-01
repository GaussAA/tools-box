$log = "$env:TEMP\bv.log"
"" | Set-Content -Encoding UTF8 $log
function emit($t){ if($t -ne $null){ $t.ToString() | Out-File -Append -Encoding UTF8 -FilePath $log } }

$vs      = "C:\Program Files\Microsoft Visual Studio\18\Community"
$msvc    = "$vs\VC\Tools\MSVC\14.51.36231"
$sdkRoot = "C:\Program Files (x86)\Windows Kits\10"
$sdkVer  = "10.0.26100.0"
$cmake   = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$src     = "C:\WorkSpace\ProjectSpace\tools-box"

$clBin   = "$msvc\bin\Hostx64\x64"
$rcBin   = "$sdkRoot\bin\$sdkVer\x64"
$msbuild = "$vs\MSBuild\Current\Bin\amd64"
$clExe   = "$clBin\cl.exe"

$env:Path  = "$clBin;$rcBin;$msbuild;" + $env:Path
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

emit "cl exists: $(Test-Path $clExe)"
emit "=== configure (NMake + explicit cl) ==="
Remove-Item -Recurse -Force "$src\build" -ErrorAction SilentlyContinue
& $cmake -S $src -B "$src\build" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=$clExe" "-DCMAKE_PREFIX_PATH=C:/Qt6.12/6.12.0/msvc2022_64" 2>&1 | ForEach-Object { emit $_ }
if ($LASTEXITCODE -ne 0) { emit "CONFIGURE FAILED ($LASTEXITCODE)"; exit }

emit "=== build ==="
& $cmake --build "$src\build" 2>&1 | ForEach-Object { emit $_ }
if ($LASTEXITCODE -ne 0) { emit "BUILD FAILED ($LASTEXITCODE)"; exit }

emit "=== ctest ==="
& $cmake --build "$src\build" --target test 2>&1 | ForEach-Object { emit $_ }
emit "=== DONE (ctest exit $LASTEXITCODE) ==="
