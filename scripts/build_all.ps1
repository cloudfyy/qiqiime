# Build and test both x64 and x86 (Win32) architectures for QiQi Wubi IME
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$Generator = "Visual Studio 18 2026"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

$targets = @(
    @{ Name = "x64"; Arch = "x64";   BuildDir = Join-Path $repoRoot "build\x64" },
    @{ Name = "x86"; Arch = "Win32"; BuildDir = Join-Path $repoRoot "build\x86" }
)

foreach ($t in $targets) {
    $cacheFile = Join-Path $t.BuildDir "CMakeCache.txt"
    if (-not (Test-Path $cacheFile)) {
        Write-Host "=== Configuring $($t.Name) ($($t.Arch)) ===" -ForegroundColor Cyan
        & cmake -S $repoRoot -B $t.BuildDir -G $Generator -A $t.Arch
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    Write-Host "=== Building $($t.Name) ($Config) ===" -ForegroundColor Cyan
    & cmake --build $t.BuildDir --config $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "=== Testing $($t.Name) ($Config) ===" -ForegroundColor Cyan
    & ctest --test-dir $t.BuildDir -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "Both x64 and x86 ($Config) builds and unit tests succeeded!" -ForegroundColor Green
