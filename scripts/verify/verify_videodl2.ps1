Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
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
    INPUT[] a = new INPUT[1]; a[0].type = 0; a[0].mi.dwFlags = 0x0004;
    return SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
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

$exe    = "c:\WorkSpace\ProjectSpace\tools-box\build\bin\Debug\ToolBox.exe"
$shots  = "c:\WorkSpace\ProjectSpace\tools-box\build\shots"
$plugin = "HKCU:\Software\ToolBox\ToolBox\plugin\media.video-download"
$videoDl = -join ([char]0x89C6, [char]0x9891, [char]0x4E0B, [char]0x8F7D)

function Get-Hwnd {
  $p = Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { return [IntPtr]::Zero }
  $p.Refresh(); return $p.MainWindowHandle
}
function Shot($hwnd, $name) {
  $r = New-Object W+RECT
  [void][W]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc(); [void][W]::PrintWindow($hwnd, $hdc, 2); $g.ReleaseHdc($hdc)
  $bmp.Save((Join-Path $shots $name), [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose(); Write-Output "shot: $name"
}
function Focus($hwnd) {
  [void][W]::SetForegroundWindow($hwnd); Start-Sleep -Milliseconds 400
  return ([W]::GetForegroundWindow() -eq $hwnd)
}
function Find-ByType($hwnd, $type) {
  $ae = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty, $type)
  return $ae.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)
}
function Find-NavItem($hwnd, $pattern) {
  foreach ($i in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::ListItem))) {
    if ($i.Current.Name -like $pattern) { return $i }
  }
  return $null
}
function Click-Elem($hwnd, $elem, $tag) {
  if (-not (Focus $hwnd)) { Write-Output "ABORT: not foreground"; return $false }
  $r = $elem.Current.BoundingRectangle
  [void][W]::ClickAt([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2))
  Start-Sleep -Milliseconds 800
  Write-Output ("clicked {0}" -f $tag)
  return $true
}
function Dump-Plugin($label) {
  Write-Output "== registry $label"
  if (Test-Path $plugin) {
    $k = Get-Item $plugin
    foreach ($n in $k.GetValueNames()) { Write-Output ("   {0} = {1}" -f $n, $k.GetValue($n)) }
  } else { Write-Output "   <no plugin key>" }
}

Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600
[void][W]::ReleaseLeft()

# ---------- 1. 打开页面，检查「开始下载」在缺内核时是禁用的 ----------
[void](Start-Process -FilePath $exe -PassThru)
Start-Sleep -Seconds 3
$hwnd = Get-Hwnd
$nav = Find-NavItem $hwnd "*$videoDl*"
if (-not $nav) { Write-Output "ABORT: nav not found"; exit 1 }
[void](Click-Elem $hwnd $nav "nav 视频下载")
Start-Sleep -Seconds 1

foreach ($b in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Button))) {
  if ($b.Current.Name -eq "开始下载") {
    Write-Output ("download button enabled = {0} (expect False: no yt-dlp)" -f $b.Current.IsEnabled)
  }
  if ($b.Current.Name -eq "取消") {
    Write-Output ("cancel button enabled = {0} (expect False: idle)" -f $b.Current.IsEnabled)
  }
}
Shot $hwnd "v4_idle.png"

# ---------- 2. 展开画质下拉，点最后一项（仅音频 mp3） ----------
$combo = (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::ComboBox) | Select-Object -First 1)
if (-not $combo) { Write-Output "ABORT: combo not found"; exit 1 }
[void](Click-Elem $hwnd $combo "quality combo")

$root = [System.Windows.Automation.AutomationElement]::RootElement
$lcond = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
  [System.Windows.Automation.ControlType]::ListItem)
$popupItem = $null
foreach ($i in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $lcond)) {
  if ($i.Current.Name -like "*mp3*") { $popupItem = $i; break }
}
if ($popupItem) {
  [void](Click-Elem $hwnd $popupItem "popup item (mp3)")
} else {
  Write-Output "popup item NOT FOUND"
}
Shot $hwnd "v5_quality_mp3.png"

# ---------- 3. 关窗保存 ----------
[void][W]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Seconds 2
Dump-Plugin "after close (expect quality = 4)"

# ---------- 4. 重开，确认画质被恢复 ----------
[void](Start-Process -FilePath $exe -PassThru)
Start-Sleep -Seconds 3
$hwnd2 = Get-Hwnd
$nav2 = Find-NavItem $hwnd2 "*$videoDl*"
[void](Click-Elem $hwnd2 $nav2 "nav 视频下载 (restart)")
Start-Sleep -Seconds 1
Shot $hwnd2 "v6_quality_restored.png"
Dump-Plugin "after restart"

[void][W]::PostMessage($hwnd2, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 800
Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output "DONE"
