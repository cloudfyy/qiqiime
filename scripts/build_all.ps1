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
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    Write-QiQiStep "Building $($t.Name) ($Config)"
    & cmake --build $t.BuildDir --config $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-QiQiStep "Testing $($t.Name) ($Config)"
    & ctest --test-dir $t.BuildDir -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
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

    foreach ($fileName in $QiQiDataFileNames) {
        $srcFile = Join-Path $QiQiRepoRoot "data\$fileName"
        Copy-Item $srcFile -Destination (Join-Path $pkgDir "data")
    }

    Copy-Item (Join-Path $PSScriptRoot "common.ps1") -Destination (Join-Path $pkgDir "scripts\common.ps1")
    Copy-Item (Join-Path $PSScriptRoot "register_ime.ps1") -Destination (Join-Path $pkgDir "scripts\register_ime.ps1")
    Copy-Item (Join-Path $PSScriptRoot "unregister_ime.ps1") -Destination (Join-Path $pkgDir "scripts\unregister_ime.ps1")
    Copy-Item (Join-Path $QiQiRepoRoot "LICENSE") -Destination $pkgDir
    Copy-Item (Join-Path $QiQiRepoRoot "README.md") -Destination $pkgDir
    Copy-Item (Join-Path $QiQiRepoRoot "docs\DEPLOYMENT.md") -Destination $pkgDir

    Compress-Archive -Path $pkgDir -DestinationPath $zipPath -Force
    $zipSizeKB = [math]::Round((Get-Item $zipPath).Length / 1KB, 1)
    Write-QiQiPass "Release ZIP archive created: $zipPath ($zipSizeKB KB)"

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
        $wxsPath = Join-Path $QiQiRepoRoot "installer\QiQiIME.wxs"
        # Ensure WiX v7+ EULA is accepted if running WiX 7+
        & $wixExe eula accept wix7 2>$null | Out-Null
        $global:LASTEXITCODE = 0

        foreach ($loc in @(
            @{ Culture = "zh-CN"; Lcid = "2052"; SummaryCp = "936";  Wxl = Join-Path $QiQiRepoRoot "installer\zh-CN.wxl" },
            @{ Culture = "en-US"; Lcid = "1033"; SummaryCp = "1252"; Wxl = Join-Path $QiQiRepoRoot "installer\en-US.wxl" }
        )) {
            $msiPath = Join-Path $distRoot "$pkgName-$($loc.Culture).msi"
            if (Test-Path $msiPath) { Remove-Item -Force $msiPath }
            & $wixExe build $wxsPath -loc $loc.Wxl -culture $loc.Culture -arch x64 `
                -d "ProductVersion=$version" -d "ProductLcid=$($loc.Lcid)" -d "SummaryCodepage=$($loc.SummaryCp)" -d "StageDir=$pkgDir" -o $msiPath
            if ($LASTEXITCODE -ne 0) {
                throw "WiX MSI build ($($loc.Culture)) failed with exit code $LASTEXITCODE"
            }
            $pdbPath = [System.IO.Path]::ChangeExtension($msiPath, ".wixpdb")
            if (Test-Path $pdbPath) { Remove-Item -Force $pdbPath }
            $msiSizeKB = [math]::Round((Get-Item $msiPath).Length / 1KB, 1)
            Write-QiQiPass "Release MSI installer ($($loc.Culture)) created: $msiPath ($msiSizeKB KB)"
        }
    } else {
        Write-QiQiWarn "WiX Toolset CLI not found (install via 'dotnet tool install --global wix' to build .msi)"
    }
}
