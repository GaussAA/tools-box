# Enforces docs/coding-standards.md section 2: a source file over ~600 lines must be
# evaluated for a split, and the outcome recorded. "Recorded" means listed in the
# exemption table below with a reason and a pointer to where the decision lives.
#
# Why this is a script and not a review item: "we will split it later" is otherwise
# invisible. Nobody re-reads a 600-line rule during a busy review, so the file keeps
# growing and the decision is never written down. A recorded exemption with its own
# line limit at least makes the debt explicit and stops it from growing silently --
# the check fails again as soon as the file passes the limit it was granted.
#
# NOTE: this file must keep its UTF-8 BOM. Windows PowerShell 5.1 reads a BOM-less
# script with the system ANSI codepage (936 / GB2312 here), which mangles UTF-8 Chinese
# comments badly enough to break parsing. verify_whitespace.ps1 enforces the BOM.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\verify\verify_filesize.ps1
# Exit code 0 = clean, 1 = violations found.

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$git = (Get-Command git -ErrorAction SilentlyContinue)
if (-not $git) { Write-Output "FAIL: git not found on PATH"; exit 1 }

# Default threshold from coding-standards.md section 2.
$threshold = 600

# Exemptions: relative path -> @{ Limit = <max lines>; Why = <reason> }.
# Adding one is a deliberate act: it must name the place where the decision is
# recorded, so a reader can go and read the reasoning instead of trusting this table.
$exemptions = @{
  "plugins/videodl/VideoDlPlugin.cpp" = @{
    Limit = 800
    Why   = "deviation 9.2 / plan D2 in docs/architecture.md: parsing rules are in plugins/videodl/core, the engine downloader is EngineFetcher.h, and the runner / resolver are DownloadRunner.h / DouyinResolver.h; the page is UI-only and must not grow back past this"
  }
}

$sources = @(& git -C $repo ls-files "*.cpp" "*.h" "*.hpp") `
           + @(& git -C $repo ls-files --others --exclude-standard "*.cpp" "*.h" "*.hpp")
$sources = @($sources | Sort-Object -Unique)

Write-Output ("repo       = " + $repo)
Write-Output ("threshold  = " + $threshold + " lines")
Write-Output ("exemptions = " + $exemptions.Count + " file(s)")

$failed = @()
$stale = @()

foreach ($rel in $sources) {
  $path = Join-Path $repo $rel
  if (-not (Test-Path $path -PathType Leaf)) { continue }

  # 行数用 .NET 读，**不能用 Get-Content**：PowerShell 5.1 按系统 ANSI 代码页解码
  # 无 BOM 的 UTF-8，中文注释被解成乱码的同时还会把相邻行并成一行 —— 实测
  # plugins/videodl/VideoDlPlugin.cpp 实际 1269 行被数成 1181。门禁数错行数就会在
  # 文件真的超限时给出假绿，那比没有这个脚本更糟（同一个坑的另一个表现见
  # verify_whitespace.ps1 的 *.ps1 必须带 BOM）。
  $lines = [System.IO.File]::ReadAllLines($path).Length
  if ($lines -le $threshold) {
    # An exemption that is no longer needed is dead weight: it quietly permits the
    # file to grow back. Report it so the table stays honest.
    if ($exemptions.ContainsKey($rel)) { $stale += $rel }
    continue
  }

  if ($exemptions.ContainsKey($rel)) {
    $limit = $exemptions[$rel].Limit
    if ($lines -le $limit) {
      Write-Output ("EXEMPT: {0} ({1} lines, limit {2}) -- {3}" -f $rel, $lines, $limit, $exemptions[$rel].Why)
      continue
    }
    $failed += ("{0} is {1} lines, over the {2} it was granted" -f $rel, $lines, $limit)
    continue
  }

  $failed += ("{0} is {1} lines, over {2} and not recorded anywhere" -f $rel, $lines, $threshold)
}

foreach ($rel in $stale) {
  Write-Output ("STALE: {0} is back under {1} lines -- drop its exemption" -f $rel, $threshold)
}

if ($failed.Count -gt 0) {
  Write-Output ""
  Write-Output ("FAIL: {0} file(s) over the line limit" -f $failed.Count)
  foreach ($one in $failed) { Write-Output ("  - " + $one) }
  Write-Output "Split the file, or record the decision in docs/architecture.md and add it to the exemption table above."
  exit 1
}

Write-Output ("PASS: no unrecorded source file over {0} lines ({1} file(s) scanned)" -f $threshold, $sources.Count)
exit 0
