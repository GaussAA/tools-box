# Shell smoke test: plugin load count, version single-source, Qt dialog translation.
# Covers docs/architecture.md ledger items 9.5 / 9.6 and "the shell loads every plugin".
# Touches no plugin settings; it does switch the selected tool, so ui/lastToolId ends up
# pointing at the home page when the window closes.
#
# -Exe points the test at another build of the shell. Use it to check a packaged
# output (unzip the release zip somewhere, then pass the extracted ToolBox.exe):
# that is the only way to prove the package really carries the plugins, the Qt
# runtime and translations/.
#
# NOTE: this file must keep its UTF-8 BOM. Windows PowerShell 5.1 reads a BOM-less
# script with the system ANSI codepage (936 / GB2312 here), which mangles UTF-8 Chinese
# comments badly enough to break parsing. verify_whitespace.ps1 enforces the BOM.
# The Chinese strings asserted below are still rebuilt from code points via Chars,
# because the script has to stay readable under whatever codepage the host picks.

param(
  [string]$Exe = "c:\WorkSpace\ProjectSpace\tools-box\build\bin\Debug\ToolBox.exe",
  # Expected UI language. The caller is responsible for making the app actually run in
  # that language (ui/language in the registry, empty = follow the system).
  [ValidateSet("zh", "en")][string]$Lang = "zh"
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy, mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public MOUSEINPUT mi; }

  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] inputs, int size);

  public static uint ReleaseLeft() {
    INPUT[] a = new INPUT[1];
    a[0].type = 0;
    a[0].mi.dwFlags = 0x0004;
    return SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }

  // Normalized MOUSEEVENTF_ABSOLUTE gets remapped on this machine and lands in the
  // wrong spot, so place the cursor with SetCursorPos and inject a zero-delta down/up.
  public static uint ClickAt(int x, int y) {
    SetCursorPos(x, y);
    System.Threading.Thread.Sleep(150);
    INPUT[] a = new INPUT[2];
    a[0].type = 0; a[0].mi.dwFlags = 0x0002;
    a[1].type = 0; a[1].mi.dwFlags = 0x0004;
    uint rc = SendInput(2, a, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(150);
    return rc;
  }
}
"@

$ErrorActionPreference = "Continue"
[void][W]::SetProcessDPIAware()

function Chars([int[]]$cp) { return -join ($cp | ForEach-Object { [char]$_ }) }

# Expected strings depend on the UI language. Chinese is the source language (what you
# get with no translation file installed); English comes from translations/toolbox_en.ts.
# The language is chosen at startup from ui/language (empty = follow the system), so the
# caller sets that registry value before running with -Lang en.
# Chinese is rebuilt from code points so this script stays readable under any codepage;
# the English set is plain ASCII.
if ($Lang -eq "en") {
  $titleBox      = "Toolbox"
  $menuHelp      = "Help"
  $menuAbout     = "About"
  $dlgAbout      = "About Toolbox"
  $btnOk         = "OK"
  $loadedPrefix  = "tools loaded"
  $loadErrorMark = "These plugins failed to load:"
  $toolA         = "Base64 Encode / Decode"
  $toolB         = "JSON Formatter"
  $toolC         = "Video Downloader"
} else {
  $titleBox     = Chars @(0x5DE5, 0x5177, 0x7BB1)                  # gong ju xiang
  $menuHelp     = Chars @(0x5E2E, 0x52A9)                          # bang zhu
  $menuAbout    = Chars @(0x5173, 0x4E8E)                          # guan yu
  $dlgAbout     = Chars @(0x5173, 0x4E8E, 0x5DE5, 0x5177, 0x7BB1)  # guan yu gong ju xiang
  $btnOk        = Chars @(0x786E, 0x5B9A)                          # que ding
  $loadedPrefix = Chars @(0x5DF2, 0x52A0, 0x8F7D)                  # yi jia zai
  # "the following plugins failed to load:" - shown on the home page only on failure
  $loadErrorMark = Chars @(0x4EE5, 0x4E0B, 0x63D2, 0x4EF6, 0x52A0, 0x8F7D, 0x5931, 0x8D25)
  $toolA        = "Base64 " + (Chars @(0x7F16, 0x89E3, 0x7801))
  $toolB        = "JSON "   + (Chars @(0x683C, 0x5F0F, 0x5316))
  $toolC        = Chars @(0x89C6, 0x9891, 0x4E0B, 0x8F7D)
}
$tools        = @($toolA, $toolB, $toolC)

$exe   = $Exe
$shots = "c:\WorkSpace\ProjectSpace\tools-box\build\shots"
New-Item -ItemType Directory -Force -Path $shots | Out-Null
if (-not (Test-Path $exe)) { Write-Output "FAIL: exe not found at $exe"; exit 1 }
Write-Output "exe       = $exe"

# ---------- the script owns the UI language ----------
# This used to be the caller's job ("the caller is responsible for..." in the param
# block). In practice that meant: run it, get a pile of FAILs about text that does not
# match, and have to work out that the app simply came up in the other language.
#
# It is worse than a missing step, because on Windows the language is not only read from
# ui/language: a LANG / LC_ALL environment variable in the launching shell flips a Qt app
# to English even when the system language is Chinese. Measured here: with
# LANG=en_US.UTF-8 in the environment and ui/language unset, this app came up fully
# English (nav "Home | Developer tools | JSON Formatter | ...") while
# GetUserDefaultUILanguage() reported zh-CN. So "just follow the system" is not a stable
# premise for a test -- the check forces what it needs and restores it afterwards.
$langKey = "HKCU:\Software\ToolBox\ToolBox\ui"
$langExisted = $false
$langPrevious = $null
if (Test-Path $langKey) {
  $currentLang = Get-ItemProperty -Path $langKey -Name language -ErrorAction SilentlyContinue
  if ($null -ne $currentLang) { $langExisted = $true; $langPrevious = $currentLang.language }
} else {
  New-Item -Path $langKey -Force | Out-Null
}
# Write a full locale, not just the language code: Qt's own translations are named by
# region (qtbase_zh_CN.qm -- there is no qtbase_zh.qm), so a bare "zh" makes the app
# install a translator that translates nothing and the standard dialog buttons stay
# English. The app now defends against that too (see app/core/LanguageChoice.cpp), but
# a test should exercise the realistic configuration rather than rely on the guard.
$registryLang = if ($Lang -eq "en") { "en_US" } else { "zh_CN" }
Set-ItemProperty -Path $langKey -Name language -Value $registryLang
Write-Output ("ui/language = '{0}' (forced for this run)" -f $registryLang)

# The expected app version is read from its single source of truth, the top-level
# project(... VERSION ...), rather than spelled out here. A literal would be a second
# copy to bump on every release - the duplication ledger 9.5 set out to remove - and
# this check is supposed to prove the version really is single-sourced.
$expectedVersion = $null
$cmakeLists = (Resolve-Path (Join-Path $PSScriptRoot "..\..\CMakeLists.txt")).Path
# Read the whole file: project(ToolBox) and its VERSION sit on separate lines, so a
# line-based match would never hit. \s in .NET regex spans newlines, so this works.
$raw = Get-Content $cmakeLists -Raw
if ($raw -match 'project\(\s*ToolBox\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
  $expectedVersion = $Matches[1]
}
Write-Output ("version   = " + $(if ($expectedVersion) { $expectedVersion } else { "<not found in CMakeLists.txt>" }))
# The shell finds plugins next to itself, so a packaged copy is checked in place.
Write-Output ("tools/    = " + ((Get-ChildItem (Join-Path (Split-Path $exe -Parent) "tools") -Filter *.dll -ErrorAction SilentlyContinue | Measure-Object).Count) + " dll(s)")

$failed = @()
function Check($ok, $what) {
  if ($ok) { Write-Output "PASS: $what" }
  else     { Write-Output "FAIL: $what"; $script:failed += $what }
}

function Get-AllText($root) {
  $names = @()
  foreach ($t in @([System.Windows.Automation.ControlType]::Text,
                   [System.Windows.Automation.ControlType]::Edit)) {
    $cond = New-Object System.Windows.Automation.PropertyCondition(
      [System.Windows.Automation.AutomationElement]::ControlTypeProperty, $t)
    foreach ($e in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)) {
      $names += $e.Current.Name
    }
  }
  return $names
}

function Find-ByControlType($root, $type, $scope) {
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty, $type)
  return $root.FindAll($scope, $cond)
}

function Find-MenuItem($root, $name) {
  $items = Find-ByControlType $root ([System.Windows.Automation.ControlType]::MenuItem) ([System.Windows.Automation.TreeScope]::Descendants)
  foreach ($e in $items) {
    if ($e.Current.Name -eq $name) { return $e }
  }
  return $null
}

function Click-Element($e) {
  $r = $e.Current.BoundingRectangle
  [void][W]::ClickAt([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2))
}

function Shot($hwnd, $name) {
  $r = New-Object W+RECT
  [void][W]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][W]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save((Join-Path $shots $name), [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  Write-Output "shot: $name ($w x $h)"
}

# ---------- clean slate ----------
Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600
[void][W]::ReleaseLeft()

# ---------- 1. launch ----------
[void](Start-Process -FilePath $exe -PassThru)
Start-Sleep -Seconds 3
$proc = Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Select-Object -First 1
Check ($null -ne $proc) "process alive, did not crash on startup"
if (-not $proc) { Write-Output "DONE(FAILED)"; exit 1 }
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
[void][W]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 500
Shot $hwnd "shell_home.png"

$ae = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
Check ($ae.Current.Name -eq $titleBox) "window title is the toolbox title (got '$($ae.Current.Name)')"

# ---------- 2. every plugin loaded ----------
# The shell restores the last used tool, so switch to the home page first: its text is
# the only place the shell states how many plugins it loaded.
$menuHome = Chars @(0x9996, 0x9875)                              # shou ye
$navItems = Find-ByControlType $ae ([System.Windows.Automation.ControlType]::ListItem) ([System.Windows.Automation.TreeScope]::Descendants)
$navNames = @()
$homeItem = $null
foreach ($e in $navItems) {
  $navNames += $e.Current.Name
  if ($e.Current.Name -eq $menuHome) { $homeItem = $e }
}
if ($homeItem) { Click-Element $homeItem }
Start-Sleep -Seconds 1
Write-Output ("   nav items: " + ($navNames -join " | "))
foreach ($t in $tools) { Check ($navNames -contains $t) "nav contains '$t'" }

$texts = @(Get-AllText $ae)
Write-Output ("   home texts: " + ($texts -join " / "))
$loaded = $texts | Where-Object { $_ -like "*$loadedPrefix*" } | Select-Object -First 1
Check ($null -ne $loaded -and $loaded -match "3") "home page reports 3 loaded tools"
$loadError = $texts | Where-Object { $_ -like "*$loadErrorMark*" } | Select-Object -First 1
Check ($null -eq $loadError) "home page shows no plugin load errors"

# ---------- 3. Help -> About: version single source + Qt dialog translation ----------
$root = [System.Windows.Automation.AutomationElement]::RootElement
$help = Find-MenuItem $ae $menuHelp
Check ($null -ne $help) "help menu found"
if ($help) {
  Click-Element $help
  Start-Sleep -Milliseconds 800
  $about = Find-MenuItem $root $menuAbout
  Check ($null -ne $about) "about menu item found"
  if ($about) {
    Click-Element $about
    Start-Sleep -Seconds 2

    $dlg = $null
    $windows = Find-ByControlType $root ([System.Windows.Automation.ControlType]::Window) ([System.Windows.Automation.TreeScope]::Descendants)
    $windowNames = @()
    foreach ($w in $windows) {
      $windowNames += $w.Current.Name
      if ($w.Current.Name -eq $dlgAbout) { $dlg = $w }
    }
    Write-Output ("   top-level windows: " + (($windowNames | Where-Object { $_ }) -join " | "))
    Check ($null -ne $dlg) "about dialog appeared"

    if ($dlg) {
      Shot ([IntPtr]$dlg.Current.NativeWindowHandle) "shell_about.png"
      $dtexts = @(Get-AllText $dlg)
      Write-Output ("   dialog text: " + ($dtexts -join " / "))
      # Compared against the value read from CMakeLists.txt above, so this also proves
      # TOOLBOX_VERSION really is wired through from project(... VERSION ...).
      Check ($null -ne $expectedVersion) "found the app version in CMakeLists.txt"
      if ($expectedVersion) {
        Check (($dtexts -join " ") -match [regex]::Escape($expectedVersion)) `
              "about dialog shows app version $expectedVersion"
      }

      $buttons = @()
      $buttonEls = Find-ByControlType $dlg ([System.Windows.Automation.ControlType]::Button) ([System.Windows.Automation.TreeScope]::Descendants)
      foreach ($b in $buttonEls) { $buttons += $b.Current.Name }
      Write-Output ("   buttons: " + ($buttons -join " | "))
      # QMessageBox::about's standard button text comes from the Qt translation file;
      # without it the label stays "OK".
      Check ($buttons -contains $btnOk) "dialog button is localized (translation active)"
      if ($buttons -notcontains $btnOk) {
        Write-Output "   hint: 'OK' means QTranslator was not installed or translations/ is missing"
      }
      $ok = $null
      foreach ($b in $buttonEls) {
        if ($b.Current.Name -eq $btnOk) { $ok = $b; break }
      }
      if ($ok) { Click-Element $ok; Start-Sleep -Milliseconds 800 }
    }
  }
}

# ---------- 4. teardown ----------
if ($hwnd -ne [IntPtr]::Zero) { [void][W]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
Start-Sleep -Seconds 1
Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force

# Put ui/language back the way we found it: this run's choice must not become the
# user's next startup language.
if ($langExisted) {
  Set-ItemProperty -Path $langKey -Name language -Value $langPrevious
} else {
  Remove-ItemProperty -Path $langKey -Name language -ErrorAction SilentlyContinue
}

if ($failed.Count -gt 0) {
  Write-Output ("DONE(FAILED {0}): {1}" -f $failed.Count, ($failed -join "; "))
  exit 1
}
Write-Output "DONE(ALL PASS)"
