# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [ValidateSet('all', 'zero', 'com', 'reg')]
    [string] $Profile = 'all',
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string] $PackageVersion = '0.1.0.0',
    [string] $CertificatePath,
    [securestring] $CertificatePassword,
    [switch] $RotateDevelopmentCertificate,
    [switch] $SkipSigning
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Profile.ps1')
. (Join-Path $PSScriptRoot '..\Assets.ps1')

function Invoke-HostTool {
    param([string] $File, [string[]] $Arguments)

    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$([IO.Path]::GetFileName($File)) failed (exit $LASTEXITCODE)." }
}

$names = if ($Profile -eq 'all') { 'zero', 'com', 'reg' } else { $Profile }
$profiles = @($names | ForEach-Object { Get-QiQiUwpProfile -Name $_ })
foreach ($app in $profiles) { & (Join-Path $PSScriptRoot 'Check.ps1') -Profile $app.Name }

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$build = Join-Path $repo 'build\hosts\uwp'
$dist = Join-Path $repo 'dist\test-hosts'
$sourceCert = Join-Path $PSScriptRoot 'dev.cer'
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x64'
$makeAppx = Join-Path $sdk 'makeappx.exe'
$makePri = Join-Path $sdk 'makepri.exe'
$signTool = Join-Path $sdk 'signtool.exe'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsVersion = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
if ($LASTEXITCODE -ne 0 -or !$vsVersion) { throw 'A Visual Studio C++ desktop workload is required.' }
$generator = switch (($vsVersion -split '\.')[0]) {
    '18' { 'Visual Studio 18 2026' }
    '17' { 'Visual Studio 17 2022' }
    default { throw "Unsupported Visual Studio version: $vsVersion." }
}

Invoke-HostTool cmake @('-S', $PSScriptRoot, '-B', $build, '-G', $generator, '-A', 'x64')
Invoke-HostTool cmake (@('--build', $build, '--config', $Configuration, '--parallel', '3', '--target') +
    @($profiles.Binary))
New-Item -ItemType Directory -Path $dist -Force | Out-Null

$certificate = $null
$work = Join-Path $build ('pack-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    if (!$SkipSigning) {
        if ($CertificatePath) {
            if (!$CertificatePassword -or $RotateDevelopmentCertificate) {
                throw 'Use -CertificatePath with -CertificatePassword, or rotate the shared developer certificate.'
            }
            $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
                $CertificatePath, $CertificatePassword,
                [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet)
        } elseif ($RotateDevelopmentCertificate) {
            $certificate = New-SelfSignedCertificate -Type Custom -Subject 'CN=QiQiIME Development' `
                -KeyUsage DigitalSignature -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 `
                -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddYears(2) `
                -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
            Export-Certificate -Cert $certificate.PSPath -FilePath $sourceCert -Force | Out-Null
        } else {
            $public = [Security.Cryptography.X509Certificates.X509Certificate2]::new($sourceCert)
            try {
                $certificate = Get-ChildItem -LiteralPath 'Cert:\CurrentUser\My' |
                    Where-Object { $_.Thumbprint -eq $public.Thumbprint -and $_.HasPrivateKey -and
                                   $_.NotAfter -gt (Get-Date).AddDays(30) } |
                    Select-Object -First 1
            } finally {
                $public.Dispose()
            }
        }
        if (!$certificate -or !$certificate.HasPrivateKey) {
            throw 'The developer signing key is unavailable. Use an existing PFX, -SkipSigning, or explicitly rotate it.'
        }
    }

    $priConfig = Join-Path $work 'pri.xml'
    Invoke-HostTool $makePri @('createconfig', '/cf', $priConfig, '/dq', 'en-US_zh-CN', '/o')
    foreach ($app in $profiles) {
        $stage = Join-Path $work $app.Name
        $assets = Join-Path $stage 'Assets'
        New-QiQiHostPackageAssets -OutputDirectory $assets -IconPath (Join-Path $build "generated\icon\$($app.Name).ico")
        Copy-Item -LiteralPath (Join-Path $build "$Configuration\$($app.Binary).exe") -Destination $stage
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'TestData.json') -Destination $assets
        $manifest = New-QiQiUwpManifest -Profile $app -Version $PackageVersion
        if ($certificate) {
            $manifest.SelectSingleNode("//*[local-name()='Identity']").SetAttribute('Publisher', $certificate.Subject)
        }
        $appManifest = Join-Path $stage 'AppxManifest.xml'
        $manifest.Save($appManifest)
        Invoke-HostTool $makePri @('new', '/pr', $stage, '/cf', $priConfig, '/mn', $appManifest,
            '/of', (Join-Path $stage 'resources.pri'), '/o')
        $packageName = "$($app.Binary)-$PackageVersion.msix"
        $package = Join-Path $work $packageName
        Invoke-HostTool $makeAppx @('pack', '/d', $stage, '/p', $package, '/l', '/o')
        if ($certificate) {
            if ($CertificatePath) {
                $password = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($CertificatePassword)
                try {
                    $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($password)
                    & $signTool sign /fd SHA256 /f $CertificatePath /p $plain $package
                    if ($LASTEXITCODE -ne 0) { throw "SignTool failed (exit $LASTEXITCODE)." }
                } finally {
                    $plain = $null
                    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($password)
                }
            } else {
                Invoke-HostTool $signTool @('sign', '/fd', 'SHA256', '/s', 'My', '/sha1', $certificate.Thumbprint, $package)
            }
            & (Join-Path $PSScriptRoot 'Check.ps1') -Profile $app.Name -PackagePath $package `
                -ExpectedSignerThumbprint $certificate.Thumbprint
        } else {
            & (Join-Path $PSScriptRoot 'Check.ps1') -Profile $app.Name -PackagePath $package -AllowUnsigned
        }
        $output = Join-Path $dist $packageName
        Move-Item -LiteralPath $package -Destination $output -Force
        Write-Host "Built: $output"
    }
} finally {
    if ($certificate) { $certificate.Dispose() }
    Remove-Item -LiteralPath (Resolve-Path -LiteralPath $work).Path -Recurse -Force
}
