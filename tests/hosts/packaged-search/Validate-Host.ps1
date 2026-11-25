# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [string] $PackagePath,
    [string] $ExpectedPublisher,
    [string] $ExpectedSignerThumbprint
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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

$manifestPath = Join-Path $PSScriptRoot 'Package.appxmanifest'
$fixturePath = Join-Path $PSScriptRoot 'TestData.json'
$sourceCertificatePath = Join-Path $PSScriptRoot `
    'cert\QiQiImeDevelopment.cer'
$manifest = [xml](Get-Content -LiteralPath $manifestPath -Raw)

$identity = $manifest.SelectSingleNode("//*[local-name()='Identity']")
Assert-HostCondition ($null -ne $identity) 'Package identity is missing.'
Assert-HostCondition (
    $identity.GetAttribute('ProcessorArchitecture') -eq 'x64'
) 'The development host manifest must target x64.'
Assert-HostCondition (
    $manifest.SelectNodes("//*[local-name()='Capability']").Count -eq 0
) 'The AppContainer host must not request capabilities.'
Assert-HostCondition (
    $manifest.SelectNodes("//*[contains(local-name(), 'DeviceCapability')]").Count -eq 0
) 'The AppContainer host must not request device capabilities.'
Assert-HostCondition (
    $manifest.SelectNodes("//*[local-name()='Extension' and @Category='windows.search']").Count -eq 1
) 'The AppContainer host must declare exactly one windows.search extension.'
Assert-HostCondition (
    $null -eq $manifest.SelectSingleNode("//*[@RuntimeBehavior='windowsApp' or @runFullTrust='true']")
) 'The host must remain an AppContainer app and cannot declare runFullTrust.'
Assert-HostCondition (
    $manifest.SelectSingleNode("//*[local-name()='Application']").GetAttribute('Executable') `
        -eq 'QiQiImeAppContainerSearchHost.exe'
) 'The host executable name in the manifest is unexpected.'
Assert-HostCondition (
    Test-Path -LiteralPath $sourceCertificatePath -PathType Leaf
) 'The public development certificate is missing from the source directory.'

$sourceCertificate = [
    Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $sourceCertificatePath)
try {
    Assert-HostCondition (
        $sourceCertificate.Subject -eq $identity.GetAttribute('Publisher')
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

    $signature = Get-AuthenticodeSignature -FilePath $PackagePath
    Assert-HostCondition (
        $null -ne $signature.SignerCertificate
    ) 'The package has no signer certificate.'
    Assert-HostCondition (
        $signature.Status -ne [System.Management.Automation.SignatureStatus]::HashMismatch
    ) 'The signed package has a hash mismatch.'

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
            'QiQiImeAppContainerSearchHost.exe'
        $packagedFixture = Join-Path `
            $unpackedRoot `
            'Assets\TestData.json'
        Assert-HostCondition (
            Test-Path -LiteralPath $packagedExecutable -PathType Leaf
        ) 'The package does not contain QiQiImeAppContainerSearchHost.exe.'
        Assert-HostCondition (
            Test-Path -LiteralPath $packagedFixture -PathType Leaf
        ) 'The package does not contain Assets\TestData.json.'

        $packageManifest = [xml](Get-Content -LiteralPath (
            Join-Path $unpackedRoot 'AppxManifest.xml') -Raw)
        $packageIdentity = $packageManifest.SelectSingleNode(
            "//*[local-name()='Identity']")
        $publisher = if ($ExpectedPublisher) {
            $ExpectedPublisher
        } else {
            $packageIdentity.GetAttribute('Publisher')
        }
        Assert-HostCondition (
            $signature.SignerCertificate.Subject -eq $publisher
        ) "The package signer '$($signature.SignerCertificate.Subject)' does not match the package manifest publisher '$publisher'."
        if ($ExpectedSignerThumbprint) {
            Assert-HostCondition (
                $signature.SignerCertificate.Thumbprint -eq
                    $ExpectedSignerThumbprint
            ) 'The package signer thumbprint does not match the selected signing certificate.'
        }
    } finally {
        Remove-Item -LiteralPath $unpackedRoot -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}

Write-Host 'AppContainer host validation passed.'
