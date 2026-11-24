# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [string] $PackagePath,
    [switch] $Launch
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..\..'))
$certificatePath = Join-Path $repositoryRoot `
    'tests\hosts\uwp\cert\QiQiImeDevelopment.cer'

if (-not $PackagePath) {
    $PackagePath = Get-ChildItem `
        -LiteralPath (Join-Path $repositoryRoot 'dist\test-hosts') `
        -Filter 'QiQiImePackagedFullTrustHost_*.msix' `
        -File `
        -ErrorAction Stop |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 |
        ForEach-Object FullName
}
if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
    throw "Package was not found: $PackagePath"
}
if (-not (Test-Path -LiteralPath $certificatePath -PathType Leaf)) {
    throw "Shared development certificate was not found: $certificatePath"
}

$certificate = [
    Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $certificatePath)
try {
    $trusted = @(Get-ChildItem `
        -LiteralPath 'Cert:\LocalMachine\TrustedPeople' `
        -ErrorAction SilentlyContinue |
        Where-Object Thumbprint -eq $certificate.Thumbprint)
    if ($trusted.Count -eq 0) {
        $principal = New-Object Security.Principal.WindowsPrincipal(
            [Security.Principal.WindowsIdentity]::GetCurrent())
        if (-not $principal.IsInRole(
                [Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw (
                'The shared development certificate is not trusted. ' +
                'Run this script from an elevated PowerShell once.')
        }
        Import-Certificate `
            -FilePath $certificatePath `
            -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' |
            Out-Null
    }
} finally {
    $certificate.Dispose()
}

Add-AppxPackage `
    -Path $PackagePath `
    -ForceUpdateFromAnyVersion `
    -ForceApplicationShutdown `
    -ErrorAction Stop |
    Out-Null

$package = Get-AppxPackage `
    -Name 'Cloudfyy.QiQiImePackagedFullTrustHost' `
    -ErrorAction Stop
if (-not $package) {
    throw 'The package was not registered after Add-AppxPackage.'
}

Write-Host "Installed: $($package.PackageFullName)"
Write-Host "AUMID: $($package.PackageFamilyName)!App"

if ($Launch) {
    $aumid = "$($package.PackageFamilyName)!App"
    Start-Process "shell:AppsFolder\$aumid"
}

