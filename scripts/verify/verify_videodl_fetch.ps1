param([string]$Which = "yt-dlp", [int]$TimeoutSec = 240)

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

$bin    = "c:\WorkSpace\ProjectSpace\tools-box\build\bin\Debug"
$exe    = Join-Path $bin "ToolBox.exe"
$engDir = Join-Path $bin "tools\bin"
$shots  = "c:\WorkSpace\ProjectSpace\tools-box\build\shots"
$videoDl = -join ([char]0x89C6, [char]0x9891, [char]0x4E0B, [char]0x8F7D)
$startDl = -join ([char]0x5F00, [char]0x59CB, [char]0x4E0B, [char]0x8F7D)
New-Item -ItemType Directory -Force -Path $shots | Out-Null

$target = Join-Path $engDir "$Which.exe"

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
function Click-Elem($hwnd, $elem, $tag) {
  if (-not (Focus $hwnd)) { Write-Output "ABORT: not foreground"; return $false }
  $r = $elem.Current.BoundingRectangle
  [void][W]::ClickAt([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2))
  Start-Sleep -Milliseconds 900
  Write-Output ("clicked {0}" -f $tag)
  return $true
}
function PageText($hwnd) {
  foreach ($e in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Text))) {
    if ($e.Current.Name -ne "") { Write-Output ("   text: {0}" -f $e.Current.Name) }
  }
}

Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600
[void][W]::ReleaseLeft()

Write-Output ("=== target = {0}" -f $target)
Write-Output ("exists before = {0}" -f (Test-Path $target))

[void](Start-Process -FilePath $exe -PassThru)
Start-Sleep -Seconds 3
$hwnd = Get-Hwnd

$nav = $null
foreach ($i in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::ListItem))) {
  if ($i.Current.Name -like "*$videoDl*") { $nav = $i; break }
}
if (-not $nav) { Write-Output "ABORT: nav not found"; exit 1 }
[void](Click-Elem $hwnd $nav "nav")
Start-Sleep -Seconds 1

# 两个「下载」按钮里挑对的那一个：
#   yt-dlp -> 名字带 '/'（「下载 / 更新 yt-dlp」）
#   ffmpeg -> 名字不含省略号（「手动指定 ffmpeg…」才是带省略号的那个）
$btn = $null
foreach ($b in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Button))) {
  $n = $b.Current.Name
  if ($Which -eq "yt-dlp" -and $n -like "*yt-dlp*" -and $n -like "*/*") { $btn = $b; break }
  if ($Which -eq "ffmpeg" -and $n -like "*ffmpeg*" -and -not $n.Contains([char]0x2026)) { $btn = $b; break }
}
if (-not $btn) { Write-Output "ABORT: fetch button not found"; exit 1 }
[void](Click-Elem $hwnd $btn ("fetch " + $Which))

$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $TimeoutSec -and -not (Test-Path $target)) {
  Start-Sleep -Seconds 3
}
$elapsed = [int]$sw.Elapsed.TotalSeconds
Write-Output ("waited {0}s" -f $elapsed)

if (Test-Path $target) {
  $f = Get-Item $target
  Write-Output ("OK: {0}  size={1:N0} bytes  mtime={2}" -f $f.Name, $f.Length, $f.LastWriteTime)
} else {
  Write-Output "TIMEOUT: target still missing"
  $part = Join-Path $engDir "$Which.exe.part"
  if (Test-Path $part) { Write-Output ("   partial: {0:N0} bytes" -f (Get-Item $part).Length) }
}
Start-Sleep -Seconds 2
Shot $hwnd ("fetch_{0}.png" -f $Which)
Write-Output "-- page text"
PageText $hwnd

foreach ($b in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Button))) {
  if ($b.Current.Name -eq $startDl) {
    Write-Output ("start-download enabled = {0} (expect True now)" -f $b.Current.IsEnabled)
  }
}

[void][W]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Seconds 2
Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output "DONE"
