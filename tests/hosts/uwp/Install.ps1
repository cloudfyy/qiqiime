# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [ValidateSet('all', 'zero', 'com', 'reg')]
    [string] $Profile = 'all',
    [string] $PackagePath,
    [string] $CertificatePath = (Join-Path $PSScriptRoot 'dev.cer')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Profile.ps1')
if ($Profile -eq 'all' -and $PackagePath) {
    throw 'Use -Profile zero, com, or reg with -PackagePath.'
}

$names = if ($Profile -eq 'all') { 'zero', 'com', 'reg' } else { $Profile }
$dist = Join-Path $PSScriptRoot '..\..\..\dist\test-hosts'
$packages = @(
    foreach ($name in $names) {
        $app = Get-QiQiUwpProfile -Name $name
        $path = $PackagePath
        if (!$path) {
            $path = Get-ChildItem -LiteralPath $dist -Filter "$($app.Binary)-*.msix" -File |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
        }
        if (!$path -or !(Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "No package found for UWP $name."
        }
        [pscustomobject]@{ App = $app; Path = $path }
    }
)

$certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($CertificatePath)
try {
    foreach ($package in $packages) {
        & (Join-Path $PSScriptRoot 'Check.ps1') -Profile $package.App.Name -PackagePath $package.Path `
            -ExpectedSignerThumbprint $certificate.Thumbprint
    }
    $trustedPath = "Cert:\LocalMachine\TrustedPeople\$($certificate.Thumbprint)"
    if (!(Test-Path -LiteralPath $trustedPath)) {
        $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
        if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'Run this installer as administrator to trust the shared developer certificate.'
        }
        Import-Certificate -FilePath $CertificatePath -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' | Out-Null
    }

    foreach ($package in $packages) {
        Add-AppxPackage -Path $package.Path -ErrorAction Stop
        $registered = Get-AppxPackage -Name $package.App.Identity |
            Where-Object Publisher -eq $certificate.Subject |
            Select-Object -First 1
        if (!$registered) { throw "UWP $($package.App.Name) was not registered after installation." }
        Write-Host "Installed: $($registered.PackageFullName)"
        Write-Host "AUMID: $($registered.PackageFamilyName)!App"
    }
} finally {
    $certificate.Dispose()
}
