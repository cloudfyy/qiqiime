# Register and verify QiQi Wubi IME (TSF) for both x64 and x86 (WOW64)
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\register_ime.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\register_ime.ps1 -AutoElevate -LaunchNotepad
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$AutoElevate,
    [switch]$VerifyOnly,
    [switch]$LaunchNotepad
)

. (Join-Path $PSScriptRoot "common.ps1")

Write-QiQiStep "Checking QiQiIME Artifacts ($Config)"
$dlls = Resolve-QiQiDllPaths -Config $Config
Test-QiQiRuntimeFiles -Dlls $dlls

if ($VerifyOnly) {
    exit 0
}

$forwardArgs = @("-Config", $Config)
if ($LaunchNotepad) { $forwardArgs += "-LaunchNotepad" }
Assert-QiQiAdminOrElevate -ScriptPath $PSCommandPath -ForwardArgs $forwardArgs -AutoElevate:$AutoElevate

Write-QiQiStep "Registering TSF Text Service (x64 + x86 WOW64)"
if ($dlls.X64 -and [Environment]::Is64BitOperatingSystem) {
    Invoke-QiQiRegsvr32 -DllPath $dlls.X64
}
if ($dlls.X86) {
    Invoke-QiQiRegsvr32 -DllPath $dlls.X86 -Is32Bit
}

Write-QiQiStep "Verifying COM CLSID & TSF LanguageProfile in Registry"
$clsid64Path = "HKLM:\SOFTWARE\Classes\CLSID\$QiQiClsid\InProcServer32"
if (-not (Test-Path $clsid64Path)) {
    throw "64-bit COM CLSID key not found: $clsid64Path"
}
Write-QiQiPass "64-bit COM InProcServer32 verified: $((Get-ItemProperty -Path $clsid64Path).'(default)')"

if ([Environment]::Is64BitOperatingSystem) {
    $clsid32Path = "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$QiQiClsid\InProcServer32"
    if (-not (Test-Path $clsid32Path)) {
        throw "32-bit WOW64 COM CLSID key not found: $clsid32Path"
    }
    Write-QiQiPass "32-bit WOW64 COM InProcServer32 verified: $((Get-ItemProperty -Path $clsid32Path).'(default)')"
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
