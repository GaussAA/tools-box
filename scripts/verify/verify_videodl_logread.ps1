param(
  [string]$Url = "https://www.bilibili.com/video/BV1GJ411x7h7",
  [string]$Tag = "logcheck",
  [string]$CookiesFile = ""
)

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy, mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public MOUSEINPUT mi; }
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] inputs, int size);
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
$dlDir = "c:\WorkSpace\ProjectSpace\tools-box\build\vdl-$Tag"
$url   = $Url
$ck    = $CookiesFile
$videoDl = -join ([char]0x89C6, [char]0x9891, [char]0x4E0B, [char]0x8F7D)
$startDl = -join ([char]0x5F00, [char]0x59CB, [char]0x4E0B, [char]0x8F7D)
$normW   = -join ([char]0x4FEE, [char]0x6B63)

if (Test-Path $dlDir) { Remove-Item $dlDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dlDir | Out-Null

function Get-Hwnd {
  $p = Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { return [IntPtr]::Zero }
  $p.Refresh(); return $p.MainWindowHandle
}
function Find-ByType($hwnd, $type) {
  $ae = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty, $type)
  return $ae.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)
}
function Focus($hwnd) {
  [void][W]::SetForegroundWindow($hwnd); Start-Sleep -Milliseconds 400
  return ([W]::GetForegroundWindow() -eq $hwnd)
}
function Click-Elem($hwnd, $elem) {
  if (-not (Focus $hwnd)) { return $false }
  $r = $elem.Current.BoundingRectangle
  [void][W]::ClickAt([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2))
  Start-Sleep -Milliseconds 900
  return $true
}

Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600

[void](Start-Process -FilePath $exe -PassThru)
Start-Sleep -Seconds 3
$hwnd = Get-Hwnd

foreach ($i in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::ListItem))) {
  if ($i.Current.Name -like "*$videoDl*") { [void](Click-Elem $hwnd $i); break }
}
Start-Sleep -Seconds 1

$edits = Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Edit)
($edits[1].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).SetValue($url)
($edits[2].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).SetValue($dlDir)
if ($ck -ne "") {
  ($edits[3].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).SetValue($ck)
}

$go = $null
foreach ($b in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Button))) {
  if ($b.Current.Name -eq $startDl) { $go = $b; break }
}
[void](Click-Elem $hwnd $go)

# yt-dlp 跑完下载还会拉起 ffmpeg 做合并，两个都停了才算收工。
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 300) {
  Start-Sleep -Seconds 2
  $busy = (Get-Process -Name yt-dlp,ffmpeg -ErrorAction SilentlyContinue) -ne $null
  if (-not $busy -and $sw.Elapsed.TotalSeconds -gt 8) { break }
}
Start-Sleep -Seconds 1

# 日志是多行只读文本框：逐个 Edit 读值，取最长的那个。
$log = ""
foreach ($e in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Edit))) {
  try {
    $v = ($e.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).Current.Value
    if ($v -and $v.Length -gt $log.Length) { $log = $v }
  } catch { }
}
Write-Output ("log length = {0}" -f $log.Length)
Write-Output "--- first 6 log lines ---"
($log -split "`r?`n") | Where-Object { $_.Trim() -ne '' } | Select-Object -First 6 | ForEach-Object { Write-Output ("   {0}" -f $_.Trim()) }
Write-Output "--- lines containing the normalization notice or --cookies ---"
foreach ($l in ($log -split "`r?`n")) {
  if ($l -like "*$normW*" -or $l -like "*--cookies*") { Write-Output ("   {0}" -f $l.Trim()) }
}
Write-Output "--- last 30 log lines ---"
($log -split "`r?`n") | Where-Object { $_.Trim() -ne '' } | Select-Object -Last 30 | ForEach-Object { Write-Output ("   {0}" -f $_.Trim()) }
Write-Output "--- output files ---"
Get-ChildItem $dlDir -File -ErrorAction SilentlyContinue | ForEach-Object { Write-Output ("   {0}  {1:N0} bytes" -f $_.Name, $_.Length) }

Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output "DONE"
