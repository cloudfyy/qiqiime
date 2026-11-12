$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$installerDir = Join-Path $repoRoot "installer"
[xml]$source = Get-Content (Join-Path $installerDir "QiQiIME.wxs") -Raw -Encoding UTF8
$ns = [System.Xml.XmlNamespaceManager]::new($source.NameTable)
$ns.AddNamespace("w", "http://wixtoolset.org/schemas/v4/wxs")
$package = $source.SelectSingleNode("/w:Wix/w:Package", $ns)
Assert-True ($package.Scope -eq "perMachine") "Start menu entries must be installed for all users."
$folder = $package.SelectSingleNode("w:StandardDirectory[@Id='ProgramMenuFolder']/w:Directory[@Id='QiQiProgramMenuFolder']", $ns)
Assert-True ($null -ne $folder -and $folder.Name -eq "QiQiIME") "Missing stable QiQiIME start menu folder."
$component = $package.SelectSingleNode("w:ComponentGroup[@Id='ProductComponents']/w:Component[@Id='CmpStartMenuShortcuts']", $ns)
Assert-True ($null -ne $component) "Missing start menu component."
Assert-True ($null -ne $package.SelectSingleNode("w:Feature/w:ComponentGroupRef[@Id='ProductComponents']", $ns)) "Start menu component is not included in the product feature."
Assert-True ($component.Directory -eq $folder.Id -and $component.Bitness -eq '$(var.ShortcutBitness)') "Incorrect shortcut component directory or bitness."
$keyPath = $component.SelectSingleNode("w:RegistryValue[@KeyPath='yes']", $ns)
Assert-True ($null -ne $keyPath -and $keyPath.Root -eq "HKMU") "Non-advertised shortcuts require an ICE43-compatible key path; HKMU resolves to HKLM in this per-machine package."
Assert-True ($null -ne $component.SelectSingleNode("w:RemoveFolder[@On='uninstall']", $ns)) "The empty start menu folder must be removed on uninstall."
Assert-True (@($component.SelectNodes("w:RemoveFile|w:RemoveRegistryKey", $ns)).Count -eq 0) "Shortcut cleanup must not delete user files or registry keys."

$expected = @(
    @{ Id = "SettingsShortcut"; File = "FileSettingsExe"; Exe = "QiQiIMESettings.exe"; Icon = "ProductIcon" },
    @{ Id = "DictManagerShortcut"; File = "FileDictManagerExe"; Exe = "QiQiIMEDictManager.exe"; Icon = "DictManagerIcon" },
    @{ Id = "LogMonitorShortcut"; File = "FileLogMonitorExe"; Exe = "QiQiIMELogMonitor.exe"; Icon = "LogMonitorIcon" }
)
Assert-True (@($component.SelectNodes("w:Shortcut", $ns)).Count -eq $expected.Count) "Expected exactly three tool shortcuts."
foreach ($entry in $expected) {
    $shortcut = $component.SelectSingleNode("w:Shortcut[@Id='$($entry.Id)']", $ns)
    Assert-True ($null -ne $shortcut) "Missing shortcut $($entry.Id)."
    Assert-True ($shortcut.Target -eq ('[#' + $entry.File + '$(var.NativeFileSuffix)]')) "Shortcut $($entry.Id) must target the native tool file."
    Assert-True ($shortcut.WorkingDirectory -eq '$(var.NativeDirectory)' -and $shortcut.Advertise -eq "no") "Shortcut $($entry.Id) must launch the native tool directly."
    Assert-True ($shortcut.Name -eq "!(loc.$($entry.Id)Name)" -and $shortcut.Description -eq "!(loc.$($entry.Id)Description)") "Shortcut $($entry.Id) must use localized name and description."
    Assert-True ($shortcut.Icon -eq $entry.Icon -and $shortcut.IconIndex -eq "0") "Incorrect icon for $($entry.Id)."
    foreach ($arch in @("x64", "x86")) {
        $file = $package.SelectSingleNode("w:ComponentGroup/w:Component/w:File[@Id='$($entry.File)$($arch.ToUpperInvariant())']", $ns)
        Assert-True ($null -ne $file -and $file.Source.EndsWith("\$arch\$($entry.Exe)")) "Shortcut target $($entry.File) is not packaged from $arch."
    }
    $icon = $package.SelectSingleNode("w:Icon[@Id='$($entry.Icon)']", $ns)
    Assert-True ($null -ne $icon -and (Test-Path -LiteralPath (Join-Path $repoRoot $icon.SourceFile))) "Missing icon source for $($entry.Id)."
}

$referenceIds = $null
$referenceNames = $null
foreach ($culture in @("zh-CN", "en-US")) {
    [xml]$localization = Get-Content (Join-Path $installerDir "$culture.wxl") -Raw -Encoding UTF8
    Assert-True ($localization.WixLocalization.Culture -eq $culture) "Incorrect localization culture in $culture.wxl."
    $strings = @{}
    foreach ($item in $localization.WixLocalization.String) {
        Assert-True (-not $strings.ContainsKey($item.Id)) "Duplicate localization key $($item.Id) in $culture."
        Assert-True (-not [string]::IsNullOrWhiteSpace($item.Value)) "Empty localization key $($item.Id) in $culture."
        $strings[$item.Id] = $item.Value
    }
    foreach ($match in [regex]::Matches($source.OuterXml, '!\(loc\.([^)]+)\)')) {
        Assert-True ($strings.ContainsKey($match.Groups[1].Value)) "Missing localization key $($match.Groups[1].Value) in $culture."
    }
    $ids = @($strings.Keys | Sort-Object)
    if ($null -ne $referenceIds) {
        Assert-True (@(Compare-Object $referenceIds $ids).Count -eq 0) "Installer localization keys must match across languages."
    }
    $referenceIds = $ids
    $names = @($expected | ForEach-Object { $strings["$($_.Id)Name"] })
    Assert-True (@($names | Sort-Object -Unique).Count -eq $expected.Count) "Tool shortcut names must be distinct in $culture."
    if ($null -ne $referenceNames) {
        for ($i = 0; $i -lt $names.Count; $i++) {
            Assert-True ($referenceNames[$i] -ne $names[$i]) "Shortcut $($expected[$i].Id) must have different names in Chinese and English."
        }
    }
    $referenceNames = $names
}

Write-Host "Installer shortcuts and both language resources passed."
& (Join-Path $PSScriptRoot "symbols.ps1")
& (Join-Path $PSScriptRoot "documentation.ps1")
