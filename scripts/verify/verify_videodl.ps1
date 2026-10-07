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
  $ec = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Edit)
  $best = $null; $bestScore = 1e9
  foreach ($e in $ae.FindAll([System.Windows.Automation.TreeScope]::Descendants, $ec)) {
    $r = $e.Current.BoundingRectangle
    if ($r.X -lt $lr.X) { continue }        # 必须在标签右侧
    $dy = [math]::Abs($r.Y - $lr.Y)
    if ($dy -gt 24) { continue }            # 同一行
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
using System.Text;
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

  // 归一化的 MOUSEEVENTF_ABSOLUTE 在这台机器上会被重映射，落点不对；
  // 先用 SetCursorPos 摆好光标，再注入不带位移的按下/抬起。
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
New-Item -ItemType Directory -Force -Path $shots | Out-Null

# 脚本里不写中文字面量：PowerShell 5.1 对无 BOM 的 .ps1 会按 ANSI 解析，中文会乱码。
$videoDl = -join ([char]0x89C6, [char]0x9891, [char]0x4E0B, [char]0x8F7D)

function Get-Hwnd {
  $p = Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { return [IntPtr]::Zero }
  $p.Refresh()
  return $p.MainWindowHandle
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

function Focus($hwnd) {
  [void][W]::SetForegroundWindow($hwnd)
  Start-Sleep -Milliseconds 400
  return ([W]::GetForegroundWindow() -eq $hwnd)
}

function Find-ByType($hwnd, $type) {
  $ae = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty, $type)
  return $ae.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)
}

function Dump-Plugin {
  $label = $args[0]
  Write-Output "== registry $label"
  if (Test-Path $plugin) {
    $k = Get-Item $plugin
    foreach ($n in $k.GetValueNames()) {
      Write-Output ("   {0} = {1}" -f $n, $k.GetValue($n))
    }
  } else { Write-Output "   <no plugin key>" }
}

function Open-VideoDl($hwnd) {
  $items = Find-ByType $hwnd ([System.Windows.Automation.ControlType]::ListItem)
  $target = $null
  foreach ($i in $items) { if ($i.Current.Name -like "*$videoDl*") { $target = $i; break } }
  if (-not $target) { Write-Output "ABORT: nav item not found"; return $false }
  $r = $target.Current.BoundingRectangle
  if (-not (Focus $hwnd)) { Write-Output "ABORT: not foreground"; return $false }
  [void][W]::ClickAt([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2))
  Start-Sleep -Seconds 2
  return $true
}

function Dump-PageText($hwnd) {
  foreach ($t in @([System.Windows.Automation.ControlType]::Text,
                   [System.Windows.Automation.ControlType]::Button,
                   [System.Windows.Automation.ControlType]::ComboBox)) {
    foreach ($e in (Find-ByType $hwnd $t)) {
      Write-Output ("   [{0}] '{1}'" -f $t.ProgrammaticName, $e.Current.Name)
    }
  }
}

# ---------- 清场 ----------
Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Enter-UiLanguage
Start-Sleep -Milliseconds 600
Remove-Item -Path $plugin -Recurse -Force -ErrorAction SilentlyContinue
[void][W]::ReleaseLeft()

# ---------- 1. 首次启动，进入「视频下载」 ----------
[void](Start-Process -FilePath $exe -PassThru)
Start-Sleep -Seconds 3
$hwnd = Get-Hwnd
Write-Output ("main window handle = {0}" -f $hwnd)
if (-not (Open-VideoDl $hwnd)) { Write-Output "DONE(abort1)"; exit 1 }
Shot $hwnd "v1_page.png"
Write-Output "-- page content"
Dump-PageText $hwnd
Dump-Plugin "after first open"

$edits = Find-ByType $hwnd ([System.Windows.Automation.ControlType]::Edit)
Write-Output ("-- edit count = {0}" -f $edits.Count)
Check ($edits.Count -ge 3) "页面有 3 个以上输入框（地址 / 保存目录 / cookies）"
for ($i = 0; $i -lt $edits.Count; $i++) {
  $vp = $edits[$i].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
  Write-Output ("   edit[{0}] = '{1}'" -f $i, $vp.Current.Value)
}

# ---------- 2. 改保存目录 + 改画质 ----------
$dirEdit = Find-EditBesideLabel $hwnd "保存到"
$newDir = "C:\WorkSpace\ProjectSpace\tools-box\build\shots\dl"
if ($dirEdit) {
  $v = $dirEdit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
  $v.SetValue($newDir)
  Write-Output ("set saveDir -> {0}" -f $newDir)
} else { Write-Output "saveDir edit NOT FOUND (expected >= 3 edits)" }
Check ($null -ne $dirEdit) "找到了保存目录输入框"

$combo = Find-ByType $hwnd ([System.Windows.Automation.ControlType]::ComboBox) | Select-Object -First 1
if ($combo) {
  $qBefore = Get-ComboValue $hwnd   # 改动之前先读，否则「变了没有」永远比不出来
  # 旧写法是 SetFocus + 连按 4 次 {DOWN}，实测**画质并没有变** —— 焦点并没有真正
  # 落到 QComboBox 上，方向键被别处吃掉了，而脚本只打印一句 "quality selection
  # unreadable" 就当作改完了。改走 UIAutomation 的 ExpandCollapse（已探明本机
  # QComboBox 暴露 ExpandCollapse + Value，但**不暴露 Selection**），直接点弹层项。
  $expand = $combo.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
  $expand.Expand()
  Start-Sleep -Milliseconds 700
  $lcond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::ListItem)
  $root = [System.Windows.Automation.AutomationElement]::RootElement
  $picked = $null
  foreach ($li in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $lcond)) {
    if ($li.Current.Name -like "*1080p*") { $picked = $li; break }
  }
  Check ($null -ne $picked) "画质下拉展开了，能看到 1080p 选项"
  if ($picked) {
    # 本脚本没有 Click-Elem（各脚本的辅助函数并不统一），按 Open-VideoDl 的做法
    # 直接用坐标点击弹层项。
    $pr = $picked.Current.BoundingRectangle
    if (Focus $hwnd) {
      [void][W]::ClickAt([int]($pr.X + $pr.Width / 2), [int]($pr.Y + $pr.Height / 2))
    }
    Start-Sleep -Milliseconds 700
  }
  $qAfter = Get-ComboValue $hwnd
  Write-Output ("quality now = '{0}'" -f $qAfter)
  Check (-not [string]::IsNullOrWhiteSpace($qAfter)) "画质下拉读得出当前值"
  Check ($qAfter -like "*1080p*") "选中 1080p 后画质下拉显示为 1080p（$qBefore -> $qAfter）"
} else { Write-Output "quality combo NOT FOUND" }
Start-Sleep -Milliseconds 500
Shot $hwnd "v2_changed.png"

# ---------- 3. 关窗 -> saveState ----------
[void][W]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Seconds 2
Dump-Plugin "after close (saveState)"

# ---------- 4. 重开 -> restoreState ----------
[void](Start-Process -FilePath $exe -PassThru)
Start-Sleep -Seconds 3
$hwnd2 = Get-Hwnd
if (-not (Open-VideoDl $hwnd2)) { Write-Output "DONE(abort2)"; exit 1 }
Shot $hwnd2 "v3_restart.png"

$dirEdit2 = Find-EditBesideLabel $hwnd2 "保存到"
if ($dirEdit2) {
  $v2 = $dirEdit2.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
  Write-Output ("restored saveDir = '{0}'  (expect {1})" -f $v2.Current.Value, $newDir)
  Check ($v2.Current.Value -eq $newDir) "重启后保存目录已恢复（设置持久化）"
}
$q2 = Get-ComboValue $hwnd2
Write-Output ("restored quality = '{0}'" -f $q2)
Check (-not [string]::IsNullOrWhiteSpace($q2)) "重启后画质有选中项（设置持久化）"
Write-Output "-- page content after restart"
Dump-PageText $hwnd2

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
