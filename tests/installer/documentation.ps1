# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "..\..\scripts\common.ps1")

function Test-MsiLoggingExample(
    [string]$Code,
    [ValidateSet('install', 'uninstall')][string]$Operation,
    [string]$PackageExtension = '.msi',
    [int]$ProductCount = 1,
    [int]$ExitCode = 0,
    [bool]$LogExists = $true,
    [string]$ExpectedFailure = ''
) {
    $state = [pscustomobject]@{ Arguments = ''; Log = ''; Viewed = ''; Launches = 0 }
    $product = '{11111111-2222-3333-4444-555555555555}'
    $packagePath = 'C:\QiQi documentation fixture\trusted.msi'
    function Read-Host { return '"' + $packagePath + '"' }
    function Get-Item {
        [CmdletBinding()]
        param([string]$LiteralPath)
        if ($LiteralPath -ne $packagePath) { throw 'MSI example did not preserve the quoted input path.' }
        return [pscustomobject]@{
            FullName = $packagePath
            Extension = $PackageExtension
            PSIsContainer = $false
        }
    }
    function Test-Path {
        param([string]$LiteralPath, [string]$PathType)
        if ($LiteralPath.StartsWith('HKLM:\')) { return $true }
        return $LogExists -and $LiteralPath -eq $state.Log -and $PathType -eq 'Leaf'
    }
    function Get-ItemProperty {
        [CmdletBinding()]
        param([string]$Path)
        if (-not $Path.StartsWith('HKLM:\')) { throw 'Unexpected registry query in MSI example.' }
        if ($Path.Contains('WOW6432Node')) { return }
        [pscustomobject]@{ PSChildName = 'unrelated-record-without-display-name' }
        for ($index = 0; $index -lt $ProductCount; ++$index) {
            [pscustomobject]@{
                DisplayName = 'QiQi Wubi IME'
                DisplayVersion = '1.1.1'
                WindowsInstaller = 1
                PSChildName = $product
            }
        }
    }
    function Start-Process {
        param([string]$FilePath, [string]$ArgumentList, [switch]$Wait, [switch]$PassThru)
        if ($FilePath -ne (Join-Path $env:SystemRoot 'System32\msiexec.exe') -or -not $Wait -or -not $PassThru) {
            throw 'MSI examples must wait for the system installer and retain its exit code.'
        }
        if ($ArgumentList -notmatch ' /norestart /L\*V! "([^"]+)"$') {
            throw 'MSI examples must prohibit automatic restarts and request flushed verbose logs.'
        }
        $state.Log = $Matches[1]
        $state.Arguments = $ArgumentList
        ++$state.Launches
        return [pscustomobject]@{ ExitCode = $ExitCode }
    }
    function notepad.exe {
        param([string]$Path)
        if ($Path -ne $state.Log) { throw 'Viewer must open the log returned by the installer example.' }
        $state.Viewed = $Path
    }
    $failed = $false
    $output = @()
    try {
        $output = @(& ([scriptblock]::Create($Code)))
    } catch {
        if (-not $ExpectedFailure -or $_.Exception.Message -notmatch $ExpectedFailure) { throw }
        $failed = $true
    }
    if ($failed -ne [bool]$ExpectedFailure) { throw 'MSI example did not reject the expected invalid state.' }
    $shouldLaunch = $PackageExtension -eq '.msi' -and ($Operation -eq 'install' -or $ProductCount -eq 1)
    if ($state.Launches -ne [int]$shouldLaunch) { throw 'MSI example launched in an invalid state.' }
    if (-not $shouldLaunch) { return }
    $expectedCommand = if ($Operation -eq 'install') { '/i "' + $packagePath + '"' } else { "/x $product /qb" }
    if (-not $state.Arguments.StartsWith($expectedCommand + ' ') -or
        $state.Arguments.Contains('QIQI_REMOVE_')) {
        throw 'MSI example changed the requested operation or forced a personal-data cleanup choice.'
    }
    $exitLine = @($output | Where-Object { "$_" -match "^\u9000\u51FA\u7801\uFF1A$ExitCode$" })
    if ($LogExists -and ($state.Viewed -ne $state.Log -or $exitLine.Count -ne 1)) {
        throw 'MSI example must show the real exit code and open its generated log, including failure logs.'
    }
    if (-not $LogExists -and $state.Viewed) { throw 'MSI example must not open a nonexistent log.' }
}

$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ("qiqi-documentation-" + [guid]::NewGuid().ToString("N"))
$expected = @(
    "LICENSE",
    "README.md",
    "README.en.md",
    "PRIVACY.md",
    "CODE_SIGNING.md",
    "THIRD_PARTY_NOTICES.md",
    "install.md",
    "log.md",
    "release-notes.md",
    "licenses\Apache-2.0.txt",
    "licenses\rime-wubi86-jidian-MIT.txt",
    "licenses\wubi-haifeng-BSD-3-Clause.txt",
    "licenses\microsoft-gsl-MIT.txt",
    "licenses\nlohmann-json-MIT.txt",
    "licenses\miniz-MIT.txt"
)
try {
    $destination = Join-Path $fixture "stage"
    Copy-QiQiReleaseDocumentation -Destination $destination
    $actual = @(Get-ChildItem -LiteralPath $destination -Recurse -File)
    if ($actual.Count -ne $expected.Count) { throw "Unexpected number of staged documentation files." }
    foreach ($relativePath in $expected) {
        $sourceRelativePath = if ($relativePath -in @("install.md", "log.md", "release-notes.md")) {
            "docs\$relativePath"
        } else {
            $relativePath
        }
        $source = Join-Path $QiQiRepoRoot $sourceRelativePath
        $target = Join-Path $destination $relativePath
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            throw "Missing staged documentation: $relativePath"
        }
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
            throw "Documentation changed during staging: $relativePath"
        }
    }

    [xml]$installer = Get-Content (Join-Path $QiQiRepoRoot "installer\QiQiIME.wxs") -Raw -Encoding UTF8
    $ns = [System.Xml.XmlNamespaceManager]::new($installer.NameTable)
    $ns.AddNamespace("w", "http://wixtoolset.org/schemas/v4/wxs")
    $directory = $installer.SelectSingleNode("//w:Directory[@Id='DirLicenses']", $ns)
    if ($null -eq $directory -or $directory.Name -ne "licenses" -or $directory.ParentNode.Id -ne "INSTALLFOLDER") {
        throw "MSI licenses must be installed under INSTALLFOLDER\licenses."
    }
    $components = $installer.SelectNodes("//w:ComponentGroup[@Id='ProductComponents']/w:Component[@Id='CmpDocs' or @Id='CmpLicenses']", $ns)
    $msiPaths = @(
        foreach ($component in $components) {
            $expectedDirectory = if ($component.Id -eq "CmpDocs") { "INSTALLFOLDER" } else { "DirLicenses" }
            if ($component.Directory -ne $expectedDirectory) { throw "Incorrect MSI documentation component directory." }
            foreach ($file in $component.SelectNodes("w:File", $ns)) {
                $prefix = '$(var.StageDir)\'
                if (-not $file.Source.StartsWith($prefix, [StringComparison]::Ordinal)) {
                    throw "MSI documentation must use the release staging directory."
                }
                $file.Source.Substring($prefix.Length)
            }
        }
    )
    if (@(Compare-Object $expected $msiPaths).Count -ne 0) {
        throw "MSI and ZIP staging must contain the same required documentation and licenses."
    }

    $sourceFixture = Join-Path $fixture "source"
    foreach ($relativePath in $expected) {
        $sourceRelativePath = if ($relativePath -in @("install.md", "log.md", "release-notes.md")) {
            "docs\$relativePath"
        } else {
            $relativePath
        }
        $target = Join-Path $sourceFixture $sourceRelativePath
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $destination $relativePath) -Destination $target
    }
    $missing = Join-Path $sourceFixture "licenses\wubi-haifeng-BSD-3-Clause.txt"
    Remove-Item -LiteralPath $missing
    $rejected = $false
    try {
        Copy-QiQiReleaseDocumentation -Destination $destination -SourceDirectory $sourceFixture
    } catch {
        if ($_.Exception.Message -ne "Missing release documentation: $missing") { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Missing license must fail even when an older staged copy exists." }
    $header = [System.Collections.Generic.List[string]]::new()
    $inLicense = $false
    foreach ($line in (Get-Content -LiteralPath (Join-Path $QiQiRepoRoot "data\wubi86_haifeng.txt") -Encoding UTF8)) {
        if ($line.StartsWith("# Copyright (c) 2010, SUN Haifeng", [StringComparison]::Ordinal)) {
            $inLicense = $true
        }
        if ($inLicense) {
            if (-not $line.StartsWith("#", [StringComparison]::Ordinal)) { break }
            $text = $line.Substring(1)
            if ($text.StartsWith(" ", [StringComparison]::Ordinal)) { $text = $text.Substring(1) }
            $header.Add($text)
        }
    }
    $bsd = Get-Content -LiteralPath (Join-Path $QiQiRepoRoot "licenses\wubi-haifeng-BSD-3-Clause.txt") -Raw -Encoding UTF8
    $expectedNotice = $bsd.Replace("`r`n", "`n").TrimEnd()
    if (-not [string]::Equals(($header -join "`n").TrimEnd(), $expectedNotice, [StringComparison]::Ordinal)) {
        throw "Haifeng dictionary header must retain the complete BSD copyright, conditions and disclaimer."
    }
    $guide = Get-Content -LiteralPath (Join-Path $QiQiRepoRoot 'docs\install.md') -Raw -Encoding UTF8
    $examples = @{}
    foreach ($operation in @('install', 'uninstall')) {
        $matches = @([regex]::Matches($guide, '(?s)```powershell\r?\n(.*?)\r?\n```') |
            Where-Object { $_.Groups[1].Value.Contains("'QiQiIME-$operation-") })
        if ($matches.Count -ne 1) { throw "Expected one documented MSI logging example: $operation" }
        $code = $matches[0].Groups[1].Value
        $tokens = $null
        $errors = $null
        [void][System.Management.Automation.Language.Parser]::ParseInput($code, [ref]$tokens, [ref]$errors)
        if ($errors.Count -ne 0) { throw "Invalid documented PowerShell example: $operation" }
        $examples[$operation] = $code
    }
    Test-MsiLoggingExample $examples.install 'install'
    Test-MsiLoggingExample $examples.install 'install' -PackageExtension '.txt' -ExpectedFailure 'MSI'
    Test-MsiLoggingExample $examples.install 'install' -LogExists $false -ExpectedFailure '\u672A\u751F\u6210\u65E5\u5FD7'
    Test-MsiLoggingExample $examples.uninstall 'uninstall' -ExitCode 1603
    Test-MsiLoggingExample $examples.uninstall 'uninstall' -ProductCount 0 -ExpectedFailure '\u6CA1\u6709\u627E\u5230'
    Test-MsiLoggingExample $examples.uninstall 'uninstall' -ProductCount 2 -ExpectedFailure '\u591A\u4E2A\u5B89\u88C5\u8BB0\u5F55'
    Write-Host 'Documented MSI install/uninstall logging examples passed with mocked process and registry access.'
    Write-Host "Documentation staging, MSI payload parity, missing-license and dictionary-notice checks passed."
} finally {
    if (Test-Path -LiteralPath $fixture) {
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
