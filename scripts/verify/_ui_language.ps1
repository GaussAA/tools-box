# Shared helper: force the UI language for a manual regression script, and put it back.
#
# Why this exists (measured 2026-10-06): on Windows the interface language is NOT
# decided by the system language alone. A LANG / LC_ALL environment variable in the
# launching shell flips a Qt app to English even when GetUserDefaultUILanguage() says
# zh-CN. With LANG=en_US.UTF-8 in the environment and ui/language unset, this app came
# up fully English (nav "Home | Developer tools | ...").
#
# That silently breaks every script which locates a control by its Chinese label:
# they abort with something like "ABORT: nav not found" -- a symptom three layers away
# from the cause. verify_shell.ps1 has always forced ui/language itself; the other
# manual scripts assumed "Chinese unless something odd happens", and in a scheduled
# run (or any non-Chinese shell) that assumption breaks.
#
# Usage, at the top of a script:
#   . (Join-Path $PSScriptRoot "_ui_language.ps1")
#   Enter-UiLanguage            # forces zh_CN, remembers what was there
#   ...launch the app, drive the UI...
#   Restore-UiLanguage          # idempotent; safe to call on the normal path
# The PowerShell.Exiting handler registered by Enter-UiLanguage also restores, so an
# early "exit 1" (ABORT path) cannot leave zh_CN behind as the user's own setting.
#
# NOTE: this file must keep its UTF-8 BOM (see verify_whitespace.ps1).

$script:ToolboxUiLangKey = "HKCU:\Software\ToolBox\ToolBox\ui"
$script:ToolboxUiLangBackup = $null
$script:ToolboxUiLangRestored = $true

function Restore-UiLanguage {
    if ($script:ToolboxUiLangRestored) { return }
    $backup = $script:ToolboxUiLangBackup
    $script:ToolboxUiLangRestored = $true
    if ($null -eq $backup) { return }
    if ($backup.Existed) {
        Set-ItemProperty -Path $script:ToolboxUiLangKey -Name language -Value $backup.Previous
    } else {
        Remove-ItemProperty -Path $script:ToolboxUiLangKey -Name language -ErrorAction SilentlyContinue
    }
}

function Enter-UiLanguage {
    param([string]$Locale = "zh_CN")

    $backup = [pscustomobject]@{ Existed = $false; Previous = $null }
    if (Test-Path $script:ToolboxUiLangKey) {
        $current = Get-ItemProperty -Path $script:ToolboxUiLangKey -Name language -ErrorAction SilentlyContinue
        if ($null -ne $current) {
            $backup.Existed = $true
            $backup.Previous = $current.language
        }
    } else {
        New-Item -Path $script:ToolboxUiLangKey -Force | Out-Null
    }
    $script:ToolboxUiLangBackup = $backup
    $script:ToolboxUiLangRestored = $false

    # Write a full locale, not just a language code: Qt names its own translations by
    # region (qtbase_zh_CN.qm, and there is no qtbase_zh.qm), so a bare "zh" installs a
    # translator that translates nothing. Same reasoning as verify_shell.ps1.
    Set-ItemProperty -Path $script:ToolboxUiLangKey -Name language -Value $Locale

    # Belt and braces for the ABORT paths: those call "exit 1" from the middle of the
    # script, skipping any restore placed at the end.
    Register-EngineEvent -SourceIdentifier Toolbox.UiLanguageRestore -Action {
        Restore-UiLanguage
    } | Out-Null

    Write-Output ("ui/language = '{0}' (forced for this run; the previous value is restored on exit)" -f $Locale)
}
