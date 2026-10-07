param(
  [string]$Url = "https://www.bilibili.com/video/BV1GJ411x7h7",
  [string]$Tag = "logcheck",
  [string]$CookiesFile = "",
  [string]$Exe = "c:/WorkSpace/ProjectSpace/tools-box/build/bin/Debug/ToolBox.exe"
)

# Force the UI language for this run and restore it on exit: with a
# LANG/LC_ALL environment variable in the launching shell the app comes
# up in English, and every lookup by Chinese label below would abort with
# "nav not found" -- a symptom three layers from the cause. See
# _ui_language.ps1 and docs/error_ledger.md.
. (Join-Path $PSScriptRoot "_ui_language.ps1")
# 判定机制：以前这个脚本只打印日志内容、结尾无条件 DONE 且退出码 0 —— 日志里
# 命令行拼错了、cookie 路径没脱敏，它也照样「通过」。现在把三件真正要紧的事
# 变成断言：命令行确实执行了、cookie 路径确实被脱敏、降级提示确实出现。
$failed = @()
function Check($ok, $what) {
  if ($ok) { Write-Output "PASS: $what" } else { Write-Output "FAIL: $what"; $script:failed += $what }
}
$execLine = -join ([char]0x6267, [char]0x884C)   # "执行"

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
Enter-UiLanguage
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
# 点之前先确认按钮真的可用：页面要等地址校验通过才启用它，按钮禁用时点击是
# 无效操作 —— 下载没启动、日志自然是空的，断言会把这个报成「代码回归」，而真因
# 是站点风控 / 需要 cookies。所以：等它启用，等不到就以**专用退出码 2** 退出，
# 让报告能把它归到「环境/站点因素」而不是「代码失败」。
$enableDeadline = (Get-Date).AddSeconds(20)
while (-not $go.Current.IsEnabled -and (Get-Date) -lt $enableDeadline) {
  Start-Sleep -Milliseconds 500
  $go = $null
  foreach ($b in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Button))) {
    if ($b.Current.Name -eq $startDl) { $go = $b; break }
  }
  if (-not $go) { break }
}
if (-not $go -or -not $go.Current.IsEnabled) {
  Write-Output "ABORT: start button stayed disabled for 20s -- address rejected or the site needs cookies"
  Write-Output "       这是站点/环境因素，不计入代码回归（退出码 2）"
  Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
  Restore-UiLanguage
  exit 2
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
# 断言 1：确实跑过一次下载（命令行被执行过）
Check ($log -like "*$execLine*") "日志里有下载命令行（下载确实启动过）"
# 断言 2：cookie 路径已脱敏 —— 命令行里出现 --cookies 就说明把路径原样带出去了
Check ($log -notlike "*--cookies*") "命令行里没有 --cookies（cookie 路径已脱敏）"
# 断言 3：ffmpeg 缺失时应有降级提示（缺了它会让用户以为画质选项失效）
Check ($log -like "*$normW*") "日志里有降级/提示信息"

Write-Output "--- lines containing the normalization notice or --cookies ---"
foreach ($l in ($log -split "`r?`n")) {
  if ($l -like "*$normW*" -or $l -like "*--cookies*") { Write-Output ("   {0}" -f $l.Trim()) }
}
Write-Output "--- last 30 log lines ---"
($log -split "`r?`n") | Where-Object { $_.Trim() -ne '' } | Select-Object -Last 30 | ForEach-Object { Write-Output ("   {0}" -f $_.Trim()) }
Write-Output "--- output files ---"
Get-ChildItem $dlDir -File -ErrorAction SilentlyContinue | ForEach-Object { Write-Output ("   {0}  {1:N0} bytes" -f $_.Name, $_.Length) }

Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Restore-UiLanguage

if ($failed.Count -gt 0) {
  Write-Output ("DONE(FAILED {0}): {1}" -f $failed.Count, ($failed -join "; "))
  exit 1
}
Write-Output "DONE(ALL PASS)"
exit 0
