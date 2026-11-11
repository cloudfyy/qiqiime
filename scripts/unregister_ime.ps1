# Unregister QiQi Wubi IME (TSF) for both x86 and x64 and verify removal
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\unregister_ime.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\unregister_ime.ps1 -AutoElevate
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$AutoElevate
)

. (Join-Path $PSScriptRoot "common.ps1")
$dlls = Resolve-QiQiDllPaths -Config $Config

if (-not $dlls.X64 -and -not $dlls.X86) {
    Write-Error "No qiqiime.dll found to unregister."
}

Assert-QiQiAdminOrElevate -ScriptPath $PSCommandPath -ForwardArgs @("-Config", $Config) -AutoElevate:$AutoElevate

Write-QiQiStep "Unregistering TSF Text Service ($(Get-QiQiRuntimeArchitectures))"
if ($dlls.X86) {
    Invoke-QiQiRegsvr32 -DllPath $dlls.X86 -Is32Bit -Unregister
}
if ($dlls.X64 -and [Environment]::Is64BitOperatingSystem) {
    Invoke-QiQiRegsvr32 -DllPath $dlls.X64 -Unregister
}

foreach ($arch in (Get-QiQiRuntimeArchitectures)) {
    if ($null -ne (Get-QiQiComDllPath -Architecture $arch)) {
        throw "$arch COM CLSID still exists after unregister"
    }
}
Write-QiQiPass "Registry cleanup verified"
Write-Host "`nUnregistered successfully." -ForegroundColor Green
