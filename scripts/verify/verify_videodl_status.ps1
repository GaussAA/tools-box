# Force the UI language for this run and restore it on exit: with a
# LANG/LC_ALL environment variable in the launching shell the app comes
# up in English, and every lookup by Chinese label below would abort with
# "nav not found" -- a symptom three layers from the cause. See
# _ui_language.ps1 and docs/error_ledger.md.
# -Exe points the check at another build. The default is the Debug build, but
# note that the Qt runtime only exists in an output directory that has had the
# deploy target run (build_verify.ps1 does that for Release) -- aiming this at a
# build without the runtime makes the app exit on startup, and the symptom then
# looks like "the feature is broken" rather than "the app never started".
param([string]$Exe = "c:/WorkSpace/ProjectSpace/tools-box/build/bin/Debug/ToolBox.exe")
. (Join-Path $PSScriptRoot "_ui_language.ps1")
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
# 判定机制：本脚本此前只打印观察到的状态文字与产物清单、结尾无条件 DONE 且
# 退出码 0 —— 下载卡在解析阶段、什么都没下下来，它也照样「通过」。现在把真正
# 要紧的几件事变成断言，并按失败数决定退出码（0=通过，1=代码回归，2=环境或
# 站点因素，由前面的 ABORT 路径给出）。
$failed = @()
function Check($ok, $what) {
  if ($ok) { Write-Output "PASS: $what" } else { Write-Output "FAIL: $what"; $script:failed += $what }
}

# 页面字段会随功能增长而变多（共享内核目录落地后输入框从 3 个变成 6 个），所以
# 不能用「$edits[1] 是地址、$edits[2] 是保存目录」这种按下标猜字段的写法 ——
# 猜错不会报错，只会让脚本往错误的输入框里填地址，然后在「为什么没反应」里
# 找半天原因。改成按标签文本定位同行右侧的输入框。
function Find-EditBesideLabel($hwnd, $labelText) {
  $ae = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
  $tc = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Text)
  $label = $null
  foreach ($t in $ae.FindAll([System.Windows.Automation.TreeScope]::Descendants, $tc)) {
    if ($t.Current.Name -eq $labelText) { $label = $t; break }
  }
  if (-not $label) { return $null }
  $lr = $label.Current.BoundingRectangle
  $labelMidY = $lr.Y + $lr.Height / 2
  $ec = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Edit)
  # 按**垂直中心距**找最近的输入框，而不是「同行右侧」：页面上两种摆法都存在 ——
  # 「保存到」「画质」「Cookie 文件」是标签在左、输入框在同一行右侧，而「视频地址」
  # 的输入框却落在标签的斜上方（中心距约 29px，且 x 在标签左侧）。只认同行右侧会
  # 漏掉后者，于是「找不到字段」——而按中心距匹配对两者都成立。
  $best = $null; $bestScore = 1e9
  foreach ($e in $ae.FindAll([System.Windows.Automation.TreeScope]::Descendants, $ec)) {
    $r = $e.Current.BoundingRectangle
    $dy = [math]::Abs(($r.Y + $r.Height / 2) - $labelMidY)
    if ($dy -gt 40) { continue }           # 垂直上离得太远，不是这个字段
    if ($dy -lt $bestScore) { $bestScore = $dy; $best = $e }
  }
  return $best
}

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
Enter-UiLanguage
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

$urlEdit = Find-EditBesideLabel $hwnd "视频地址"
$dirEdit = Find-EditBesideLabel $hwnd "保存到"
if (-not $urlEdit -or -not $dirEdit) {
  Write-Output "ABORT: address or saveDir field not found"
  exit 1
}
($urlEdit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).SetValue($url)
($dirEdit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).SetValue($dlDir)
Check ($urlEdit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value -eq $url) "地址已填入视频地址输入框"
Check ($dirEdit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value -eq $dlDir) "保存目录已填入对应输入框"

$go = $null
foreach ($b in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Button))) {
  if ($b.Current.Name -eq $startDl) { $go = $b; break }
}
if (-not $go) { Write-Output "ABORT: start button not found"; exit 1 }
# 页面要等地址校验通过才启用「开始下载」；按钮禁用时点击是无效操作，下载根本没
# 启动，脚本却会一路打印观察到结束 —— 看起来像「跑过了」，实际什么都没验。所以先
# 等它启用，等不到就以**专用退出码 2** 退出，报告据此归到「站点/环境因素」。
$enableDeadline = (Get-Date).AddSeconds(20)
while ($go -and -not $go.Current.IsEnabled -and (Get-Date) -lt $enableDeadline) {
  Start-Sleep -Milliseconds 500
  $go = $null
  foreach ($b in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Button))) {
    if ($b.Current.Name -eq $startDl) { $go = $b; break }
  }
}
if ($go -and -not $go.Current.IsEnabled) {
  Write-Output "ABORT: start button stayed disabled for 20s -- address rejected or the site needs cookies"
  Write-Output "       这是站点/环境因素，不计入代码回归（退出码 2）"
  [void][W]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
  Start-Sleep -Seconds 1
  Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
  Restore-UiLanguage
  exit 2
}
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
$files = @(Get-ChildItem $dlDir -File -ErrorAction SilentlyContinue)
$files | ForEach-Object {
  Write-Output ("   {0}  {1:N0} bytes" -f $_.Name, $_.Length)
}

# 这个脚本的本意就是「盯着状态文字从就绪一路走到完成」，所以三件事都要断言：
Check ($history.Count -ge 2) ("状态有推进（观察到 {0} 次变化：{1}）" -f $history.Count, ($history -join " -> "))
Check ($last -like "*$doneW*") ("最终状态是「完成」而不是「失败/超时」：{0}" -f $last)
Check ($files.Count -ge 1) "下载目录里确实产出了文件"
$nonEmpty = @($files | Where-Object { $_.Length -gt 0 })
Check ($nonEmpty.Count -eq $files.Count -and $files.Count -ge 1) "产出的文件都不是 0 字节"

Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
Restore-UiLanguage

if ($failed.Count -gt 0) {
  Write-Output ("DONE(FAILED {0}): {1}" -f $failed.Count, ($failed -join "; "))
  exit 1
}
Write-Output "DONE(ALL PASS)"
exit 0
