# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
[xml]$source = Get-Content -LiteralPath (Join-Path $root 'installer\QiQiIME.wxs') -Raw -Encoding UTF8
$ns = [System.Xml.XmlNamespaceManager]::new($source.NameTable)
$ns.AddNamespace('w', 'http://wixtoolset.org/schemas/v4/wxs')
$package = $source.SelectSingleNode('/w:Wix/w:Package', $ns)
$property = $package.SelectSingleNode("w:Property[@Id='QIQI_REMOVE_USER_DATA']", $ns)
if ($null -eq $property -or $property.HasAttribute('Value') -or $property.Secure -ne 'yes') {
    throw 'User data removal must be opt-in, unchecked by default, and passed securely to execute sequence.'
}
$checkbox = $package.SelectSingleNode("w:UI/w:Dialog[@Id='UninstallDataDlg']/w:Control[@Type='CheckBox']", $ns)
if ($null -eq $checkbox -or $checkbox.Property -ne 'QIQI_REMOVE_USER_DATA' -or $checkbox.CheckBoxValue -ne '1') {
    throw 'Uninstall must offer one explicit data removal checkbox.'
}
$show = $package.SelectSingleNode("w:InstallUISequence/w:Show[@Dialog='UninstallDataDlg']", $ns)
$promptCondition = 'Installed AND REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE AND UILevel >= 3 AND UILevel < 5 AND NOT QIQI_REMOVE_USER_DATA'
$prompt = $package.SelectSingleNode("w:InstallExecuteSequence/w:Custom[@Action='PromptRemoveUserData']", $ns)
if ($null -eq $prompt -or $prompt.Before -ne 'DeactivateUserProfile' -or
    $prompt.Condition -ne $promptCondition) {
    throw 'Basic/reduced uninstall must prompt before deactivation, excluding silent/full UI, upgrades and explicit command-line choices.'
}
$promptAction = $package.SelectSingleNode("w:CustomAction[@Id='PromptRemoveUserData']", $ns)
if ($null -eq $promptAction -or $promptAction.BinaryRef -ne 'InstallerActions' -or
    $promptAction.DllEntry -ne 'PromptRemoveUserData' -or $promptAction.Execute -ne 'immediate' -or
    $promptAction.Impersonate -ne 'yes' -or $promptAction.Return -ne 'check') {
    throw 'The fallback prompt must be a synchronous immediate MSI action with checked cancellation/errors.'
}
$promptBinary = $package.SelectSingleNode("w:Binary[@Id='InstallerActions']", $ns)
if ($null -eq $promptBinary -or
    $promptBinary.SourceFile -ne '$(var.StageDir)\$(var.NativeFileSuffix)\QiQiIMEInstallerActions.dll') {
    throw 'The fallback prompt must be embedded and use the native installer-action DLL.'
}
$promptText = $package.SelectSingleNode("w:Property[@Id='QIQI_REMOVE_USER_DATA_PROMPT']", $ns)
if ($null -eq $promptText -or $promptText.Value -ne '!(loc.RemoveUserDataPrompt)') {
    throw 'The basic/reduced uninstall confirmation must use package-localized text.'
}
$localizedPrompts = @{}
foreach ($culture in @('zh-CN', 'en-US')) {
    [xml]$localization = Get-Content -LiteralPath (Join-Path $root "installer\$culture.wxl") -Raw -Encoding UTF8
    $localNs = [System.Xml.XmlNamespaceManager]::new($localization.NameTable)
    $localNs.AddNamespace('l', 'http://wixtoolset.org/schemas/v4/wxl')
    $texts = @($localization.SelectNodes("/l:WixLocalization/l:String[@Id='RemoveUserDataPrompt']", $localNs))
    if ($texts.Count -ne 1 -or [string]::IsNullOrWhiteSpace($texts[0].Value)) {
        throw "Missing or duplicate uninstall confirmation translation: $culture"
    }
    $localizedPrompts[$culture] = $texts[0].Value
}
if ($localizedPrompts['zh-CN'] -notmatch '\p{IsCJKUnifiedIdeographs}' -or
    $localizedPrompts['en-US'] -notmatch '[A-Za-z]' -or
    $localizedPrompts['en-US'] -match '\p{IsCJKUnifiedIdeographs}' -or
    $localizedPrompts['zh-CN'] -eq $localizedPrompts['en-US']) {
    throw 'The uninstall confirmation must provide distinct Chinese and English translations.'
}
$remove = $package.SelectSingleNode("w:InstallExecuteSequence/w:Custom[@Action='RemoveUserData']", $ns)
if ($null -eq $show -or $show.Before -ne 'ExecuteAction' -or
    $show.Condition -ne 'Installed AND REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE') {
    throw 'Prompt must be limited to explicit uninstall, before execution, never upgrade removal.'
}
if ($null -eq $remove -or $remove.Before -ne 'InstallFinalize' -or
    $remove.Condition -ne 'Installed AND REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE AND QIQI_REMOVE_USER_DATA="1"') {
    throw 'Data removal must require explicit opt-in and exclude installation, repair and upgrade.'
}
$action = $package.SelectSingleNode("w:CustomAction[@Id='RemoveUserData']", $ns)
if ($action.Execute -ne 'commit' -or $action.Impersonate -ne 'yes' -or $action.Return -ne 'check' -or
    $action.BinaryRef -ne 'UserDataCleaner' -or $action.ExeCommand -ne '--remove-user-data') {
    throw 'Cleanup must use an embedded EXE at successful commit, as the initiating user, with checked errors.'
}
$binary = $package.SelectSingleNode("w:Binary[@Id='UserDataCleaner']", $ns)
if ($null -eq $binary -or $binary.SourceFile -ne '$(var.StageDir)\$(var.NativeFileSuffix)\QiQiIMESettings.exe') {
    throw 'Cleanup must retain the native executable independently of installed files.'
}
Write-Host 'Uninstall data retention defaults, full/basic/reduced prompts, opt-in and upgrade guards passed.'
