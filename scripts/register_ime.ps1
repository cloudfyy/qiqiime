# Run as Administrator to register QiQi Wubi IME (TSF) for both x64 and x86
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

$dllX64Candidates = @(
    (Join-Path $repoRoot "build\x64\$Config\qiqiime.dll"),
    (Join-Path $repoRoot "build\$Config\qiqiime.dll")
)
$dllX64 = $dllX64Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1

$dllX86 = Join-Path $repoRoot "build\x86\$Config\qiqiime.dll"

if (-not $dllX64 -and -not (Test-Path $dllX86)) {
    Write-Error "No built qiqiime.dll found. Please run scripts\build_all.ps1 or CMake build first."
}

if ($dllX64) {
    $regsvr64 = Join-Path $env:SystemRoot "System32\regsvr32.exe"
    Write-Host "Registering x64 TSF Text Service: $dllX64"
    & $regsvr64 /s $dllX64
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to register x64 DLL ($dllX64), exit code: $LASTEXITCODE"
    }
}

if (Test-Path $dllX86) {
    $regsvr32 = if ([Environment]::Is64BitOperatingSystem) {
        Join-Path $env:SystemRoot "SysWOW64\regsvr32.exe"
    } else {
        Join-Path $env:SystemRoot "System32\regsvr32.exe"
    }
    Write-Host "Registering x86 (WOW64) TSF Text Service: $dllX86"
    & $regsvr32 /s $dllX86
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to register x86 DLL ($dllX86), exit code: $LASTEXITCODE"
    }
}

Write-Host "Registered successfully! Switch input method with Win+Space." -ForegroundColor Green
