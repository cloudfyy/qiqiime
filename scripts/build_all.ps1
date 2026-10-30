# Build, test, and optionally package both x64 and x86 (Win32) architectures for QiQi Wubi IME
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Package
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$Generator = "Visual Studio 18 2026",
    [switch]$Package
)

. (Join-Path $PSScriptRoot "common.ps1")

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
    Write-QiQiPass "Release archive created: $zipPath ($zipSizeKB KB)"
}
