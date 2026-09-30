param([string]$UrlFile, [string]$Tag = "test", [int]$TimeoutSec = 300, [string]$CookiesFile = "")

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
$dlDir  = "c:\WorkSpace\ProjectSpace\tools-box\build\vdl-$Tag"
$url    = [IO.File]::ReadAllText($UrlFile, [Text.Encoding]::UTF8).Trim()

# 中文字面量一律用码点拼，避开脚本解析阶段的编码问题。
$videoDl = -join ([char]0x89C6, [char]0x9891, [char]0x4E0B, [char]0x8F7D)
$startDl = -join ([char]0x5F00, [char]0x59CB, [char]0x4E0B, [char]0x8F7D)
$doneW   = -join ([char]0x5B8C, [char]0x6210)
$failW   = -join ([char]0x5931, [char]0x8D25)
$recogW  = -join ([char]0x8BC6, [char]0x522B, [char]0x51FA, [char]0x5730, [char]0x5740)
$ready   = -join ([char]0x5C31, [char]0x7EEA, [char]0x3002)

New-Item -ItemType Directory -Force -Path $shots | Out-Null
if (Test-Path $dlDir) { Remove-Item $dlDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dlDir | Out-Null

Write-Output ("=== tag={0}  timeout={1}s" -f $Tag, $TimeoutSec)
Write-Output ("input  = {0}" -f $url)

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
function Doc-Text($hwnd) {
  $out = @()
  foreach ($d in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Document))) {
    try {
      $tp = $d.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern)
      $out = $tp.DocumentRange.GetText(-1) -split "`r?`n"
      break
    } catch { }
  }
  return $out
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

$edits = Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Edit)
($edits[1].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).SetValue($url)
($edits[2].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).SetValue($dlDir)
Write-Output ("url set ok = {0}" -f $edits[1].Current.Name)
Write-Output ("edit count = {0}" -f $edits.Count)
if ($CookiesFile -ne "") {
  ($edits[3].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).SetValue($CookiesFile)
  Write-Output ("cookies set = {0}" -f $edits[3].Current.Name)
}

# 状态标签：以初始的「就绪。」定位，之后一直跟着这个元素读它的文本。
$statusElem = $null
foreach ($t in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Text))) {
  if ($t.Current.Name -eq $ready) { $statusElem = $t; break }
}
if (-not $statusElem) { Write-Output "ABORT: status label not found"; exit 1 }

$go = $null
foreach ($b in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Button))) {
  if ($b.Current.Name -eq $startDl) { $go = $b; break }
}
if (-not $go) { Write-Output "ABORT: start button not found"; exit 1 }
[void](Click-Elem $hwnd $go "start download")

$history = @()
$last = ""
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $TimeoutSec) {
  $text = ""
  try { $text = $statusElem.Current.Name } catch { }
  if ($text -ne $last -and $text -ne "") {
    Write-Output ("[{0,5:N1}s] {1}" -f $sw.Elapsed.TotalSeconds, $text)
    $history += $text
    $last = $text
  }
  if ($text -like "*$doneW*" -or $text -like "*$failW*") { break }
  Start-Sleep -Seconds 1
}
Shot $hwnd ("dl_{0}.png" -f $Tag)

Write-Output "--- log lines mentioning address recognition:"
foreach ($l in (Doc-Text $hwnd)) {
  if ($l -like "*$recogW*") { Write-Output ("   {0}" -f $l.Trim()) }
}

Write-Output "--- output files:"
$files = Get-ChildItem $dlDir -File -ErrorAction SilentlyContinue
if ($files) {
  $files | ForEach-Object { Write-Output ("   {0}  {1:N0} bytes" -f $_.Name, $_.Length) }
} else {
  Write-Output "   (none)"
}

Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
Write-Output "DONE"