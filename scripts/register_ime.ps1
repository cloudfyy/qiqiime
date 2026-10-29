# Run as Administrator to register QiQi Wubi IME (TSF)
$ErrorActionPreference = "Stop"
$dllPath = Join-Path $PSScriptRoot "..\build\Release\qiqiime.dll"
if (-not (Test-Path $dllPath)) {
    Write-Error "DLL not found at $dllPath. Please build the project with CMake first."
}
Write-Host "Registering TSF Text Service: $dllPath"
& regsvr32.exe /s $dllPath
Write-Host "Registered successfully! Switch input method with Win+Space." -ForegroundColor Green
