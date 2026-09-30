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
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int n);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  static uint SendInput(uint n, INPUT[] inputs, int size) { return SendInputNative(n, inputs, size); }
  [DllImport("user32.dll", EntryPoint = "SendInput")] static extern uint SendInputNative(uint n, INPUT[] inputs, int size);

  // 补发一次抬起，清掉可能卡住的左键状态。
  public static uint ReleaseLeft() {
    INPUT[] a = new INPUT[1];
    a[0].type = 0;
    a[0].mi.dwFlags = 0x0004;
    return SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }

  // 把光标摆到 (x,y)，再注入按下/抬起。不用 MOUSEEVENTF_ABSOLUTE：
  // 这台机器上它的坐标归一化会被重映射，实测落点不对。
  public static uint ClickAt(int x, int y, bool right) {
    SetCursorPos(x, y);
    System.Threading.Thread.Sleep(150);
    INPUT[] a = new INPUT[2];
    a[0].type = 0;
    a[0].mi.dwFlags = right ? 0x0008 : 0x0002;
    a[1].type = 0;
    a[1].mi.dwFlags = right ? 0x0010 : 0x0004;
    uint rc = SendInput(2, a, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(150);
    return rc;
  }

  public static string ClassOf(IntPtr h) {
    StringBuilder sb = new StringBuilder(256);
    GetClassName(h, sb, 256);
    return sb.ToString();
  }

  public static string PointInfo(int x, int y) {
    POINT p; p.x = x; p.y = y;
    IntPtr hit = WindowFromPoint(p);
    return string.Format("hit={0} class={1} root={2} rootClass={3}",
      hit, ClassOf(hit), GetAncestor(hit, 2), ClassOf(GetAncestor(hit, 2)));
  }

  public static int LeftButtonState() { return GetAsyncKeyState(0x01) & 0xFFFF; }
}
"@

$ErrorActionPreference = "Continue"
[void][W]::SetProcessDPIAware()
Write-Output ("screen = {0}x{1}" -f [W]::GetSystemMetrics(0), [W]::GetSystemMetrics(1))
Write-Output ("VK_LBUTTON before = 0x{0:X4}" -f ([W]::LeftButtonState()))
Start-Sleep -Milliseconds 200
[void][W]::ReleaseLeft()
Start-Sleep -Milliseconds 200
Write-Output ("VK_LBUTTON after  = 0x{0:X4}" -f ([W]::LeftButtonState()))

$exe = "c:\WorkSpace\ProjectSpace\tools-box\build\bin\Debug\ToolBox.exe"
$shots = "c:\WorkSpace\ProjectSpace\tools-box\build\shots"
$regPath = "HKCU:\Software\ToolBox\ToolBox\ui"

function Dump-Reg($label) {
  Write-Output "== registry $label"
  if (Test-Path $regPath) {
    $k = Get-Item $regPath
    $names = $k.GetValueNames()
    if ($names.Count -eq 0) { Write-Output "   <empty key>" }
    foreach ($n in $names) {
      $v = $k.GetValue($n)
      $t = $k.GetValueKind($n)
      if ($v -is [string[]]) { $s = "[" + ($v -join " | ") + "]" } else { $s = "$v" }
      Write-Output "   $n ($t) = $s"
    }
  } else { Write-Output "   <no ui key>" }
}

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

# 把窗口切到前台并确认，确认不了就绝不注入点击，免得点到用户的 IDE 上。
function Focus($hwnd) {
  [void][W]::SetForegroundWindow($hwnd)
  Start-Sleep -Milliseconds 400
  return ([W]::GetForegroundWindow() -eq $hwnd)
}

function Nav-Items($hwnd) {
  $ae = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::ListItem)
  $items = $ae.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)
  $out = @()
  foreach ($i in $items) {
    $r = $i.Current.BoundingRectangle
    $out += [pscustomobject]@{ Name = $i.Current.Name; X = [int]$r.X; Y = [int]$r.Y; W = [int]$r.Width; H = [int]$r.Height }
  }
  return $out
}

function Menu-Names {
  $root = [System.Windows.Automation.AutomationElement]::RootElement
  $mcond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::MenuItem)
  $mis = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $mcond)
  $out = @()
  foreach ($mi in $mis) { $out += $mi.Current.Name }
  return $out
}

function Reg-Favorites {
  if (Test-Path $regPath) {
    $k = Get-Item $regPath
    $v = $k.GetValue("favorites")
    if ($v -is [string[]]) { return ($v -join "|") }
    return "$v"
  }
  return ""
}

function Click-Item($hwnd, $item, $right) {
  $cx = $item.X + [int]($item.W / 2)
  $cy = $item.Y + [int]($item.H / 2)
  if (-not (Focus $hwnd)) { Write-Output "ABORT: window not foreground, click not injected"; return $false }
  Write-Output ("click({0}) '{1}' at ({2},{3})  {4}" -f $(if ($right) { "right" } else { "left" }), $item.Name, $cx, $cy, [W]::PointInfo($cx, $cy))
  $rc = [W]::ClickAt($cx, $cy, $right)
  Write-Output "  SendInput rc=$rc (want 2)"
  return $true
}

# ---------- 1. 清场 ----------
Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
Remove-Item -Path $regPath -Recurse -Force -ErrorAction SilentlyContinue
Dump-Reg "after wipe"

# ---------- 2. 首次启动：应该没有收藏/最近使用 ----------
$proc = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 3
$hwnd = Get-Hwnd
Shot $hwnd "n1_fresh.png"
Dump-Reg "after fresh launch"

$items = Nav-Items $hwnd
Write-Output "-- nav items (fresh)"
$items | ForEach-Object { Write-Output ("   '{0}' @ ({1},{2}) {3}x{4}" -f $_.Name, $_.X, $_.Y, $_.W, $_.H) }

# ---------- 3. 点「JSON 格式化」 -> 应进「最近使用」 ----------
$json = $items | Where-Object { $_.Name -like "*JSON*" } | Select-Object -First 1
if ($json) { [void](Click-Item $hwnd $json $false) } else { Write-Output "JSON item NOT FOUND" }
Start-Sleep -Seconds 2
Shot $hwnd "n2_after_click.png"
Dump-Reg "after click JSON"
Write-Output "-- nav items (after click)"
Nav-Items $hwnd | ForEach-Object { Write-Output ("   '{0}' @ ({1},{2})" -f $_.Name, $_.X, $_.Y) }

# ---------- 4. 右键同一项 -> 收藏 ----------
$jsonItem = (Nav-Items $hwnd | Where-Object { $_.Name -like "*JSON*" } | Select-Object -First 1)
if ($jsonItem) {
  # 右键 -> 回车选中菜单里唯一的「收藏」。
  # 弹出菜单是独立顶层窗口，PrintWindow 拍不到；UIA 能看到菜单项，
  # 但 InvokePattern 对 Qt 的菜单不生效，所以用键盘确认。
  [void](Click-Item $hwnd $jsonItem $true)
  Start-Sleep -Milliseconds 900
  Write-Output ("-- menu items: {0}" -f ((Menu-Names) -join ", "))
  [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
  Start-Sleep -Milliseconds 1200
  Write-Output ("-- favorites after ENTER = '{0}'" -f (Reg-Favorites))

  if ((Reg-Favorites) -eq "") {
    [System.Windows.Forms.SendKeys]::SendWait("{ESC}")
    Start-Sleep -Milliseconds 400
    [void](Click-Item $hwnd $jsonItem $true)
    Start-Sleep -Milliseconds 900
    [System.Windows.Forms.SendKeys]::SendWait("{DOWN}{ENTER}")
    Start-Sleep -Milliseconds 1200
    Write-Output ("-- favorites after DOWN+ENTER = '{0}'" -f (Reg-Favorites))
  }

  Shot $hwnd "n4_favorited.png"
  Dump-Reg "after favorite"
  Write-Output "-- nav items (after favorite)"
  Nav-Items $hwnd | ForEach-Object { Write-Output ("   '{0}' @ ({1},{2})" -f $_.Name, $_.X, $_.Y) }
}

# ---------- 5. 关掉再开：收藏/最近使用 应该还在 ----------
[void][W]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Seconds 2
[void](Start-Process -FilePath $exe -PassThru)
Start-Sleep -Seconds 3
$hwnd2 = Get-Hwnd
Shot $hwnd2 "n5_restart.png"
Dump-Reg "after restart"
Write-Output "-- nav items (after restart)"
Nav-Items $hwnd2 | ForEach-Object { Write-Output ("   '{0}' @ ({1},{2})" -f $_.Name, $_.X, $_.Y) }

# ---------- 6. 搜索：JSON 同时出现在收藏/最近使用/分类三处，计数应按工具去重 ----------
$ae2 = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd2)
$econd = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
  [System.Windows.Automation.ControlType]::Edit)
$edit = $ae2.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $econd)
if ($edit) {
  $vp = $edit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
  $vp.SetValue("json")
  Start-Sleep -Seconds 1
  Shot $hwnd2 "n6_search.png"
  Write-Output "-- nav items (search 'json')"
  Nav-Items $hwnd2 | ForEach-Object { Write-Output ("   '{0}' @ ({1},{2})" -f $_.Name, $_.X, $_.Y) }
} else {
  Write-Output "search box (Edit) NOT FOUND"
}

[void][W]::PostMessage($hwnd2, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 800
Get-Process -Name ToolBox -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output "DONE"
