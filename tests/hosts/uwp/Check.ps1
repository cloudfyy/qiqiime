# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [ValidateSet('zero', 'com', 'reg')]
    [string] $Profile = 'zero',
    [string] $PackagePath,
    [string] $ExpectedSignerThumbprint,
    [switch] $AllowUnsigned
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Profile.ps1')
$app = Get-QiQiUwpProfile -Name $Profile

function Assert-HostCondition {
    param(
        [Parameter(Mandatory = $true)]
        [bool] $Condition,
        [Parameter(Mandatory = $true)]
        [string] $Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Get-StringMatches {
    param(
        [Parameter(Mandatory = $true)]
        [object[]] $Terms,
        [Parameter(Mandatory = $true)]
        [string] $Query,
        [AllowEmptyCollection()]
        [object[]] $Alternatives = @()
    )
    $values = @($Query) + @($Alternatives)
    foreach ($term in $Terms) {
        foreach ($value in $values) {
            if ([string]::Equals(
                    [string]$term,
                    [string]$value,
                    [StringComparison]::OrdinalIgnoreCase)) {
                return $true
            }
        }
    }
    return $false
}

$fixturePath = Join-Path $PSScriptRoot 'TestData.json'
$sourceCertificatePath = Join-Path $PSScriptRoot 'dev.cer'

function Assert-QiQiUwpManifest {
    param([xml] $Manifest, $App)

    $identity = $Manifest.SelectSingleNode("//*[local-name()='Identity']")
    Assert-HostCondition (
        $identity -and $identity.GetAttribute('Name') -ceq $App.Identity -and
        $identity.GetAttribute('ProcessorArchitecture') -ceq 'x64'
    ) 'Package identity or processor architecture does not match the test profile.'
    $capabilities = $Manifest.SelectNodes("//*[local-name()='Capabilities']/*")
    $expectedCount = [int] (![string]::IsNullOrEmpty($App.Capability))
    Assert-HostCondition ($capabilities.Count -eq $expectedCount) 'Unexpected capability count.'
    if ($expectedCount) {
        $capability = $capabilities.Item(0)
        Assert-HostCondition (
            $capability.LocalName -ceq 'Capability' -and
            $capability.NamespaceURI -ceq 'http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities' -and
            $capability.GetAttribute('Name') -ceq $App.Capability
        ) 'Unexpected restricted capability.'
    }
    Assert-HostCondition (
        $Manifest.SelectNodes("//*[local-name()='Extension' and @Category='windows.search']").Count -eq 1
    ) 'The AppContainer host must declare exactly one windows.search extension.'
    $entry = $Manifest.SelectSingleNode("//*[local-name()='Application']")
    Assert-HostCondition (
        $entry -and $entry.GetAttribute('Executable') -ceq "$($App.Binary).exe" -and
        $entry.GetAttribute('EntryPoint') -ceq 'QiQiImeAppContainerSearchHost.App' -and
        !$entry.HasAttribute('TrustLevel', 'http://schemas.microsoft.com/appx/manifest/uap/windows10/10')
    ) 'The test executable or its AppContainer entry point is incorrect.'
}

$manifest = New-QiQiUwpManifest -Profile $app -Version '0.1.0.0'
Assert-QiQiUwpManifest -Manifest $manifest -App $app
Assert-HostCondition (
    Test-Path -LiteralPath $sourceCertificatePath -PathType Leaf
) 'The public development certificate is missing from the source directory.'

$sourceCertificate = [
    Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $sourceCertificatePath)
try {
    Assert-HostCondition (
        $sourceCertificate.Subject -eq $manifest.SelectSingleNode("//*[local-name()='Identity']").GetAttribute('Publisher')
    ) 'The source development certificate subject must match the manifest publisher.'
    Assert-HostCondition (
        -not $sourceCertificate.HasPrivateKey
    ) 'The source development certificate must not contain a private key.'
    $codeSigningUsage = @(
        $sourceCertificate.Extensions |
        Where-Object {
            $_.Oid -and $_.Oid.Value -eq '2.5.29.37'
        } |
        ForEach-Object { $_.Format($false) }
    ) -join ';'
    Assert-HostCondition (
        $codeSigningUsage -match '1\.3\.6\.1\.5\.5\.7\.3\.3'
    ) 'The source development certificate lacks the code-signing EKU.'
} finally {
    $sourceCertificate.Dispose()
}

$fixture = Get-Content -LiteralPath $fixturePath -Raw | ConvertFrom-Json
Assert-HostCondition ($fixture.records.Count -gt 0) 'Fixture records are empty.'
Assert-HostCondition ($fixture.fixtures.Count -gt 0) 'Synthetic fixtures are empty.'

$recordIds = @($fixture.records | ForEach-Object { [string]$_.id })
Assert-HostCondition (
    @($recordIds | Select-Object -Unique).Count -eq $recordIds.Count
) 'Fixture record IDs must be unique.'

$fixtureIds = @($fixture.fixtures | ForEach-Object { [string]$_.id })
Assert-HostCondition (
    @($fixtureIds | Select-Object -Unique).Count -eq $fixtureIds.Count
) 'Synthetic fixture IDs must be unique.'

foreach ($case in $fixture.fixtures) {
    $queryLength = ([string]$case.query).Length
    $compositionStart = [int]$case.compositionStart
    $compositionLength = [int]$case.compositionLength
    Assert-HostCondition (
        $compositionStart -ge 0 -and
        $compositionStart -le $queryLength -and
        $compositionLength -ge 0 -and
        $compositionLength -le ($queryLength - $compositionStart)
    ) "Fixture '$($case.id)' has an invalid composition span."

    $matches = @(
        $fixture.records | Where-Object {
            Get-StringMatches `
                -Terms @($_.terms) `
                -Query ([string]$case.query) `
                -Alternatives @($case.alternatives)
        }
    ).Count
    Assert-HostCondition (
        $matches -eq [int]$case.expectedMatches
    ) "Fixture '$($case.id)' expects $($case.expectedMatches) matches but resolves to $matches."
}

if ($PackagePath) {
    Assert-HostCondition (
        Test-Path -LiteralPath $PackagePath -PathType Leaf
    ) "Package does not exist: $PackagePath"

    if (!$AllowUnsigned) {
        $signature = Get-AuthenticodeSignature -FilePath $PackagePath
        Assert-HostCondition ($null -ne $signature.SignerCertificate) 'The package has no signer certificate.'
        Assert-HostCondition (
            $signature.Status -ne [System.Management.Automation.SignatureStatus]::HashMismatch
        ) 'The signed package has a hash mismatch.'
    }

    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
    $makeAppx = Get-ChildItem -LiteralPath $sdkRoot `
        -Recurse `
        -Filter 'makeappx.exe' `
        -File `
        -ErrorAction Stop |
        Where-Object FullName -like '*\x64\makeappx.exe' |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    Assert-HostCondition ($null -ne $makeAppx) 'makeappx.exe was not found.'

    $unpackedRoot = Join-Path $env:TEMP (
        'qiqi-appcontainer-validation-' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $unpackedRoot -Force | Out-Null
        & $makeAppx.FullName unpack /p $PackagePath /d $unpackedRoot /o
        if ($LASTEXITCODE -ne 0) {
            throw "The package cannot be unpacked by makeappx.exe (exit code $LASTEXITCODE)."
        }
        $packagedExecutable = Join-Path `
            $unpackedRoot `
            "$($app.Binary).exe"
        $packagedFixture = Join-Path `
            $unpackedRoot `
            'Assets\TestData.json'
        Assert-HostCondition (
            Test-Path -LiteralPath $packagedExecutable -PathType Leaf
        ) "The package does not contain $($app.Binary).exe."
        Assert-HostCondition (
            Test-Path -LiteralPath $packagedFixture -PathType Leaf
        ) 'The package does not contain Assets\TestData.json.'

        $packageManifest = [xml](Get-Content -LiteralPath (
            Join-Path $unpackedRoot 'AppxManifest.xml') -Raw)
        Assert-QiQiUwpManifest -Manifest $packageManifest -App $app
        foreach ($asset in 'store.png', 'small.png', 'tile.png', 'wide.png', 'splash.png') {
            Assert-HostCondition (Test-Path -LiteralPath (Join-Path $unpackedRoot "Assets\$asset") -PathType Leaf) `
                "The package is missing $asset."
        }
        if (!$AllowUnsigned) {
            $publisher = $packageManifest.SelectSingleNode("//*[local-name()='Identity']").GetAttribute('Publisher')
            Assert-HostCondition (
                $signature.SignerCertificate.Subject -eq $publisher
            ) 'The package signer does not match the manifest publisher.'
            if ($ExpectedSignerThumbprint) {
                Assert-HostCondition ($signature.SignerCertificate.Thumbprint -eq $ExpectedSignerThumbprint) `
                    'The package signer thumbprint does not match the selected certificate.'
            }
        }
    } finally {
        Remove-Item -LiteralPath (Resolve-Path -LiteralPath $unpackedRoot).Path -Recurse -Force
    }
}

Write-Host "UWP $($app.Name) manifest and fixture verified."
