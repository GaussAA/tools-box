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
$dlDir = "c:\WorkSpace\ProjectSpace\tools-box\build\vdl-test"
$url   = "https://www.bilibili.com/video/BV1GJ411x7h7"

# 中文字面量一律用码点拼，避开脚本解析阶段的编码问题。
$videoDl = -join ([char]0x89C6, [char]0x9891, [char]0x4E0B, [char]0x8F7D)
$startDl = -join ([char]0x5F00, [char]0x59CB, [char]0x4E0B, [char]0x8F7D)
$ready   = -join ([char]0x5C31, [char]0x7EEA, [char]0x3002)
$doneW   = -join ([char]0x5B8C, [char]0x6210)
$failW   = -join ([char]0x5931, [char]0x8D25)

New-Item -ItemType Directory -Force -Path $shots | Out-Null
if (Test-Path $dlDir) { Remove-Item $dlDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dlDir | Out-Null

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
# 每次重查一遍状态标签，避免跨进程元素引用失效。
function Get-StatusText($hwnd, $prevElem) {
  try {
    if ($prevElem) { return $prevElem.Current.Name }
  } catch { }
  foreach ($t in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Text))) {
    $n = $t.Current.Name
    if ($n -and $n.Length -lt 200 -and $n -notlike "*yt-dlp*" -and $n -notlike "*ffmpeg*") {
      return $n
    }
  }
  return ""
}

Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600
[void][W]::ReleaseLeft()

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

# 初始状态文字
$statusElem = $null
foreach ($t in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Text))) {
  if ($t.Current.Name -eq $ready) { $statusElem = $t; break }
}
if (-not $statusElem) { Write-Output "ABORT: status label (ready) not found"; exit 1 }
Write-Output ("initial status = '{0}'" -f $statusElem.Current.Name)

$edits = Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Edit)
($edits[1].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).SetValue($url)
($edits[2].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).SetValue($dlDir)
Write-Output "url + saveDir set"

$go = $null
foreach ($b in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Button))) {
  if ($b.Current.Name -eq $startDl) { $go = $b; break }
}
if (-not $go) { Write-Output "ABORT: start button not found"; exit 1 }
[void](Click-Elem $hwnd $go "start download")

# 采样状态文字与进度条数值，记录变化序列
$history = @()
$last = ""
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 240) {
  $text = ""
  try { $text = $statusElem.Current.Name } catch { Write-Output "warn: status element stale" }
  $bar = ""
  try {
    $pb = (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::ProgressBar) | Select-Object -First 1)
    if ($pb) { $bar = $pb.GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern).Current.Value }
  } catch { }

  if ($text -ne $last) {
    $line = "[{0,5:N1}s] status='{1}'  bar={2}" -f $sw.Elapsed.TotalSeconds, $text, $bar
    Write-Output $line
    $history += $text
    $last = $text
  }
  if ($text -like "*$doneW*" -or $text -like "*$failW*") { break }
  Start-Sleep -Milliseconds 700
}
Shot $hwnd "status_timeline.png"

Write-Output "--- status sequence:"
$i = 0
foreach ($h in $history) { $i++; Write-Output ("   {0}. {1}" -f $i, $h) }
Write-Output ("final status = '{0}'" -f $last)

Write-Output "--- output files:"
Get-ChildItem $dlDir -File -ErrorAction SilentlyContinue | ForEach-Object {
  Write-Output ("   {0}  {1:N0} bytes" -f $_.Name, $_.Length)
}

Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
Write-Output "DONE"