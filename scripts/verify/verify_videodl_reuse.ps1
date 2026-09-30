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

$exe   = "c:\WorkSpace\ProjectSpace\tools-box\build\bin\Debug\ToolBox.exe"
$shots = "c:\WorkSpace\ProjectSpace\tools-box\build\shots"
$bin   = "c:\WorkSpace\ProjectSpace\tools-box\build\bin\Debug\tools\bin"
$videoDl = -join ([char]0x89C6, [char]0x9891, [char]0x4E0B, [char]0x8F7D)
New-Item -ItemType Directory -Force -Path $shots | Out-Null

function Snapshot($label) {
  Write-Output "== bin $label"
  Get-ChildItem $bin -File -ErrorAction SilentlyContinue | ForEach-Object {
    $h = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.Substring(0, 16)
    Write-Output ("   {0}  {1} bytes  sha256:{2}" -f $_.Name, $_.Length, $h)
  }
}
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

Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600
[void][W]::ReleaseLeft()

Snapshot "before launch"

# 全新启动，不做任何操作，只等页面自己解析内核路径
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

$status = ""
foreach ($t in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Text))) {
  if ($t.Current.Name -like "*yt-dlp*") { $status = $t.Current.Name; break }
}
Write-Output "--- engine status label:"
Write-Output $status

$dlBtn = $null
$fetchBtns = @()
foreach ($b in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Button))) {
  if ($b.Current.Name -eq (-join ([char]0x5F00,[char]0x59CB,[char]0x4E0B,[char]0x8F7D))) { $dlBtn = $b }
  if ($b.Current.Name -like "*yt-dlp*" -or $b.Current.Name -like "*ffmpeg*") {
    $fetchBtns += ("{0}={1}" -f $b.Current.Name, $b.Current.IsEnabled)
  }
}
Write-Output ("start button enabled = {0}" -f $(if ($dlBtn) { $dlBtn.Current.IsEnabled } else { "?" }))
Write-Output ("kernel buttons: {0}" -f ($fetchBtns -join "; "))

# 静置观察，确认没有任何内核下载被触发
Start-Sleep -Seconds 12
Shot $hwnd "reuse_status.png"
Snapshot "after 12s idle (expect identical)"

$part = Get-ChildItem $bin -Filter "*.part" -ErrorAction SilentlyContinue
Write-Output ("leftover .part files = {0}" -f $(if ($part) { ($part.Name -join ",") } else { "none" }))

[void][W]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Seconds 2
Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output "DONE"
