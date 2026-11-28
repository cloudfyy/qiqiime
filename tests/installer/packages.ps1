# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,
    [Parameter(Mandatory = $true)]
    [string]$Version
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Read-MsiTable($Database, [string]$Table, [string[]]$Columns) {
    $select = ($Columns | ForEach-Object { '`' + $_ + '`' }) -join ", "
    $view = $Database.OpenView('SELECT ' + $select + ' FROM `' + $Table + '`')
    try {
        [void]$view.Execute()
        while ($null -ne ($record = $view.Fetch())) {
            try {
                $row = @{}
                for ($i = 0; $i -lt $Columns.Count; $i++) {
                    $row[$Columns[$i]] = $record.StringData($i + 1)
                }
                $row
            } finally {
                [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($record)
            }
        }
    } finally {
        [void]$view.Close()
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view)
    }
}

$installer = New-Object -ComObject WindowsInstaller.Installer
$upgradeCodes = @{}
$componentIds = @{}
$productCodes = @()
$expectedLicenses = @(
    "Apache-2.0.txt", "rime-wubi86-jidian-MIT.txt", "wubi-haifeng-BSD-3-Clause.txt",
    "microsoft-gsl-MIT.txt", "nlohmann-json-MIT.txt", "miniz-MIT.txt",
    "ftxui-MIT.txt", "Unicode-3.0.txt"
)
try {
    foreach ($arch in @("x64", "x86")) {
        $nativeSuffix = $arch.ToUpperInvariant()
        $nativeDirectory = "Dir$nativeSuffix"
        $systemDirectory = if ($arch -eq "x64") { "System64Folder" } else { "SystemFolder" }
        $programFiles = if ($arch -eq "x64") { "ProgramFiles64Folder" } else { "ProgramFilesFolder" }
        $guidPrefix = if ($arch -eq "x64") { "20261029" } else { "20261111" }
        foreach ($culture in @("zh-CN", "en-US")) {
            $path = Join-Path $PackageDirectory "QiQiIME-v$Version-win-$arch-$culture.msi"
            Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Missing MSI: $path"
            $database = $installer.OpenDatabase((Get-Item -LiteralPath $path).FullName, 0)
            try {
                $properties = @{}
                foreach ($row in (Read-MsiTable $database "Property" @("Property", "Value"))) {
                    $properties[$row.Property] = $row.Value
                }
                Assert-True ($properties.ProductVersion -eq $Version) "Incorrect version in $path"
                $lcid = if ($culture -eq "zh-CN") { "2052" } else { "1033" }
                Assert-True ($properties.ProductLanguage -eq $lcid) "Incorrect language in $path"
                Assert-True ($properties.UpgradeCode -eq "{8E7B9D31-5A42-4C81-9F11-$($guidPrefix)9999}") "Upgrade identity changed in $path"
                Assert-True ($properties.ProductCode -notin $productCodes) "ProductCode reused between packages."
                $productCodes += $properties.ProductCode
                $upgradeCodes[$arch] = $properties.UpgradeCode
                $summary = $database.SummaryInformation(0)
                try {
                    $platform = if ($arch -eq "x64") { "x64" } else { "Intel" }
                    Assert-True ($summary.Property(7) -eq "$platform;$lcid") "Incorrect MSI platform in $path"
                } finally {
                    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($summary)
                }

                $directories = @(Read-MsiTable $database "Directory" @("Directory", "Directory_Parent"))
                $install = @($directories | Where-Object { $_.Directory -eq "INSTALLFOLDER" })
                Assert-True ($install.Count -eq 1 -and $install[0].Directory_Parent -eq $programFiles) "Incorrect Program Files directory in $path"
                $components = @(Read-MsiTable $database "Component" @("Component", "ComponentId", "Directory_", "Attributes"))
                $ids = @($components | ForEach-Object { $_.ComponentId } | Sort-Object)
                if ($componentIds.ContainsKey($arch)) {
                    Assert-True (@(Compare-Object $componentIds[$arch] $ids).Count -eq 0) "Component identity differs between languages."
                }
                $componentIds[$arch] = $ids
                foreach ($component in $components) {
                    $is64 = ([int]$component.Attributes -band 256) -ne 0
                    Assert-True ($is64 -eq ($arch -eq "x64")) "Unexpected component bitness in $path"
                    $suffix = switch ($component.Component) {
                        "CmpDllX64" { "0101" }
                        "CmpDllX86" { "0102" }
                        "CmpSharedData" { "0103" }
                        "CmpScripts" { "0104" }
                        "CmpDocs" { "0105" }
                        "CmpStartMenuShortcuts" { "0106" }
                        "CmpLicenses" { "0107" }
                        "CmpFullCodeIndex" { "0108" }
                        "CmpPinyinIndex" { "0109" }
                        default { throw "Unexpected component: $($component.Component)" }
                    }
                    Assert-True ($component.ComponentId -eq "{8E7B9D31-5A42-4C81-9F11-$guidPrefix$suffix}") "Component upgrade identity changed in $path"
                }
                $files = @(Read-MsiTable $database "File" @("File", "Component_", "FileName"))
                $expectedNonLicenseFileCount = if ($arch -eq "x64") { 31 } else { 24 }
                $expectedFileCount = $expectedNonLicenseFileCount + $expectedLicenses.Count
                Assert-True ($files.Count -eq $expectedFileCount) "Unexpected payload count in ${path}: expected $expectedFileCount, found $($files.Count)."
                $indexComponents = @{ "full.idx" = "CmpFullCodeIndex"; "pinyin.idx" = "CmpPinyinIndex" }
                foreach ($name in $indexComponents.Keys) {
                    $indexFile = @($files | Where-Object { $_.FileName.Split('|')[-1] -eq $name })
                    $indexComponent = @($components | Where-Object { $_.Component -eq $indexComponents[$name] })
                    Assert-True ($indexFile.Count -eq 1 -and $indexFile[0].Component_ -eq $indexComponents[$name] -and
                        $indexComponent.Count -eq 1 -and $indexComponent[0].Directory_ -eq "DirData") "Expected one shared data\$name in $path"
                }
                $documentation = @($files | Where-Object { $_.Component_ -eq "CmpDocs" } | ForEach-Object { $_.FileName.Split('|')[-1] })
                foreach ($name in @("LICENSE", "README.md", "README.en.md", "PRIVACY.md", "CODE_SIGNING.md", "THIRD_PARTY_NOTICES.md", "install.md", "log.md", "release-notes.md")) {
                    Assert-True ($name -in $documentation) "Missing documentation $name in $path"
                }
                $licenseDirectory = @($directories | Where-Object { $_.Directory -eq "DirLicenses" })
                Assert-True ($licenseDirectory.Count -eq 1 -and $licenseDirectory[0].Directory_Parent -eq "INSTALLFOLDER") "Incorrect license directory in $path"
                $licenseComponent = @($components | Where-Object { $_.Component -eq "CmpLicenses" })
                Assert-True ($licenseComponent.Count -eq 1 -and $licenseComponent[0].Directory_ -eq "DirLicenses") "Incorrect license component in $path"
                $licenses = @($files | Where-Object { $_.Component_ -eq "CmpLicenses" } | ForEach-Object { $_.FileName.Split('|')[-1] })
                foreach ($name in $expectedLicenses) {
                    Assert-True ($name -in $licenses) "Missing third-party license $name in $path"
                }
                foreach ($payloadArch in $(if ($arch -eq "x64") { @("X64", "X86") } else { @("X86") })) {
                    $payload = @($files | Where-Object { $_.Component_ -eq "CmpDll$payloadArch" })
                    Assert-True ($payload.Count -eq 7) "Missing runtime files in $path"
                    $names = @($payload | ForEach-Object { $_.FileName.Split('|')[-1] })
                    foreach ($name in @("qiqiime.dll", "QiQiIMESettings.exe", "QiQiIMEDictManager.exe", "QiQiIMELogMonitor.exe", "QiQiIMEDiag.exe", "QiQiIMEConfigBroker.exe", "QiQiIMEConfigProxy.dll")) {
                        Assert-True ($name -in $names) "Missing $name in $path"
                    }
                }
                if ($arch -eq "x86") {
                    Assert-True (@($directories | Where-Object { $_.Directory -eq "DirX64" }).Count -eq 0) "x86 MSI contains x64 directory."
                }
                $shortcuts = @(Read-MsiTable $database "Shortcut" @("Shortcut", "Target", "WkDir", "Component_"))
                Assert-True ($shortcuts.Count -eq 4) "Incorrect shortcut count in $path"
                foreach ($shortcut in $shortcuts) {
                    $fileId = switch ($shortcut.Shortcut) {
                        "SettingsShortcut" { "FileSettingsExe$nativeSuffix" }
                        "DictManagerShortcut" { "FileDictManagerExe$nativeSuffix" }
                        "LogMonitorShortcut" { "FileLogMonitorExe$nativeSuffix" }
                        "DiagShortcut" { "FileDiagExe$nativeSuffix" }
                        default { throw "Unexpected shortcut: $($shortcut.Shortcut)" }
                    }
                    Assert-True ($shortcut.Target -eq "[#$fileId]" -and $shortcut.WkDir -eq $nativeDirectory) "Shortcut must launch the native tool in $path"
                }

                $actions = @(Read-MsiTable $database "CustomAction" @("Action", "Type", "Source", "Target"))
                $sequence = @(Read-MsiTable $database "InstallExecuteSequence" @("Action", "Condition", "Sequence"))
                Assert-True (-not $properties.ContainsKey("QIQI_REMOVE_USER_DATA")) "User data deletion must not be enabled by default."
                foreach ($choice in @("QIQI_REMOVE_USER_DATA", "QIQI_REMOVE_CONFIGURATION", "QIQI_REMOVE_DICTIONARIES")) {
                    Assert-True (-not $properties.ContainsKey($choice)) "Cleanup options must retain data by default."
                    Assert-True ($choice -in $properties.SecureCustomProperties.Split(';')) "Uninstall consent must reach the execute sequence."
                }
                $cleanup = @($actions | Where-Object { $_.Action -eq "ExecuteCleanup" })
                Assert-True ($cleanup.Count -eq 1 -and $cleanup[0].Source -eq "InstallerActions" -and $cleanup[0].Target -eq "ExecuteCleanup") "Missing embedded cleanup action."
                Assert-True (([int]$cleanup[0].Type -band 3584) -eq 3584 -and ([int]$cleanup[0].Type -band 64) -ne 0) "Cleanup must be elevated commit without rollback on incomplete cleanup."
                $cleanupSequence = @($sequence | Where-Object { $_.Action -eq "ExecuteCleanup" })
                Assert-True ($cleanupSequence.Count -eq 1 -and $cleanupSequence[0].Condition -eq 'Installed AND REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE AND ExecuteCleanup') "Cleanup must be explicit uninstall with a prepared request."
                $prompt = @($actions | Where-Object { $_.Action -eq "ChooseUninstallData" })
                Assert-True ($prompt.Count -eq 1 -and [int]$prompt[0].Type -eq 1 -and
                    $prompt[0].Source -eq "InstallerActions" -and $prompt[0].Target -eq "ChooseUninstallData") "Missing immediate native MSI wizard action."
                $promptSequence = @($sequence | Where-Object { $_.Action -eq "ChooseUninstallData" })
                Assert-True ($promptSequence.Count -eq 1 -and
                    $promptSequence[0].Condition -eq 'Installed AND REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE') "Wizard must exclude upgrades and non-uninstall operations."
                Assert-True ($properties.ContainsKey("QIQI_WIZARD_CONFIGURATION") -and $properties.ContainsKey("QIQI_WIZARD_DICTIONARIES")) "Missing localized independent uninstall choices."
                $binaryRows = @(Read-MsiTable $database "Binary" @("Name"))
                Assert-True (@($binaryRows | Where-Object { $_.Name -eq "InstallerActions" }).Count -eq 1) "Prompt DLL must be embedded in the MSI."
                $controls = @(Read-MsiTable $database "Control" @("Dialog_", "Type", "Property"))
                Assert-True (@($controls | Where-Object { $_.Dialog_ -eq "UninstallProgressDlg" -and $_.Type -eq "ProgressBar" }).Count -eq 1) "Missing full-UI uninstall progress."
                $uiSequence = @(Read-MsiTable $database "InstallUISequence" @("Action", "Condition", "Sequence"))
                Assert-True (@($uiSequence | Where-Object { $_.Action -eq "ChooseUninstallData" -and $_.Condition -eq 'Installed AND REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE' }).Count -eq 1) "Missing full-UI uninstall choices."
                foreach ($id in @("RegX86", "UnregX86", "EnableUserProfile", "DeactivateUserProfile", "GrantAppContainerAcl", "RegisterConfigBroker", "ShutdownConfigBroker", "UnregisterConfigBroker")) {
                    Assert-True (@($actions | Where-Object { $_.Action -eq $id }).Count -eq 1) "Missing action $id in $path"
                }
                foreach ($action in $actions) {
                    if ($action.Action -in @("EnableUserProfile", "DeactivateUserProfile")) {
                        Assert-True ($action.Source -eq $nativeDirectory -and $action.Target.StartsWith("[$systemDirectory]regsvr32.exe ") -and $action.Target.Contains("[$nativeDirectory]qiqiime.dll")) "User profile action uses the wrong architecture in $path"
                    }
                    if ($action.Action -in @("RegX86", "UnregX86")) {
                        Assert-True ($action.Source -eq "DirX86" -and $action.Target.StartsWith("[SystemFolder]regsvr32.exe ")) "x86 action uses the wrong registration tool in $path"
                    }
                    if ($action.Action -eq "GrantAppContainerAcl") {
                        Assert-True ($action.Target.StartsWith("[$systemDirectory]icacls.exe ")) "ACL action uses the wrong system directory in $path"
                    }
                }
                $ordered = @{}
                foreach ($row in $sequence) { $ordered[$row.Action] = [int]$row.Sequence }
                Assert-True ($ordered.RemoveFiles -lt $ordered.ExecuteCleanup -and $ordered.ExecuteCleanup -lt $ordered.InstallFinalize) "Cleanup must be scheduled after file removal and before commit."
                Assert-True ($ordered.InstallFinalize -lt $ordered.CompleteUninstall) "Cleanup results must be reported after commit."
                Assert-True ($ordered.CheckInstallPendingCleanup -lt $ordered.FindRelatedProducts) "Restart gate must precede upgrade detection/removal."
                Assert-True ($ordered.GrantAppContainerAcl -lt $ordered.RegX86 -and $ordered.RegX86 -lt $ordered.EnableUserProfile) "Registration order changed in $path"
                Assert-True ($ordered.RegX86 -lt $ordered.RegisterConfigBroker) "Broker must register after the native TSF COM registration in $path"
                Assert-True ($ordered.DeactivateUserProfile -lt $ordered.InstallValidate) "Profile must deactivate before lock checks."
                Assert-True ($ordered.ShutdownConfigBroker -lt $ordered.DeactivateUserProfile) "Broker must stop before uninstall lock checks in $path"
                Assert-True ($ordered.ChooseUninstallData -lt $ordered.PrepareUninstall -and $ordered.PrepareUninstall -lt $ordered.DeactivateUserProfile) "Ask before changing the user's active input profile."
                Assert-True ($ordered.InstallInitialize -lt $ordered.UnregX86) "Unregister must run inside the install transaction."
                Assert-True ($ordered.InstallInitialize -lt $ordered.UnregisterConfigBroker) "Broker unregister must run inside the install transaction in $path."
                if ($arch -eq "x64") {
                    Assert-True ($ordered.GrantAppContainerAcl -lt $ordered.RegX64 -and $ordered.RegX64 -lt $ordered.RegX86) "x64 must register before WOW64."
                    Assert-True ($ordered.UnregX86 -lt $ordered.UnregX64) "WOW64 must unregister before x64."
                } else {
                    Assert-True (@($actions | Where-Object { $_.Action -in @("RegX64", "UnregX64") -or $_.Target -match 'System64Folder|DirX64' }).Count -eq 0) "x86 MSI contains x64 actions."
                    Assert-True (-not $ordered.ContainsKey("RegX64") -and -not $ordered.ContainsKey("UnregX64")) "x86 MSI schedules x64 actions."
                    $launch = @(Read-MsiTable $database "LaunchCondition" @("Condition", "Description"))
                    Assert-True (@($launch | Where-Object { $_.Condition -eq "Installed OR NOT VersionNT64" -and -not [string]::IsNullOrWhiteSpace($_.Description) }).Count -eq 1) "x86 MSI must reject new installs on x64 Windows."
                }
                Write-Host "Verified MSI platform, payload, identities, shortcuts and registration: $path"
            } finally {
                [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database)
            }
        }
    }
    Assert-True ($upgradeCodes.x64 -ne $upgradeCodes.x86) "Architectures must have independent upgrade identities."
    Assert-True (@($componentIds.x64 | Where-Object { $_ -in $componentIds.x86 }).Count -eq 0) "Architectures must not share component identities."
} finally {
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
}
