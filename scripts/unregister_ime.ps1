# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

# Unregister QiQi Wubi IME (TSF) for both x86 and x64 and verify removal
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\unregister_ime.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\unregister_ime.ps1 -AutoElevate
#   powershell -ExecutionPolicy Bypass -File scripts\unregister_ime.ps1 -AutoElevate -RemoveUserData
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$AutoElevate,
    [switch]$RemoveUserData
)

. (Join-Path $PSScriptRoot "common.ps1")
$dlls = Resolve-QiQiDllPaths -Config $Config

if (-not $dlls.X64 -and -not $dlls.X86) {
    Write-Error "No qiqiime.dll found to unregister."
}

$cleaner = $null
if ($RemoveUserData) {
    $nativeDll = if ([Environment]::Is64BitOperatingSystem) { $dlls.X64 } else { $dlls.X86 }
    if (-not $nativeDll) {
        throw "Missing native DLL path for user data cleanup."
    }
    $cleaner = Join-Path (Split-Path -Parent $nativeDll) "QiQiIMESettings.exe"
    if (-not (Test-Path -LiteralPath $cleaner -PathType Leaf)) {
        throw "Missing native settings executable for user data cleanup: $cleaner"
    }
}

# Keep cleanup in the initiating account, even if UAC uses another administrator.
$unregisteredByElevation = Assert-QiQiAdminOrElevate -ScriptPath $PSCommandPath `
    -ForwardArgs @("-Config", $Config) -AutoElevate:$AutoElevate -ReturnAfterElevation:$RemoveUserData

Write-QiQiStep "Unregistering TSF Text Service ($(Get-QiQiRuntimeArchitectures))"
if (-not $unregisteredByElevation) {
    if ($dlls.X86) {
        Invoke-QiQiRegsvr32 -DllPath $dlls.X86 -Is32Bit -Unregister
    }
    if ($dlls.X64 -and [Environment]::Is64BitOperatingSystem) {
        Invoke-QiQiRegsvr32 -DllPath $dlls.X64 -Unregister
    }
}

foreach ($arch in (Get-QiQiRuntimeArchitectures)) {
    if ($null -ne (Get-QiQiComDllPath -Architecture $arch)) {
        throw "$arch COM CLSID still exists after unregister"
    }
}
Write-QiQiPass "Registry cleanup verified"
if ($RemoveUserData) {
    Write-QiQiStep "Removing current user's default settings and dictionary data"
    $cleanup = Start-Process -FilePath $cleaner -ArgumentList "--remove-user-data" -Wait -PassThru
    if ($cleanup.ExitCode -ne 0) {
        throw "Input method unregistered, but user data cleanup failed, exit code: $($cleanup.ExitCode). Some data may remain."
    }
    Write-QiQiPass "Current user's default data removed; custom dictionaries and backups preserved"
}
Write-Host "`nUnregistered successfully." -ForegroundColor Green
