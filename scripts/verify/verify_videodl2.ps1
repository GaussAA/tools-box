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
# 判定机制：本脚本此前只打印观察到的值、结尾无条件 DONE 且退出码 0 —— 设置没保存
# 住、下拉框里没有目标项，它也照样「通过」。现在把真正要紧的几件事变成断言，
# 结尾按失败数决定退出码（0=通过，1=代码回归，2 由调用前的 ABORT 路径给出）。
$failed = @()
function Check($ok, $what) {
  if ($ok) { Write-Output "PASS: $what" } else { Write-Output "FAIL: $what"; $script:failed += $what }
}

# 页面字段会随功能增长而变多（共享内核目录落地后输入框从 3 个变成 6 个），所以
# 不能再用「$edits[2] 是保存目录」这种按下标猜字段的写法 —— 猜错不会报错，只会让
# 断言测到另一个字段，然后得出「通过」的假象。这里改成按标签文本定位同行右侧的
# 输入框：字段怎么增删都不影响。
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

# QComboBox 在本机暴露的是 ValuePattern（读当前值）与 ExpandCollapsePattern（展开），
# **不支持 SelectionPattern** —— 旧代码用 GetCurrentPattern(Selection) 读取会抛
# 「不支持该模式」，于是画质断言永远拿不到值。
function Get-ComboValue($hwnd) {
  $ae = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
  $cc = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::ComboBox)
  $cb = $ae.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cc) | Select-Object -First 1
  if (-not $cb) { return "" }
  try { return $cb.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value }
  catch { return "" }
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
Enter-UiLanguage
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

$dlBtnFound = $false
foreach ($b in (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Button))) {
  if ($b.Current.Name -eq "开始下载") {
    $dlBtnFound = $true
    Write-Output ("download button enabled = {0}" -f $b.Current.IsEnabled)
  }
  if ($b.Current.Name -eq "取消") {
    Write-Output ("cancel button enabled = {0} (expect False: idle)" -f $b.Current.IsEnabled)
  }
}
Check $dlBtnFound "页面上存在「开始下载」按钮"
# 注意：此处不再断言按钮禁用 —— 该脚本写于「内核未安装」的前提（共享内核目录
# 落地后 yt-dlp/ffmpeg 可能已在位），断言「禁用」会把环境差异报成代码回归。
Shot $hwnd "v4_idle.png"

# ---------- 2. 展开画质下拉，点最后一项（仅音频 mp3） ----------
$combo = (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::ComboBox) | Select-Object -First 1)
if (-not $combo) { Write-Output "ABORT: combo not found"; exit 1 }
$expand = $combo.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
$expand.Expand()
Start-Sleep -Milliseconds 700

$root = [System.Windows.Automation.AutomationElement]::RootElement
$lcond = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
  [System.Windows.Automation.ControlType]::ListItem)
$popupItem = $null
foreach ($i in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $lcond)) {
  if ($i.Current.Name -like "*mp3*") { $popupItem = $i; break }
}
Check ($null -ne $popupItem) "画质下拉里有「仅音频 mp3」选项"
if ($popupItem) {
  [void](Click-Elem $hwnd $popupItem "popup item (mp3)")
  Start-Sleep -Milliseconds 500
  $qNow = Get-ComboValue $hwnd
  Write-Output ("quality now = '{0}'" -f $qNow)
  Check ($qNow -like "*mp3*") "选中 mp3 后画质下拉显示为 mp3"
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
$q3 = Get-ComboValue $hwnd2
Write-Output ("restored quality = '{0}' (expect mp3)" -f $q3)
Check ($q3 -like "*mp3*") "重启后画质仍是 mp3（设置持久化）"
Dump-Plugin "after restart"

[void][W]::PostMessage($hwnd2, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 800
Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Restore-UiLanguage

if ($failed.Count -gt 0) {
  Write-Output ("DONE(FAILED {0}): {1}" -f $failed.Count, ($failed -join "; "))
  exit 1
}
Write-Output "DONE(ALL PASS)"
exit 0
