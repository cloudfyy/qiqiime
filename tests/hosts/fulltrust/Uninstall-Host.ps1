# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [switch] $RemoveDevelopmentCertificate
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$packages = @(Get-AppxPackage `
    -Name 'Cloudfyy.QiQiImePackagedFullTrustHost' `
    -ErrorAction SilentlyContinue)
foreach ($package in $packages) {
    Remove-AppxPackage -Package $package.PackageFullName
    Write-Host "Removed: $($package.PackageFullName)"
}
if ($packages.Count -eq 0) {
    Write-Host 'QiQiImePackagedFullTrustHost is not installed.'
}

if (-not $RemoveDevelopmentCertificate) {
    return
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run from an elevated PowerShell to remove the machine trust entry.'
}

$repositoryRoot = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..\..'))
$certificatePath = Join-Path $repositoryRoot `
    'tests\hosts\packaged-search\cert\QiQiImeDevelopment.cer'
$certificate = [
    Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $certificatePath)
try {
    $matches = @(Get-ChildItem `
        -LiteralPath 'Cert:\LocalMachine\TrustedPeople' |
        Where-Object Thumbprint -eq $certificate.Thumbprint)
    foreach ($match in $matches) {
        Remove-Item -LiteralPath $match.PSPath -Force
        Write-Host "Removed machine development trust: $($match.Thumbprint)"
    }
} finally {
    $certificate.Dispose()
}

Write-Host (
    'Removing this shared certificate trust may also affect the UWP test host.')

