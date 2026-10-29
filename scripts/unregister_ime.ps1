# Run as Administrator to unregister QiQi Wubi IME (TSF)
$ErrorActionPreference = "Stop"
$dllPath = Join-Path $PSScriptRoot "..\build\Release\qiqiime.dll"
if (-not (Test-Path $dllPath)) {
    Write-Error "DLL not found at $dllPath."
}
Write-Host "Unregistering TSF Text Service: $dllPath"
& regsvr32.exe /u /s $dllPath
Write-Host "Unregistered successfully." -ForegroundColor Green
