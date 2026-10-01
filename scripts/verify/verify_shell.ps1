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
# NOTE: keep every literal at the PowerShell level ASCII. PowerShell 5.1 parses a
# BOM-less .ps1 as ANSI, so Chinese in comments/strings corrupts the token stream.
# Chinese text needed for assertions is rebuilt from code points via Chars.

param(
  [string]$Exe = "c:\WorkSpace\ProjectSpace\tools-box\build\bin\Debug\ToolBox.exe"
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
$tools        = @($toolA, $toolB, $toolC)

$exe   = $Exe
$shots = "c:\WorkSpace\ProjectSpace\tools-box\build\shots"
New-Item -ItemType Directory -Force -Path $shots | Out-Null
if (-not (Test-Path $exe)) { Write-Output "FAIL: exe not found at $exe"; exit 1 }
Write-Output "exe       = $exe"
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
      # Version comes from the top-level project(... VERSION ...) via TOOLBOX_VERSION.
      Check (($dtexts -join " ") -match "0\.1\.0") "about dialog shows app version 0.1.0"

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

if ($failed.Count -gt 0) {
  Write-Output ("DONE(FAILED {0}): {1}" -f $failed.Count, ($failed -join "; "))
  exit 1
}
Write-Output "DONE(ALL PASS)"
