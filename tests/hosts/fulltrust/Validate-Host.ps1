# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [string] $PackagePath,
    [string] $ExecutablePath
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

function Test-AppContainerPeFlag {
    param([Parameter(Mandatory = $true)][string] $Path)

    $stream = [IO.File]::OpenRead($Path)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        try {
            Assert-HostCondition (
                $reader.ReadUInt16() -eq 0x5A4D
            ) 'The host is not an MZ executable.'
            $stream.Position = 0x3C
            $peOffset = $reader.ReadInt32()
            $stream.Position = $peOffset
            Assert-HostCondition (
                $reader.ReadUInt32() -eq 0x00004550
            ) 'The host does not contain a PE signature.'
            $stream.Position = $peOffset + 4 + 20
            $magic = $reader.ReadUInt16()
            Assert-HostCondition (
                $magic -eq 0x20B
            ) 'The full-trust host must be a PE32+ x64 executable.'
            $stream.Position = $peOffset + 24 + 70
            $dllCharacteristics = $reader.ReadUInt16()
            return (
                ($dllCharacteristics -band 0x1000) -ne 0
            )
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

$manifestPath = Join-Path $PSScriptRoot 'Package.appxmanifest'
$certificatePath = Join-Path $PSScriptRoot `
    '..\uwp\dev.cer'
$manifest = [xml](Get-Content -LiteralPath $manifestPath -Raw)
$identity = $manifest.SelectSingleNode(
    "//*[local-name()='Identity']")
$application = $manifest.SelectSingleNode(
    "//*[local-name()='Application']")

Assert-HostCondition (
    $null -ne $identity
) 'Package identity is missing.'
Assert-HostCondition (
    $application.GetAttribute('EntryPoint') -eq
        'Windows.FullTrustApplication'
) 'The host must use the Windows.FullTrustApplication entry point.'
Assert-HostCondition (
    $manifest.SelectNodes(
        "//*[local-name()='Capability' and @Name='runFullTrust']").Count -eq 1
) 'The host must declare runFullTrust exactly once.'
Assert-HostCondition (
    $manifest.SelectNodes(
        "//*[local-name()='Capability' and @Name!='runFullTrust']").Count -eq 0
) 'The test host should not request additional package capabilities.'
Assert-HostCondition (
    $manifest.SelectNodes(
        "//*[local-name()='RegistryWriteVirtualization' or " +
        "local-name()='FilesystemWriteVirtualization' or " +
        "local-name()='ExcludedKey' or " +
        "local-name()='ExcludedDirectory']").Count -eq 0
) 'Registry and filesystem virtualization must remain at their default behavior.'
Assert-HostCondition (
    $manifest.SelectSingleNode(
        "//*[local-name()='Capability' and @Name='unvirtualizedResources']") `
        -eq $null
) 'The host cannot declare unvirtualizedResources.'
Assert-HostCondition (
    $manifest.SelectNodes(
        "//*[local-name()='TargetDeviceFamily' " +
        "and @Name='Windows.Desktop']").Count -eq 1
) 'The host must target the Windows Desktop device family.'

$sourceCertificate = [
    Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $certificatePath)
try {
    Assert-HostCondition (
        -not $sourceCertificate.HasPrivateKey
    ) 'The shared development certificate cannot contain a private key.'
    Assert-HostCondition (
        $sourceCertificate.Subject -eq
            $identity.GetAttribute('Publisher')
    ) 'The shared certificate subject must match the host publisher.'
} finally {
    $sourceCertificate.Dispose()
}

if ($ExecutablePath) {
    Assert-HostCondition (
        Test-Path -LiteralPath $ExecutablePath -PathType Leaf
    ) "Executable not found: $ExecutablePath"
    Assert-HostCondition (
        -not (Test-AppContainerPeFlag -Path $ExecutablePath)
    ) 'The full-trust executable cannot have the PE AppContainer flag.'
}

if ($PackagePath) {
    Assert-HostCondition (
        Test-Path -LiteralPath $PackagePath -PathType Leaf
    ) "Package not found: $PackagePath"
    $signature = Get-AuthenticodeSignature -FilePath $PackagePath
    Assert-HostCondition (
        $null -ne $signature.SignerCertificate
    ) 'The package has no signer certificate.'

    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
    $makeAppx = Get-ChildItem -LiteralPath $sdkRoot `
        -Recurse `
        -Filter 'makeappx.exe' `
        -File |
        Where-Object FullName -like '*\x64\makeappx.exe' |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    Assert-HostCondition (
        $null -ne $makeAppx
    ) 'makeappx.exe was not found.'

    $unpackedRoot = Join-Path $env:TEMP (
        'qiqi-fulltrust-validation-' +
        [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $unpackedRoot -Force |
            Out-Null
        & $makeAppx.FullName unpack /p $PackagePath /d $unpackedRoot /o
        if ($LASTEXITCODE -ne 0) {
            throw (
                'The package cannot be unpacked by makeappx.exe ' +
                "(exit code $LASTEXITCODE).")
        }
        $packagedExecutable = Join-Path $unpackedRoot `
            'QiQiImePackagedFullTrustHost.exe'
        Assert-HostCondition (
            Test-Path -LiteralPath $packagedExecutable -PathType Leaf
        ) 'The package does not contain QiQiImePackagedFullTrustHost.exe.'
        Assert-HostCondition (
            -not (Test-AppContainerPeFlag -Path $packagedExecutable)
        ) 'The packaged executable unexpectedly has the AppContainer flag.'
        $packageManifest = [xml](Get-Content -LiteralPath (
            Join-Path $unpackedRoot 'AppxManifest.xml') -Raw)
        $packageApplication = $packageManifest.SelectSingleNode(
            "//*[local-name()='Application']")
        Assert-HostCondition (
            $packageApplication.GetAttribute('EntryPoint') -eq
                'Windows.FullTrustApplication'
        ) 'The packaged application entry point is not full trust.'
        $certificate = [
            Security.Cryptography.X509Certificates.X509Certificate2]::new(
                $certificatePath)
        try {
            Assert-HostCondition (
                $signature.SignerCertificate.Thumbprint -eq
                    $certificate.Thumbprint
            ) 'The package was not signed by the shared development certificate.'
        } finally {
            $certificate.Dispose()
        }
    } finally {
        Remove-Item -LiteralPath $unpackedRoot -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}

Write-Host 'Packaged full-trust host validation passed.'

