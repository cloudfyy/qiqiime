# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [ValidateSet('zero', 'com', 'reg')]
    [string] $Profile = 'zero'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Profile.ps1')
$app = Get-QiQiUwpProfile -Name $Profile
$packages = @(Get-AppxPackage -Name $app.Identity)
if (!$packages.Count) { Write-Host "UWP $Profile is not installed." }
foreach ($package in $packages) {
    Remove-AppxPackage -Package $package.PackageFullName
    Write-Host "Removed: $($package.PackageFullName)"
}
