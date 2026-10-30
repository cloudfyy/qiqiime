# Shared helper functions and constants for QiQi Wubi IME (奇奇五笔) PowerShell scripts

$ErrorActionPreference = "Stop"

$script:QiQiRepoRoot = Split-Path -Parent $PSScriptRoot
$script:QiQiClsid = "{8E7B9D31-5A42-4C81-9F11-202610290001}"
$script:QiQiProfileGuid = "{8E7B9D31-5A42-4C81-9F11-202610290002}"
$script:QiQiDataFileNames = @(
    "config.ini",
    "wubi86_jidian.txt",
    "wubi86_jidian_extra.txt",
    "wubi86_haifeng.txt"
)

function Write-QiQiStep([string]$Message) {
    Write-Host "`n=== $Message ===" -ForegroundColor Cyan
}

function Write-QiQiPass([string]$Message) {
    Write-Host "  [PASS] $Message" -ForegroundColor Green
}

function Write-QiQiWarn([string]$Message) {
    Write-Host "  [WARN] $Message" -ForegroundColor Yellow
}

function Test-QiQiAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-QiQiVersion {
    $verFile = Join-Path $script:QiQiRepoRoot "VERSION"
    if (Test-Path $verFile) {
        return (Get-Content $verFile -Raw).Trim()
    }
    return "0.0.0"
}

function Get-QiQiDefaultVsGenerator {
    foreach ($year in @(@{ Ver = "18"; Name = "Visual Studio 18 2026" }, @{ Ver = "2022"; Name = "Visual Studio 17 2022" })) {
        foreach ($edition in @("Enterprise", "Professional", "Community", "BuildTools")) {
            if (Test-Path "$env:ProgramFiles\Microsoft Visual Studio\$($year.Ver)\$edition") {
                return $year.Name
            }
        }
    }
    return "Visual Studio 18 2026"
}

function Get-QiQiBuildTargets {
    return @(
        @{ Name = "x64"; Arch = "x64";   BuildDir = Join-Path $script:QiQiRepoRoot "build\x64" },
        @{ Name = "x86"; Arch = "Win32"; BuildDir = Join-Path $script:QiQiRepoRoot "build\x86" }
    )
}

function Resolve-QiQiDllPaths([string]$Config = "Release", [string]$BaseDir = $script:QiQiRepoRoot) {
    $x64Candidates = @(
        (Join-Path $BaseDir "build\x64\$Config\qiqiime.dll"),
        (Join-Path $BaseDir "build\$Config\qiqiime.dll"),
        (Join-Path $BaseDir "x64\qiqiime.dll")
    )
    $x86Candidates = @(
        (Join-Path $BaseDir "build\x86\$Config\qiqiime.dll"),
        (Join-Path $BaseDir "x86\qiqiime.dll")
    )
    return @{
        X64 = ($x64Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1)
        X86 = ($x86Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1)
        ExpectedX64 = $x64Candidates[0]
        ExpectedX86 = $x86Candidates[0]
    }
}

function Invoke-QiQiRegsvr32([string]$DllPath, [switch]$Is32Bit, [switch]$Unregister) {
    $regsvr = if ($Is32Bit -and [Environment]::Is64BitOperatingSystem) {
        Join-Path $env:SystemRoot "SysWOW64\regsvr32.exe"
    } else {
        Join-Path $env:SystemRoot "System32\regsvr32.exe"
    }
    $archLabel = if ($Is32Bit) { "x86 (WOW64)" } else { "x64" }
    if ($Unregister) {
        Write-Host "  Unregistering $archLabel TSF Text Service: $DllPath"
        & $regsvr /u /s $DllPath
    } else {
        Write-Host "  Registering $archLabel TSF Text Service: $DllPath"
        & $regsvr /s $DllPath
    }
    if ($LASTEXITCODE -ne 0) {
        $op = if ($Unregister) { "unregister" } else { "register" }
        Write-Error "Failed to $op $archLabel DLL ($DllPath), exit code: $LASTEXITCODE"
    }
}

function Test-QiQiRuntimeFiles([hashtable]$Dlls) {
    foreach ($archInfo in @(
        @{ Name = "x64"; Dll = $Dlls.X64; Expected = $Dlls.ExpectedX64 },
        @{ Name = "x86"; Dll = $Dlls.X86; Expected = $Dlls.ExpectedX86 }
    )) {
        if (-not $archInfo.Dll) {
            throw "Missing $($archInfo.Name) DLL: $($archInfo.Expected) (run scripts\build_all.ps1 first)"
        }
        $item = Get-Item $archInfo.Dll
        $ver = $item.VersionInfo
        Write-QiQiPass "$($archInfo.Name) DLL ($([math]::Round($item.Length / 1KB, 1)) KB): $($item.FullName) [v$($ver.FileVersion)]"
        $parentDir = if ($item.Directory.Parent) { $item.Directory.Parent.FullName } else { $item.DirectoryName }
        foreach ($reqFile in $script:QiQiDataFileNames) {
            $candidates = @(
                (Join-Path $item.DirectoryName $reqFile),
                (Join-Path $parentDir "data\$reqFile"),
                (Join-Path $parentDir $reqFile)
            )
            if (-not ($candidates | Where-Object { Test-Path $_ } | Select-Object -First 1)) {
                throw "Missing required data file ($reqFile) for $($archInfo.Name) DLL in $($item.DirectoryName) or ..\data"
            }
        }
    }
}

function Assert-QiQiAdminOrElevate([string]$ScriptPath, [string[]]$ForwardArgs, [switch]$AutoElevate) {
    if (Test-QiQiAdmin) {
        return
    }
    if ($AutoElevate) {
        Write-Host "  Requesting Administrator privileges via UAC..." -ForegroundColor Yellow
        $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$ScriptPath`"") + $ForwardArgs
        $proc = Start-Process powershell.exe -Verb RunAs -ArgumentList $argList -Wait -PassThru
        exit $proc.ExitCode
    }
    Write-QiQiWarn "Current shell is NOT running as Administrator."
    Write-Host "  Re-run in an elevated PowerShell or pass -AutoElevate:" -ForegroundColor Yellow
    Write-Host "    powershell -ExecutionPolicy Bypass -File $ScriptPath -AutoElevate" -ForegroundColor White
    exit 1
}
