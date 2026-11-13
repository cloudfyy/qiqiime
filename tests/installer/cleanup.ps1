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
Write-Host 'Uninstall data retention defaults, opt-in and upgrade guards passed.'
