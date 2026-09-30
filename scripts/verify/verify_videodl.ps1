Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
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

$exe    = "c:\WorkSpace\ProjectSpace\tools-box\build\bin\Debug\ToolBox.exe"
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
for ($i = 0; $i -lt $edits.Count; $i++) {
  $vp = $edits[$i].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
  Write-Output ("   edit[{0}] = '{1}'" -f $i, $vp.Current.Value)
}

# ---------- 2. 改保存目录 + 改画质 ----------
$dirEdit = if ($edits.Count -ge 3) { $edits[2] } else { $null }
$newDir = "C:\WorkSpace\ProjectSpace\tools-box\build\shots\dl"
if ($dirEdit) {
  $v = $dirEdit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
  $v.SetValue($newDir)
  Write-Output ("set saveDir -> {0}" -f $newDir)
} else { Write-Output "saveDir edit NOT FOUND (expected >= 3 edits)" }

$combo = (Find-ByType $hwnd ([System.Windows.Automation.ControlType]::ComboBox) | Select-Object -First 1)
if ($combo) {
  $combo.SetFocus()
  Start-Sleep -Milliseconds 300
  # 非编辑型 QComboBox：焦点在它身上时按方向键直接改当前项。
  for ($i = 0; $i -lt 4; $i++) { [System.Windows.Forms.SendKeys]::SendWait("{DOWN}"); Start-Sleep -Milliseconds 200 }
  $sp = $combo.GetCurrentPattern([System.Windows.Automation.SelectionPattern]::Pattern)
  $sel = $sp.Current.GetSelection()
  if ($sel.Count -gt 0) { Write-Output ("quality now = '{0}'" -f $sel[0].Current.Name) }
  else { Write-Output "quality selection unreadable" }
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

$edits2 = Find-ByType $hwnd2 ([System.Windows.Automation.ControlType]::Edit)
if ($edits2.Count -ge 3) {
  $v2 = $edits2[2].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
  Write-Output ("restored saveDir = '{0}'  (expect {1})" -f $v2.Current.Value, $newDir)
}
$combo2 = (Find-ByType $hwnd2 ([System.Windows.Automation.ControlType]::ComboBox) | Select-Object -First 1)
if ($combo2) {
  $sp2 = $combo2.GetCurrentPattern([System.Windows.Automation.SelectionPattern]::Pattern)
  $sel2 = $sp2.Current.GetSelection()
  if ($sel2.Count -gt 0) { Write-Output ("restored quality = '{0}'" -f $sel2[0].Current.Name) }
}
Write-Output "-- page content after restart"
Dump-PageText $hwnd2

[void][W]::PostMessage($hwnd2, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 800
Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output "DONE"
