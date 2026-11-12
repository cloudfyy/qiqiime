# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

# Register and verify native x86 Windows or x64 Windows with x86 (WOW64) hosts.
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\register_ime.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\register_ime.ps1 -AutoElevate -LaunchNotepad
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$AutoElevate,
    [switch]$VerifyOnly,
    [switch]$EnableCurrentUserOnly,
    [switch]$LaunchNotepad
)

. (Join-Path $PSScriptRoot "common.ps1")

Write-QiQiStep "Checking QiQiIME Artifacts ($Config)"
$dlls = Resolve-QiQiDllPaths -Config $Config
Test-QiQiRuntimeFiles -Dlls $dlls

if ($VerifyOnly) {
    exit 0
}

if ($EnableCurrentUserOnly) {
    if ([Environment]::Is64BitOperatingSystem) {
        Invoke-QiQiRegsvr32 -DllPath $dlls.X64 -EnableCurrentUser
    } else {
        Invoke-QiQiRegsvr32 -DllPath $dlls.X86 -Is32Bit -EnableCurrentUser
    }
    Write-QiQiPass "Current user's TSF profile enabled. Switch input method with Win+Space."
    exit 0
}

$forwardArgs = @("-Config", $Config)
if ($LaunchNotepad) { $forwardArgs += "-LaunchNotepad" }
Assert-QiQiAdminOrElevate -ScriptPath $PSCommandPath -ForwardArgs $forwardArgs -AutoElevate:$AutoElevate

Write-QiQiStep "Granting AppContainer (UWP) Read/Execute ACLs on Build & Data Directories"
Grant-QiQiAppContainerReadExecute -Dlls $dlls

Write-QiQiStep "Registering TSF Text Service ($(Get-QiQiRuntimeArchitectures))"
if ($dlls.X64 -and [Environment]::Is64BitOperatingSystem) {
    Invoke-QiQiRegsvr32 -DllPath $dlls.X64
}
if ($dlls.X86) {
    Invoke-QiQiRegsvr32 -DllPath $dlls.X86 -Is32Bit
}

Write-QiQiStep "Verifying COM CLSID & TSF LanguageProfile in Registry"
foreach ($arch in (Get-QiQiRuntimeArchitectures)) {
    $registeredDll = Get-QiQiComDllPath -Architecture $arch
    if ($registeredDll -ne $dlls[$arch]) {
        throw "$arch COM InProcServer32 mismatch: expected '$($dlls[$arch])', found '$registeredDll'."
    }
    Write-QiQiPass "$arch COM InProcServer32 verified: $registeredDll"
}

$tipProfilePath = "HKLM:\SOFTWARE\Microsoft\CTF\TIP\$QiQiClsid\LanguageProfile\0x00000804\$QiQiProfileGuid"
if (-not (Test-Path $tipProfilePath)) {
    throw "TSF LanguageProfile registry key not found: $tipProfilePath"
}
Write-QiQiPass "TSF LanguageProfile (zh-CN 0x0804) verified"

Write-Host "`nRegistered successfully! Switch input method with Win+Space." -ForegroundColor Green
if ($LaunchNotepad) {
    Start-Process notepad.exe
}
