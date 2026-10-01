# Refresh translations/toolbox_en.ts by running the CMake target `update_translations`
# (Qt 6.12 lupdate now runs on this machine). Reconstructs the MSVC environment by hand
# and puts the Qt bin dir on PATH so lupdate can find the Qt runtime DLLs.
$log = "$PSScriptRoot\lupdate.log"
"" | Set-Content -Encoding UTF8 $log
function emit($t){ if($t -ne $null){ $t.ToString() | Out-File -Append -Encoding UTF8 -FilePath $log } }

$vs      = "C:\Program Files\Microsoft Visual Studio\18\Community"
$msvc    = "$vs\VC\Tools\MSVC\14.51.36231"
$sdkRoot = "C:\Program Files (x86)\Windows Kits\10"
$sdkVer  = "10.0.26100.0"
$cmakeBin= "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$ninjaBin= "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$qtBin   = "C:\Qt6.12\6.12.0\msvc2022_64\bin"
$cmake   = "$cmakeBin\cmake.exe"
$src     = "C:\WorkSpace\ProjectSpace\tools-box"

$clBin   = "$msvc\bin\Hostx64\x64"
$rcBin   = "$sdkRoot\bin\$sdkVer\x64"
$msbuild = "$vs\MSBuild\Current\Bin\amd64"

$env:Path  = "$clBin;$rcBin;$msbuild;$cmakeBin;$ninjaBin;$qtBin;" + $env:Path
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

emit "qt core dll exists: $(Test-Path "$qtBin\Qt6Core.dll")"
emit "lupdate version:"
& "$qtBin\lupdate.exe" -version 2>&1 | ForEach-Object { emit $_ }

Push-Location $src
if (-not (Test-Path "$src\build\CMakeCache.txt")) {
    emit "=== build not configured; configuring via preset ==="
    & $cmake --preset default 2>&1 | ForEach-Object { emit $_ }
}
emit "=== update_translations ==="
& $cmake --build "$src\build" --target update_translations 2>&1 | ForEach-Object { emit $_ }
$code = $LASTEXITCODE
Pop-Location
emit "=== DONE (exit $code) ==="
