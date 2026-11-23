# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [string] $PackagePath,
    [string] $CertificatePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..\..'))

if (-not $PackagePath) {
    $PackagePath = Get-ChildItem `
        -LiteralPath (Join-Path $repositoryRoot 'dist\test-hosts') `
        -Filter 'QiQiImeUwpSearchHost_*.msix' `
        -File `
        -ErrorAction Stop |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 |
        ForEach-Object FullName
}
if (-not $CertificatePath) {
    $CertificatePath = Join-Path $PSScriptRoot `
        'cert\QiQiImeDevelopment.cer'
}

if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
    throw "Package was not found: $PackagePath"
}
if (-not (Test-Path -LiteralPath $CertificatePath -PathType Leaf)) {
    throw "Test certificate was not found: $CertificatePath"
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this installer from an elevated PowerShell so the test certificate can be added to LocalMachine\TrustedPeople.'
}

Import-Certificate `
    -FilePath $CertificatePath `
    -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' |
    Out-Null

Add-AppxPackage `
    -Path $PackagePath `
    -ForceUpdateFromAnyVersion `
    -ForceApplicationShutdown `
    -ErrorAction Stop |
    Out-Null

$package = Get-AppxPackage -Name 'Cloudfyy.QiQiImeUwpSearchHost' `
    -ErrorAction Stop
if (-not $package) {
    throw 'The package was not registered after Add-AppxPackage.'
}

Write-Host "Installed: $($package.PackageFullName)"
Write-Host "AUMID: $($package.PackageFamilyName)!App"
