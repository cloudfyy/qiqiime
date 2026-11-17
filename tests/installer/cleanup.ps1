# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
[xml]$source = Get-Content -LiteralPath (Join-Path $root 'installer\QiQiIME.wxs') -Raw -Encoding UTF8
$ns = [System.Xml.XmlNamespaceManager]::new($source.NameTable)
$ns.AddNamespace('w', 'http://wixtoolset.org/schemas/v4/wxs')
$package = $source.SelectSingleNode('/w:Wix/w:Package', $ns)
foreach ($name in @('QIQI_REMOVE_USER_DATA', 'QIQI_REMOVE_CONFIGURATION', 'QIQI_REMOVE_DICTIONARIES')) {
    $property = $package.SelectSingleNode("w:Property[@Id='$name']", $ns)
    if ($null -eq $property -or $property.HasAttribute('Value') -or $property.Secure -ne 'yes') {
        throw "Personal-data selection must be explicit, default-retained and secure: $name"
    }
}
$uninstall = 'Installed AND REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE'
$execute = $package.SelectSingleNode("w:CustomAction[@Id='ExecuteCleanup']", $ns)
if ($null -eq $execute -or $execute.BinaryRef -ne 'InstallerActions' -or
    $execute.DllEntry -ne 'ExecuteCleanup' -or $execute.Execute -ne 'commit' -or
    $execute.Impersonate -ne 'no' -or $execute.Return -ne 'ignore' -or $execute.HideTarget -ne 'yes') {
    throw 'Validated initiating-user cleanup must run at elevated commit without rolling back program removal.'
}
$cleanupSequence = $package.SelectSingleNode("w:InstallExecuteSequence/w:Custom[@Action='ExecuteCleanup']", $ns)
if ($null -eq $cleanupSequence -or $cleanupSequence.Before -ne 'InstallFinalize' -or
    $cleanupSequence.Condition -ne "$uninstall AND ExecuteCleanup") {
    throw 'Cleanup must be limited to explicit uninstall and a freshly prepared request.'
}
foreach ($entry in @(
    @{ Action = 'ChooseUninstallData'; Before = 'PrepareUninstall' },
    @{ Action = 'PrepareUninstall'; Before = 'DeactivateUserProfile' }
)) {
    $row = $package.SelectSingleNode("w:InstallExecuteSequence/w:Custom[@Action='$($entry.Action)']", $ns)
    if ($null -eq $row -or $row.Before -ne $entry.Before -or $row.Condition -ne $uninstall) {
        throw 'Confirm choices and prepare the validated request before deactivation or any removal.'
    }
}
$complete = $package.SelectSingleNode("w:InstallExecuteSequence/w:Custom[@Action='CompleteUninstall']", $ns)
if ($null -eq $complete -or $complete.After -ne 'InstallFinalize' -or $complete.Condition -ne $uninstall) {
    throw 'Incomplete cleanup must be reported after commit, never cause rollback of program removal.'
}
foreach ($sequence in @('InstallUISequence', 'InstallExecuteSequence')) {
    $gate = $package.SelectSingleNode("w:$sequence/w:Custom[@Action='CheckInstallPendingCleanup']", $ns)
    if ($null -eq $gate -or $gate.Before -ne 'FindRelatedProducts' -or $gate.Condition -ne 'NOT (REMOVE="ALL")') {
        throw 'The pending-cleanup gate must precede upgrade discovery/removal and any installation changes.'
    }
}
$wizard = $package.SelectSingleNode("w:InstallUISequence/w:Custom[@Action='ChooseUninstallData']", $ns)
$progress = $package.SelectSingleNode("w:InstallUISequence/w:Show[@Dialog='UninstallProgressDlg']", $ns)
if ($null -eq $wizard -or $wizard.Before -ne 'UninstallProgressDlg' -or $wizard.Condition -ne $uninstall -or
    $null -eq $progress -or $progress.Before -ne 'ExecuteAction' -or $progress.Condition -ne $uninstall) {
    throw 'Full UI must show choices before progress, with the same explicit-uninstall guards.'
}
$binary = $package.SelectSingleNode("w:Binary[@Id='InstallerActions']", $ns)
if ($null -eq $binary -or $binary.SourceFile -ne '$(var.StageDir)\$(var.NativeFileSuffix)\QiQiIMEInstallerActions.dll') {
    throw 'Wizard and cleanup actions must remain embedded after installed files are removed.'
}
if ($package.SelectSingleNode("w:Binary[@Id='UserDataCleaner']", $ns)) {
    throw 'MSI must not invoke the legacy Settings EXE cleanup that lacks restart handling.'
}
$propertyIds = @('QIQI_WIZARD_TITLE', 'QIQI_WIZARD_DESCRIPTION', 'QIQI_WIZARD_CONFIGURATION',
    'QIQI_WIZARD_DICTIONARIES', 'QIQI_WIZARD_WARNING', 'QIQI_COMPLETE_SUCCESS',
    'QIQI_COMPLETE_RESTART', 'QIQI_COMPLETE_INCOMPLETE', 'QIQI_INSTALL_RESTART', 'QIQI_INSTALL_INCOMPLETE')
foreach ($id in $propertyIds) {
    $text = $package.SelectSingleNode("w:Property[@Id='$id']", $ns)
    if ($null -eq $text -or $text.Value -notmatch '^!\(loc\.[^)]+\)$') {
        throw "Wizard and completion diagnostics must be package-localized: $id"
    }
}
Write-Host 'Uninstall wizard, independent retention defaults, elevated validated cleanup, completion and restart gates passed.'
