# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

# Shared helper functions and constants for QiQi Wubi IME (奇奇五笔) PowerShell scripts

$ErrorActionPreference = "Stop"

$script:QiQiRepoRoot = Split-Path -Parent $PSScriptRoot
$script:QiQiClsid = "{8E7B9D31-5A42-4C81-9F11-202610290001}"
$script:QiQiProfileGuid = "{8E7B9D31-5A42-4C81-9F11-202610290002}"
$script:QiQiDataFileNames = @(
    "wubi86_jidian.txt",
    "wubi86_jidian_extra.txt",
    "wubi86_haifeng.txt"
)
$script:QiQiToolExeNames = @(
    "QiQiIMESettings.exe",
    "QiQiIMELogMonitor.exe",
    "QiQiIMEDictManager.exe"
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

function Get-QiQiRuntimeArchitectures([bool]$Is64BitOperatingSystem = [Environment]::Is64BitOperatingSystem) {
    if ($Is64BitOperatingSystem) { return @("x64", "x86") }
    return @("x86")
}

function Resolve-QiQiDllPaths(
    [string]$Config = "Release",
    [string]$BaseDir = $script:QiQiRepoRoot,
    [bool]$Is64BitOperatingSystem = [Environment]::Is64BitOperatingSystem
) {
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
        X64 = $(if ($Is64BitOperatingSystem) { $x64Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1 })
        X86 = ($x86Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1)
        ExpectedX64 = $x64Candidates[0]
        ExpectedX86 = $x86Candidates[0]
    }
}

function Copy-QiQiSymbols([hashtable]$Dlls, [string]$Destination) {
    foreach ($arch in @("x64", "x86")) {
        $dll = $Dlls[$arch]
        if (-not $dll) {
            throw "Missing $arch DLL path for symbol packaging."
        }
        $sourceDir = Split-Path -Parent $dll
        $targetDir = Join-Path $Destination $arch
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        foreach ($binary in (@("qiqiime.dll") + $script:QiQiToolExeNames)) {
            $pdbName = [System.IO.Path]::ChangeExtension($binary, ".pdb")
            $pdbPath = Join-Path $sourceDir $pdbName
            if (-not (Test-Path -LiteralPath $pdbPath -PathType Leaf)) {
                throw "Missing $arch symbol file: $pdbPath"
            }
            Copy-Item -LiteralPath $pdbPath -Destination (Join-Path $targetDir $pdbName)
        }
    }
}

function Copy-QiQiReleaseDocumentation(
    [string]$Destination,
    [string]$SourceDirectory = $script:QiQiRepoRoot
) {
    foreach ($relativePath in @(
        "LICENSE",
        "README.md",
        "README.zh-CN.md",
        "PRIVACY.md",
        "CODE_SIGNING.md",
        "THIRD_PARTY_NOTICES.md",
        "docs\install.md",
        "docs\log.md",
        "docs\release-notes.md",
        "licenses\Apache-2.0.txt",
        "licenses\rime-wubi86-jidian-MIT.txt",
        "licenses\wubi-haifeng-BSD-3-Clause.txt",
        "licenses\microsoft-gsl-MIT.txt"
    )) {
        $source = Join-Path $SourceDirectory $relativePath
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Missing release documentation: $source"
        }
        $targetRelativePath = if ($relativePath.StartsWith("docs\", [StringComparison]::Ordinal)) {
            Split-Path -Leaf $relativePath
        } else {
            $relativePath
        }
        $target = Join-Path $Destination $targetRelativePath
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
    }
}

function Get-QiQiRegsvr32Path(
    [switch]$Is32Bit,
    [bool]$Is64BitOperatingSystem = [Environment]::Is64BitOperatingSystem,
    [bool]$Is64BitProcess = [Environment]::Is64BitProcess
) {
    if (-not $Is32Bit -and -not $Is64BitOperatingSystem) {
        throw "Cannot register an x64 DLL on 32-bit Windows."
    }
    if ($Is32Bit -and $Is64BitOperatingSystem) {
        Join-Path $env:SystemRoot "SysWOW64\regsvr32.exe"
    } elseif ($Is64BitOperatingSystem -and -not $Is64BitProcess) {
        Join-Path $env:SystemRoot "Sysnative\regsvr32.exe"
    } else {
        Join-Path $env:SystemRoot "System32\regsvr32.exe"
    }
}

function Get-QiQiComDllPath([ValidateSet("x64", "x86")][string]$Architecture) {
    $view = if ($Architecture -eq "x64") {
        [Microsoft.Win32.RegistryView]::Registry64
    } else {
        [Microsoft.Win32.RegistryView]::Registry32
    }
    $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $view)
    try {
        $key = $baseKey.OpenSubKey("SOFTWARE\Classes\CLSID\$script:QiQiClsid\InProcServer32")
        if ($null -eq $key) { return $null }
        try {
            return $key.GetValue("", "")
        } finally {
            $key.Dispose()
        }
    } finally {
        $baseKey.Dispose()
    }
}

function Build-QiQiMsiPackages(
    [string]$WixExe,
    [string]$StageDir,
    [string]$Destination,
    [string]$Version = (Get-QiQiVersion)
) {
    foreach ($arch in @("x64", "x86")) {
        foreach ($loc in @(
            @{ Culture = "zh-CN"; Lcid = "2052"; SummaryCp = "936" },
            @{ Culture = "en-US"; Lcid = "1033"; SummaryCp = "1252" }
        )) {
            $msiPath = Join-Path $Destination "QiQiIME-v$Version-win-$arch-$($loc.Culture).msi"
            $wxsPath = Join-Path $script:QiQiRepoRoot "installer\QiQiIME.wxs"
            $wxlPath = Join-Path $script:QiQiRepoRoot "installer\$($loc.Culture).wxl"
            & $WixExe build $wxsPath -loc $wxlPath -culture $loc.Culture -arch $arch -pdbtype none `
                -d "ProductVersion=$Version" -d "ProductLcid=$($loc.Lcid)" `
                -d "SummaryCodepage=$($loc.SummaryCp)" -d "StageDir=$StageDir" -o $msiPath
            if ($LASTEXITCODE -ne 0) {
                throw "WiX MSI build ($arch, $($loc.Culture)) failed with exit code $LASTEXITCODE"
            }
            $msiSizeKB = [math]::Round((Get-Item -LiteralPath $msiPath).Length / 1KB, 1)
            Write-QiQiPass "Release MSI installer ($arch, $($loc.Culture)) created: $msiPath ($msiSizeKB KB)"
        }
    }
}

function Invoke-QiQiRegsvr32([string]$DllPath, [switch]$Is32Bit, [switch]$Unregister, [switch]$EnableCurrentUser) {
    $regsvr = Get-QiQiRegsvr32Path -Is32Bit:$Is32Bit
    $archLabel = if ($Is32Bit) { "x86" } else { "x64" }
    if ($EnableCurrentUser) {
        Write-Host "  Enabling current user's TSF profile: $DllPath"
        & $regsvr /s /n /i:user $DllPath
    } elseif ($Unregister) {
        Write-Host "  Unregistering $archLabel TSF Text Service: $DllPath"
        & $regsvr /u /s $DllPath
    } else {
        Write-Host "  Registering $archLabel TSF Text Service: $DllPath"
        & $regsvr /s $DllPath
    }
    if ($LASTEXITCODE -ne 0) {
        $op = if ($EnableCurrentUser) { "enable current user profile for" } elseif ($Unregister) { "unregister" } else { "register" }
        Write-Error "Failed to $op $archLabel DLL ($DllPath), exit code: $LASTEXITCODE"
    }
}

function Test-QiQiRuntimeFiles(
    [hashtable]$Dlls,
    [bool]$Is64BitOperatingSystem = [Environment]::Is64BitOperatingSystem
) {
    foreach ($arch in (Get-QiQiRuntimeArchitectures -Is64BitOperatingSystem $Is64BitOperatingSystem)) {
        $archInfo = @{ Name = $arch; Dll = $Dlls[$arch]; Expected = $Dlls["Expected$arch"] }
        if (-not $archInfo.Dll) {
            throw "Missing $($archInfo.Name) DLL: $($archInfo.Expected) (run scripts\build_all.ps1 first)"
        }
        $item = Get-Item $archInfo.Dll
        $ver = $item.VersionInfo
        Write-QiQiPass "$($archInfo.Name) DLL ($([math]::Round($item.Length / 1KB, 1)) KB): $($item.FullName) [v$($ver.FileVersion)]"
        foreach ($exeName in $script:QiQiToolExeNames) {
            $exePath = Join-Path $item.DirectoryName $exeName
            if (-not (Test-Path $exePath)) {
                throw "Missing required tool executable ($exeName) for $($archInfo.Name) in $($item.DirectoryName)"
            }
            $exeItem = Get-Item $exePath
            $exeVer = $exeItem.VersionInfo
            Write-QiQiPass "$($archInfo.Name) $exeName ($([math]::Round($exeItem.Length / 1KB, 1)) KB): $($exeItem.FullName) [v$($exeVer.FileVersion)]"
        }
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

function Grant-QiQiAppContainerReadExecute([hashtable]$Dlls) {
    $dirsToGrant = New-Object System.Collections.Generic.HashSet[string]([StringComparer]::OrdinalIgnoreCase)
    foreach ($dllPath in @($Dlls.X64, $Dlls.X86)) {
        if ($dllPath -and (Test-Path $dllPath)) {
            $dllDir = Split-Path -Parent $dllPath
            [void]$dirsToGrant.Add($dllDir)
            $parentDir = Split-Path -Parent $dllDir
            if ($parentDir) {
                $sharedData = Join-Path $parentDir "data"
                if (Test-Path $sharedData) {
                    [void]$dirsToGrant.Add($sharedData)
                }
            }
        }
    }
    $repoDataDir = Join-Path $script:QiQiRepoRoot "data"
    if (Test-Path $repoDataDir) {
        [void]$dirsToGrant.Add($repoDataDir)
    }

    foreach ($dir in $dirsToGrant) {
        & icacls.exe $dir /grant "*S-1-15-2-1:(OI)(CI)(RX)" /grant "*S-1-15-2-2:(OI)(CI)(RX)" /Q | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-QiQiPass "Granted AppContainer (S-1-15-2-1 / S-1-15-2-2) RX access: $dir"
        } else {
            Write-QiQiWarn "icacls exited with $LASTEXITCODE for $dir"
        }
    }
}

function Assert-QiQiAdminOrElevate(
    [string]$ScriptPath,
    [string[]]$ForwardArgs,
    [switch]$AutoElevate,
    [switch]$ReturnAfterElevation
) {
    if (Test-QiQiAdmin) {
        return
    }
    if ($AutoElevate) {
        Write-Host "  Requesting Administrator privileges via UAC..." -ForegroundColor Yellow
        $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$ScriptPath`"") + $ForwardArgs
        $proc = Start-Process powershell.exe -Verb RunAs -ArgumentList $argList -Wait -PassThru
        if ($ReturnAfterElevation) {
            if ($proc.ExitCode -ne 0) {
                throw "Elevated operation failed, exit code: $($proc.ExitCode)"
            }
            return $true
        }
        exit $proc.ExitCode
    }
    Write-QiQiWarn "Current shell is NOT running as Administrator."
    Write-Host "  Re-run in an elevated PowerShell or pass -AutoElevate:" -ForegroundColor Yellow
    Write-Host "    powershell -ExecutionPolicy Bypass -File $ScriptPath -AutoElevate" -ForegroundColor White
    exit 1
}
