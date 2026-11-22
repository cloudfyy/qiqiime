# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [switch] $RemoveDevelopmentCertificate,
    [string] $CertificatePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$packages = @(Get-AppxPackage `
    -Name 'Cloudfyy.QiQiImeUwpSearchHost' `
    -ErrorAction SilentlyContinue)
if ($packages.Count -eq 0) {
    Write-Host 'QiQiImeUwpSearchHost is not installed.'
} else {
    foreach ($package in $packages) {
        Remove-AppxPackage -Package $package.PackageFullName
        Write-Host "Removed: $($package.PackageFullName)"
    }
}

if ($RemoveDevelopmentCertificate) {
    $repositoryRoot = [IO.Path]::GetFullPath(
        (Join-Path $PSScriptRoot '..\..\..'))
    if (-not $CertificatePath) {
        $CertificatePath = Join-Path $PSScriptRoot `
            'cert\QiQiImeDevelopment.cer'
    }
    if (-not (Test-Path -LiteralPath $CertificatePath -PathType Leaf)) {
        throw "Development certificate was not found: $CertificatePath"
    }
    $principal = New-Object Security.Principal.WindowsPrincipal(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run from an elevated PowerShell to remove the machine development certificate.'
    }
    $certificate = [
        Security.Cryptography.X509Certificates.X509Certificate2]::new(
            $CertificatePath)
    try {
        $matches = @(Get-ChildItem `
            -LiteralPath 'Cert:\LocalMachine\TrustedPeople' |
            Where-Object Thumbprint -eq $certificate.Thumbprint)
        foreach ($match in $matches) {
            Remove-Item -LiteralPath $match.PSPath -Force
            Write-Host "Removed development certificate: $($match.Thumbprint)"
        }
        $privateMatches = @(Get-ChildItem `
            -LiteralPath 'Cert:\CurrentUser\My' |
            Where-Object Thumbprint -eq $certificate.Thumbprint)
        foreach ($match in $privateMatches) {
            Remove-Item -LiteralPath $match.PSPath -Force
            Write-Host "Removed development private key: $($match.Thumbprint)"
        }
    } finally {
        $certificate.Dispose()
    }
}
