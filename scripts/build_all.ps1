# Build, test, and optionally package both x64 and x86 (Win32) architectures for QiQi Wubi IME
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Package
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$Generator = "",
    [switch]$Package
)

. (Join-Path $PSScriptRoot "common.ps1")
if ([string]::IsNullOrWhiteSpace($Generator)) {
    $Generator = Get-QiQiDefaultVsGenerator
}

foreach ($t in (Get-QiQiBuildTargets)) {
    $cacheFile = Join-Path $t.BuildDir "CMakeCache.txt"
    if (-not (Test-Path $cacheFile)) {
        Write-QiQiStep "Configuring $($t.Name) ($($t.Arch))"
        & cmake -S $QiQiRepoRoot -B $t.BuildDir -G $Generator -A $t.Arch
        if ($LASTEXITCODE -ne 0) {
            $configureExitCode = $LASTEXITCODE
            Write-Error "CMake configuration ($($t.Name)) failed with exit code $configureExitCode." -ErrorAction Continue
            exit $configureExitCode
        }
    }

    Write-QiQiStep "Building $($t.Name) ($Config)"
    & cmake --build $t.BuildDir --config $Config
    if ($LASTEXITCODE -ne 0) {
        $buildExitCode = $LASTEXITCODE
        Write-Error "CMake build ($($t.Name), $Config) failed with exit code $buildExitCode." -ErrorAction Continue
        exit $buildExitCode
    }

    Write-QiQiStep "Testing $($t.Name) ($Config)"
    & ctest --test-dir $t.BuildDir -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        $testExitCode = $LASTEXITCODE
        Write-Error "CTest ($($t.Name), $Config) failed with exit code $testExitCode." -ErrorAction Continue
        exit $testExitCode
    }
}

Write-Host "`nBoth x64 and x86 ($Config) builds and unit tests succeeded!" -ForegroundColor Green

if ($Package) {
    Write-QiQiStep "Packaging Standalone Release Bundle (dist/)"
    $version = Get-QiQiVersion
    $dlls = Resolve-QiQiDllPaths -Config $Config
    $distRoot = Join-Path $QiQiRepoRoot "dist"
    $pkgName = "QiQiIME-v$version-win-x64-x86"
    $pkgDir = Join-Path $distRoot $pkgName
    $zipPath = Join-Path $distRoot "$pkgName.zip"

    if (Test-Path $pkgDir) { Remove-Item -Recurse -Force $pkgDir }
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }

    New-Item -ItemType Directory -Path (Join-Path $pkgDir "x64") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $pkgDir "x86") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $pkgDir "data") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $pkgDir "scripts") -Force | Out-Null

    Copy-Item $dlls.X64 -Destination (Join-Path $pkgDir "x64\qiqiime.dll")
    Copy-Item $dlls.X86 -Destination (Join-Path $pkgDir "x86\qiqiime.dll")
    foreach ($archPair in @(
        @{ Dir = (Split-Path -Parent $dlls.X64); Dest = (Join-Path $pkgDir "x64") },
        @{ Dir = (Split-Path -Parent $dlls.X86); Dest = (Join-Path $pkgDir "x86") }
    )) {
        foreach ($exeName in $QiQiToolExeNames) {
            $exePath = Join-Path $archPair.Dir $exeName
            if (-not (Test-Path $exePath)) {
                throw "Missing tool executable for packaging: $exePath"
            }
            Copy-Item $exePath -Destination (Join-Path $archPair.Dest $exeName)
        }
    }

    foreach ($fileName in $QiQiDataFileNames) {
        $srcFile = Join-Path $QiQiRepoRoot "data\$fileName"
        Copy-Item $srcFile -Destination (Join-Path $pkgDir "data")
    }

    Copy-Item (Join-Path $PSScriptRoot "common.ps1") -Destination (Join-Path $pkgDir "scripts\common.ps1")
    Copy-Item (Join-Path $PSScriptRoot "register_ime.ps1") -Destination (Join-Path $pkgDir "scripts\register_ime.ps1")
    Copy-Item (Join-Path $PSScriptRoot "unregister_ime.ps1") -Destination (Join-Path $pkgDir "scripts\unregister_ime.ps1")
    Copy-Item (Join-Path $QiQiRepoRoot "LICENSE") -Destination $pkgDir
    Copy-Item (Join-Path $QiQiRepoRoot "README.md") -Destination $pkgDir
    Copy-Item (Join-Path $QiQiRepoRoot "docs\deploy.md") -Destination $pkgDir
    Copy-Item (Join-Path $QiQiRepoRoot "docs\log.md") -Destination $pkgDir
    Copy-Item (Join-Path $QiQiRepoRoot "docs\release-notes.md") -Destination $pkgDir

    Compress-Archive -Path $pkgDir -DestinationPath $zipPath -Force
    $zipSizeKB = [math]::Round((Get-Item $zipPath).Length / 1KB, 1)
    Write-QiQiPass "Release ZIP archive created: $zipPath ($zipSizeKB KB)"

    $symbolsDir = Join-Path $distRoot "$pkgName-symbols"
    $symbolsZip = Join-Path $distRoot "$pkgName-symbols.zip"
    Copy-QiQiSymbols -Dlls $dlls -Destination $symbolsDir
    Compress-Archive -Path (Join-Path $symbolsDir "x64"), (Join-Path $symbolsDir "x86") -DestinationPath $symbolsZip -Force
    Write-QiQiPass "Release PDB archive created: $symbolsZip"

    # Build Windows Installer (.msi) package if WiX Toolset CLI is available
    $wixCmd = Get-Command wix -ErrorAction SilentlyContinue
    $wixExe = if ($wixCmd) {
        $wixCmd.Source
    } elseif (Test-Path "$env:USERPROFILE\.dotnet\tools\wix.exe") {
        "$env:USERPROFILE\.dotnet\tools\wix.exe"
    } else {
        $null
    }

    if ($wixExe) {
        # Ensure WiX v7+ EULA is accepted if running WiX 7+
        & $wixExe eula accept wix7 2>$null | Out-Null
        $global:LASTEXITCODE = 0

        Build-QiQiMsiPackages -WixExe $wixExe -StageDir $pkgDir -Destination $distRoot -Version $version
        & (Join-Path $QiQiRepoRoot "tests\installer\packages.ps1") -PackageDirectory $distRoot -Version $version
    } else {
        Write-QiQiWarn "WiX Toolset CLI not found (install via 'dotnet tool install --global wix' to build .msi)"
    }
}
