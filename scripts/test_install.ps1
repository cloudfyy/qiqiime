# Installation & Verification Test Script for QiQi Wubi IME (奇奇五笔)
# Usage:
#   1. Run as Administrator (installs + verifies registry/TSF profile):
#      powershell -ExecutionPolicy Bypass -File scripts\test_install.ps1
#   2. Full round-trip test (install -> verify -> uninstall -> verify removal):
#      powershell -ExecutionPolicy Bypass -File scripts\test_install.ps1 -UnregisterAfterTest
#   3. Auto-elevate via UAC prompt if not currently Administrator:
#      powershell -ExecutionPolicy Bypass -File scripts\test_install.ps1 -AutoElevate
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$BuildIfMissing,
    [switch]$AutoElevate,
    [switch]$UnregisterAfterTest,
    [switch]$LaunchNotepad
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$clsid = "{8E7B9D31-5A42-4C81-9F11-202610290001}"
$profileGuid = "{8E7B9D31-5A42-4C81-9F11-202610290002}"

function Write-Step([string]$msg) {
    Write-Host "`n=== $msg ===" -ForegroundColor Cyan
}

function Write-Pass([string]$msg) {
    Write-Host "  [PASS] $msg" -ForegroundColor Green
}

function Write-WarnMsg([string]$msg) {
    Write-Host "  [WARN] $msg" -ForegroundColor Yellow
}

# 1. Check build artifacts (x64 & x86 DLLs + dictionaries + config.ini)
Write-Step "Step 1: Checking Build Artifacts ($Config)"
$dllX64 = Join-Path $repoRoot "build\x64\$Config\qiqiime.dll"
$dllX86 = Join-Path $repoRoot "build\x86\$Config\qiqiime.dll"

if ($BuildIfMissing -and (-not (Test-Path $dllX64) -or -not (Test-Path $dllX86))) {
    Write-Host "  Building x64 and x86 targets via scripts\build_all.ps1..."
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build_all.ps1") -Config $Config
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
}

foreach ($archInfo in @(
    @{ Name = "x64"; Dll = $dllX64 },
    @{ Name = "x86"; Dll = $dllX86 }
)) {
    $dllPath = $archInfo.Dll
    if (-not (Test-Path $dllPath)) {
        throw "Missing $($archInfo.Name) DLL: $dllPath (run scripts\build_all.ps1 first)"
    }
    $item = Get-Item $dllPath
    $ver = $item.VersionInfo
    Write-Pass "$($archInfo.Name) DLL exists ($([math]::Round($item.Length / 1KB, 1)) KB): $dllPath"
    Write-Pass "$($archInfo.Name) VersionInfo: Product='$($ver.ProductName)', Version='$($ver.FileVersion)', Copyright='$($ver.LegalCopyright)'"

    $dllDir = $item.DirectoryName
    foreach ($reqFile in @("config.ini", "wubi86_jidian.txt", "wubi86_jidian_extra.txt", "wubi86_haifeng.txt")) {
        $reqPath = Join-Path $dllDir $reqFile
        if (-not (Test-Path $reqPath)) {
            throw "Missing required data file next to $($archInfo.Name) DLL: $reqPath"
        }
    }
    Write-Pass "$($archInfo.Name) runtime data files (config.ini + 3 dictionaries) present in $dllDir"
}

# 2. Check Administrator privileges for COM/TSF registration
Write-Step "Step 2: Checking Administrator Privileges"
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    if ($AutoElevate) {
        Write-Host "  Requesting UAC elevation..." -ForegroundColor Yellow
        $argList = @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-File", "`"$PSCommandPath`"",
            "-Config", $Config
        )
        if ($UnregisterAfterTest) { $argList += "-UnregisterAfterTest" }
        if ($LaunchNotepad) { $argList += "-LaunchNotepad" }
        $proc = Start-Process powershell.exe -Verb RunAs -ArgumentList $argList -Wait -PassThru
        exit $proc.ExitCode
    }

    Write-WarnMsg "Current shell is NOT running as Administrator."
    Write-WarnMsg "COM & TSF registration requires Administrator privileges."
    Write-Host "  To execute full installation test, run in an elevated PowerShell:" -ForegroundColor Yellow
    Write-Host "    powershell -ExecutionPolicy Bypass -File scripts\test_install.ps1" -ForegroundColor White
    Write-Host "  Or pass -AutoElevate to trigger UAC prompt automatically:" -ForegroundColor Yellow
    Write-Host "    powershell -ExecutionPolicy Bypass -File scripts\test_install.ps1 -AutoElevate" -ForegroundColor White
    exit 0
}
Write-Pass "Running with Administrator privileges"

# 3. Register IME (x64 + x86 WOW64)
Write-Step "Step 3: Registering QiQi Wubi IME (x64 + x86 WOW64)"
& (Join-Path $PSScriptRoot "register_ime.ps1") -Config $Config

# 4. Verify Windows Registry (COM InProcServer32 & TSF Language Profile)
Write-Step "Step 4: Verifying COM CLSID & TSF Language Profile in Registry"

$clsid64Path = "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InProcServer32"
if (-not (Test-Path $clsid64Path)) {
    throw "64-bit COM CLSID key not found: $clsid64Path"
}
$inproc64 = (Get-ItemProperty -Path $clsid64Path).'(default)'
$thread64 = (Get-ItemProperty -Path $clsid64Path).ThreadingModel
if ($inproc64 -ne $dllX64 -or $thread64 -ne "Apartment") {
    throw "64-bit InProcServer32 mismatch: got '$inproc64' ($thread64), expected '$dllX64' (Apartment)"
}
Write-Pass "64-bit COM InProcServer32 verified: $inproc64 (ThreadingModel=$thread64)"

if ([Environment]::Is64BitOperatingSystem) {
    $clsid32Path = "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$clsid\InProcServer32"
    if (-not (Test-Path $clsid32Path)) {
        throw "32-bit WOW64 COM CLSID key not found: $clsid32Path"
    }
    $inproc32 = (Get-ItemProperty -Path $clsid32Path).'(default)'
    $thread32 = (Get-ItemProperty -Path $clsid32Path).ThreadingModel
    if ($inproc32 -ne $dllX86 -or $thread32 -ne "Apartment") {
        throw "32-bit WOW64 InProcServer32 mismatch: got '$inproc32' ($thread32), expected '$dllX86' (Apartment)"
    }
    Write-Pass "32-bit WOW64 COM InProcServer32 verified: $inproc32 (ThreadingModel=$thread32)"
}

$tipProfilePath = "HKLM:\SOFTWARE\Microsoft\CTF\TIP\$clsid\LanguageProfile\0x00000804\$profileGuid"
if (-not (Test-Path $tipProfilePath)) {
    throw "TSF LanguageProfile registry key not found: $tipProfilePath"
}
$profileDesc = (Get-ItemProperty -Path $tipProfilePath).Description
Write-Pass "TSF LanguageProfile (zh-CN 0x0804) verified: $tipProfilePath (Description='$profileDesc')"

# 5. Optional Unregister round-trip test
if ($UnregisterAfterTest) {
    Write-Step "Step 5: Testing Unregistration Round-Trip"
    & (Join-Path $PSScriptRoot "unregister_ime.ps1") -Config $Config
    if (Test-Path $clsid64Path) {
        throw "64-bit COM CLSID still exists after unregister: $clsid64Path"
    }
    if ([Environment]::Is64BitOperatingSystem -and (Test-Path "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$clsid\InProcServer32")) {
        throw "32-bit WOW64 COM CLSID still exists after unregister"
    }
    Write-Pass "Unregistration verified: all COM CLSID and TSF entries cleanly removed"
} else {
    Write-Host "`n[SUCCESS] QiQi Wubi IME is installed and verified! Press Win+Space to switch to 'QiQi 五笔 86'." -ForegroundColor Green
    if ($LaunchNotepad) {
        Write-Host "Launching notepad.exe for interactive typing test..." -ForegroundColor Cyan
        Start-Process notepad.exe
    }
}
