# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Rejected([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try {
        & $Action
    } catch {
        if ($_.Exception.Message -ne $Message) { throw }
        $rejected = $true
    }
    Assert-True $rejected "Expected rejection: $Message"
}

$testState = [pscustomobject]@{
    Profiles = @()
    Checked = [Collections.Generic.List[string]]::new()
    Installed = [Collections.Generic.List[string]]::new()
    TrustQueries = 0
    RejectProfile = ''
    RejectInstallProfile = ''
    Publisher = ''
    Thumbprint = ''
}

function Reset-TestState([string[]]$Profiles) {
    $testState.Profiles = $Profiles
    $testState.Checked.Clear()
    $testState.Installed.Clear()
    $testState.TrustQueries = 0
    $testState.RejectProfile = ''
    $testState.RejectInstallProfile = ''
}

function Test-Path {
    [CmdletBinding()]
    param([string]$LiteralPath, [string]$PathType)
    if ($LiteralPath.StartsWith('Cert:\LocalMachine\TrustedPeople\')) {
        Assert-True (($testState.Checked -join ',') -eq ($testState.Profiles -join ',')) `
            'All selected packages must be checked before querying certificate trust.'
        $testState.TrustQueries++
        return $true
    }
    Microsoft.PowerShell.Management\Test-Path @PSBoundParameters
}

function Import-Certificate {
    [CmdletBinding()]
    param([string]$FilePath, [string]$CertStoreLocation)
    throw 'Real certificate import is forbidden in this fixture.'
}

function Add-AppxPackage {
    [CmdletBinding()]
    param([string]$Path)
    Assert-True (($testState.Checked -join ',') -eq ($testState.Profiles -join ',')) `
        'All selected packages must be checked before installing any app.'
    if ($testState.Profiles[$testState.Installed.Count] -eq $testState.RejectInstallProfile) {
        throw 'Synthetic installation failed.'
    }
    $testState.Installed.Add($Path)
}

function Get-AppxPackage {
    [CmdletBinding()]
    param([string]$Name)
    $expected = 'Cloudfyy.QiQiUwp.' + $testState.Profiles[$testState.Installed.Count - 1]
    Assert-True ($Name -eq $expected) 'Registration must be checked for the installed profile.'
    [pscustomobject]@{
        Publisher = $testState.Publisher
        PackageFullName = $Name + '_fixture'
        PackageFamilyName = $Name + '_fixture'
    }
}

$fixture = Join-Path ([IO.Path]::GetTempPath()) ('qiqi-uwp-install-' + [guid]::NewGuid().ToString('N'))
try {
    $scripts = Join-Path $fixture 'tests\hosts\uwp'
    $dist = Join-Path $fixture 'dist\test-hosts'
    New-Item -ItemType Directory -Path $scripts, $dist -Force | Out-Null
    foreach ($name in 'Install.ps1', 'Profile.ps1', 'dev.cer') {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $scripts
    }
    foreach ($name in 'zero', 'com', 'reg') {
        $directory = Join-Path $scripts $name
        New-Item -ItemType Directory -Path $directory | Out-Null
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "$name\app.psd1") -Destination $directory
        [IO.File]::WriteAllText((Join-Path $dist "uwp-$name-0.1.0.0.msix"), '')
    }
    [IO.File]::WriteAllText((Join-Path $scripts 'Check.ps1'), @'
[CmdletBinding()]
param([string]$Profile, [string]$PackagePath, [string]$ExpectedSignerThumbprint)
if ($ExpectedSignerThumbprint -ne $testState.Thumbprint) { throw 'Unexpected signer.' }
$testState.Checked.Add($Profile)
if ($Profile -eq $testState.RejectProfile) { throw 'Synthetic package validation failed.' }
'@)
    $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new((Join-Path $scripts 'dev.cer'))
    try {
        $testState.Publisher = $certificate.Subject
        $testState.Thumbprint = $certificate.Thumbprint
    } finally {
        $certificate.Dispose()
    }
    $installer = Join-Path $scripts 'Install.ps1'

    Reset-TestState @('zero', 'com', 'reg')
    & $installer
    Assert-True (($testState.Checked -join ',') -eq 'zero,com,reg') 'Default installation must check all three profiles.'
    Assert-True ($testState.Installed.Count -eq 3 -and $testState.TrustQueries -eq 1) `
        'Default installation must install three apps with one trust check.'

    Reset-TestState @('com')
    & $installer -Profile com
    Assert-True ($testState.Installed.Count -eq 1 -and $testState.Checked[0] -eq 'com') `
        'An explicit profile must install only that app.'

    $customPackage = Join-Path $fixture 'custom-reg.msix'
    [IO.File]::WriteAllText($customPackage, '')
    Reset-TestState @('reg')
    & $installer -Profile reg -PackagePath $customPackage
    Assert-True ($testState.Installed.Count -eq 1 -and $testState.Installed[0] -eq $customPackage) `
        'An individual profile must retain its explicit package path.'

    Reset-TestState @('zero', 'com', 'reg')
    Assert-Rejected { & $installer -PackagePath $customPackage } 'Use -Profile zero, com, or reg with -PackagePath.'
    Assert-True ($testState.Checked.Count -eq 0 -and $testState.TrustQueries -eq 0 -and $testState.Installed.Count -eq 0) `
        'An ambiguous package path must fail without side effects.'

    Reset-TestState @('zero', 'com', 'reg')
    $testState.RejectProfile = 'reg'
    Assert-Rejected { & $installer } 'Synthetic package validation failed.'
    Assert-True ($testState.Checked.Count -eq 3 -and $testState.TrustQueries -eq 0 -and $testState.Installed.Count -eq 0) `
        'A later validation failure must leave certificate trust and every app untouched.'

    Reset-TestState @('zero', 'com', 'reg')
    $testState.RejectInstallProfile = 'com'
    Assert-Rejected { & $installer } 'Synthetic installation failed.'
    Assert-True ($testState.Checked.Count -eq 3 -and $testState.TrustQueries -eq 1 -and $testState.Installed.Count -eq 1) `
        'An installation failure must stop the batch without claiming success or removing installed apps.'

    Reset-TestState @('zero', 'com', 'reg')
    Remove-Item -LiteralPath (Join-Path $dist 'uwp-reg-0.1.0.0.msix')
    Assert-Rejected { & $installer } 'No package found for UWP reg.'
    Assert-True ($testState.Checked.Count -eq 0 -and $testState.TrustQueries -eq 0 -and $testState.Installed.Count -eq 0) `
        'A missing package must fail before validation or installation starts.'

    Write-Host 'UWP default-all installation and side-effect-free preflight regressions passed.'
} finally {
    if (Test-Path -LiteralPath $fixture) {
        Remove-Item -LiteralPath (Resolve-Path -LiteralPath $fixture).Path -Recurse -Force
    }
}
