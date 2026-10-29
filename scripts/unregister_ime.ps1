# Run as Administrator to unregister QiQi Wubi IME (TSF) for both x86 and x64
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

$dllX86 = Join-Path $repoRoot "build\x86\$Config\qiqiime.dll"
$dllX64Candidates = @(
    (Join-Path $repoRoot "build\x64\$Config\qiqiime.dll"),
    (Join-Path $repoRoot "build\$Config\qiqiime.dll")
)
$dllX64 = $dllX64Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $dllX64 -and -not (Test-Path $dllX86)) {
    Write-Error "No qiqiime.dll found to unregister."
}

# Unregister x86 (WOW64) first, then native x64
if (Test-Path $dllX86) {
    $regsvr32 = if ([Environment]::Is64BitOperatingSystem) {
        Join-Path $env:SystemRoot "SysWOW64\regsvr32.exe"
    } else {
        Join-Path $env:SystemRoot "System32\regsvr32.exe"
    }
    Write-Host "Unregistering x86 (WOW64) TSF Text Service: $dllX86"
    & $regsvr32 /u /s $dllX86
}

if ($dllX64) {
    $regsvr64 = Join-Path $env:SystemRoot "System32\regsvr32.exe"
    Write-Host "Unregistering x64 TSF Text Service: $dllX64"
    & $regsvr64 /u /s $dllX64
}

Write-Host "Unregistered successfully." -ForegroundColor Green
