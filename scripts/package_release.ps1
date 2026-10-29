# Package a standalone dual-architecture (x64 + x86) release bundle and ZIP archive into dist/
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 -Build
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content (Join-Path $repoRoot "VERSION") -Raw).Trim()

if ($Build) {
    & (Join-Path $PSScriptRoot "build_all.ps1") -Config $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$dllX64 = Join-Path $repoRoot "build\x64\$Config\qiqiime.dll"
$dllX86 = Join-Path $repoRoot "build\x86\$Config\qiqiime.dll"
if (-not (Test-Path $dllX64) -or -not (Test-Path $dllX86)) {
    throw "Missing x64 or x86 qiqiime.dll. Run with -Build or execute scripts\build_all.ps1 first."
}

$distRoot = Join-Path $repoRoot "dist"
$pkgName = "QiQiIME-v$version-win-x64-x86"
$pkgDir = Join-Path $distRoot $pkgName
$zipPath = Join-Path $distRoot "$pkgName.zip"

if (Test-Path $pkgDir) { Remove-Item -Recurse -Force $pkgDir }
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }

New-Item -ItemType Directory -Path (Join-Path $pkgDir "x64") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $pkgDir "x86") -Force | Out-Null

# Copy DLLs, default config.ini, and dictionaries into x64/ and x86/
Copy-Item $dllX64 -Destination (Join-Path $pkgDir "x64\qiqiime.dll")
Copy-Item $dllX86 -Destination (Join-Path $pkgDir "x86\qiqiime.dll")

$dataFiles = @(
    (Join-Path $repoRoot "data\config.ini"),
    (Join-Path $repoRoot "data\wubi86_jidian.txt"),
    (Join-Path $repoRoot "data\wubi86_jidian_extra.txt"),
    (Join-Path $repoRoot "data\wubi86_haifeng.txt")
)
foreach ($df in $dataFiles) {
    Copy-Item $df -Destination (Join-Path $pkgDir "x64")
    Copy-Item $df -Destination (Join-Path $pkgDir "x86")
}

# Copy documentation & license
Copy-Item (Join-Path $repoRoot "LICENSE") -Destination $pkgDir
Copy-Item (Join-Path $repoRoot "README.md") -Destination $pkgDir
Copy-Item (Join-Path $repoRoot "docs\INSTALLATION.md") -Destination $pkgDir

# Generate standalone install.ps1 and uninstall.ps1 inside the release package
@'
# Run as Administrator to install QiQi Wubi IME (奇奇五笔) from this standalone package
$ErrorActionPreference = "Stop"
$dllX64 = Join-Path $PSScriptRoot "x64\qiqiime.dll"
$dllX86 = Join-Path $PSScriptRoot "x86\qiqiime.dll"
if ([Environment]::Is64BitOperatingSystem) {
    & (Join-Path $env:SystemRoot "System32\regsvr32.exe") /s $dllX64
    if ($LASTEXITCODE -ne 0) { throw "Failed to register x64 qiqiime.dll (run as Administrator)" }
    & (Join-Path $env:SystemRoot "SysWOW64\regsvr32.exe") /s $dllX86
    if ($LASTEXITCODE -ne 0) { throw "Failed to register x86 qiqiime.dll" }
} else {
    & (Join-Path $env:SystemRoot "System32\regsvr32.exe") /s $dllX86
    if ($LASTEXITCODE -ne 0) { throw "Failed to register x86 qiqiime.dll (run as Administrator)" }
}
Write-Host "奇奇五笔 (QiQi Wubi IME) installed successfully! Press Win+Space to switch." -ForegroundColor Green
'@ | Out-File -FilePath (Join-Path $pkgDir "install.ps1") -Encoding utf8

@'
# Run as Administrator to uninstall QiQi Wubi IME (奇奇五笔)
$ErrorActionPreference = "Stop"
$dllX64 = Join-Path $PSScriptRoot "x64\qiqiime.dll"
$dllX86 = Join-Path $PSScriptRoot "x86\qiqiime.dll"
if ([Environment]::Is64BitOperatingSystem) {
    & (Join-Path $env:SystemRoot "SysWOW64\regsvr32.exe") /u /s $dllX86
    & (Join-Path $env:SystemRoot "System32\regsvr32.exe") /u /s $dllX64
} else {
    & (Join-Path $env:SystemRoot "System32\regsvr32.exe") /u /s $dllX86
}
Write-Host "奇奇五笔 (QiQi Wubi IME) uninstalled successfully." -ForegroundColor Green
'@ | Out-File -FilePath (Join-Path $pkgDir "uninstall.ps1") -Encoding utf8

Compress-Archive -Path $pkgDir -DestinationPath $zipPath -Force
$zipSizeKB = [math]::Round((Get-Item $zipPath).Length / 1KB, 1)
Write-Host "Release package created: $zipPath ($zipSizeKB KB)" -ForegroundColor Green
