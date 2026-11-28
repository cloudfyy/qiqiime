# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string] $PackageVersion = '0.2.0.0',
    [switch] $SkipSigning
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-VisualStudioGenerator {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "vswhere.exe was not found: $vswhere"
    }
    $installation = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationVersion
    if (-not $installation) {
        throw 'A Visual Studio C++ desktop workload is required.'
    }
    $major = [int]($installation -split '\.')[0]
    switch ($major) {
        18 { return 'Visual Studio 18 2026' }
        17 { return 'Visual Studio 17 2022' }
        default { throw "Unsupported Visual Studio major version: $major" }
    }
}

$repositoryRoot = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..\..'))
$buildRoot = Join-Path $repositoryRoot `
    'build\hosts\fulltrust'
$stageRoot = Join-Path $buildRoot ('stage-' + [Guid]::NewGuid().ToString('N'))
$packageRoot = Join-Path $repositoryRoot 'dist\test-hosts'
$certificatePath = Join-Path $repositoryRoot `
    'tests\hosts\uwp\dev.cer'
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkVersion = '10.0.26100.0'
$sdkBin = Join-Path $sdkRoot "bin\$sdkVersion\x64"
$makeAppx = Join-Path $sdkBin 'makeappx.exe'
$makePri = Join-Path $sdkBin 'makepri.exe'
$signTool = Join-Path $sdkBin 'signtool.exe'

. (Join-Path $PSScriptRoot '..\Assets.ps1')

foreach ($tool in @($makeAppx, $makePri, $signTool)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required Windows SDK tool was not found: $tool"
    }
}

& (Join-Path $PSScriptRoot 'Validate-Host.ps1')

$cmake = (Get-Command cmake -ErrorAction Stop).Source
& $cmake `
    -S $PSScriptRoot `
    -B $buildRoot `
    -G (Resolve-VisualStudioGenerator) `
    -A x64
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}
& $cmake `
    --build $buildRoot `
    --config $Configuration `
    --target QiQiImePackagedFullTrustHost
if ($LASTEXITCODE -ne 0) {
    throw "Full-trust host build failed with exit code $LASTEXITCODE."
}

$builtExecutable = Join-Path $buildRoot "$Configuration\QiQiImePackagedFullTrustHost.exe"
if (!(Test-Path -LiteralPath $builtExecutable -PathType Leaf)) {
    throw 'The built QiQiImePackagedFullTrustHost.exe was not found.'
}
$manifest = [xml](Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Package.appxmanifest') -Raw)
$manifestIdentity = $manifest.SelectSingleNode(
    "//*[local-name()='Identity']")
$manifestIdentity.SetAttribute('Version', $PackageVersion)

$certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
    $certificatePath)
try {
    if ($certificate.HasPrivateKey) {
        throw 'The shared source development certificate contains a private key.'
    }
    New-Item -ItemType Directory -Path $stageRoot, $packageRoot -Force | Out-Null
    Copy-Item -LiteralPath $builtExecutable -Destination $stageRoot
    $manifestIdentity.SetAttribute(
        'Publisher',
        $certificate.Subject)
    $manifest.Save((Join-Path $stageRoot 'AppxManifest.xml'))

    New-QiQiHostPackageAssets `
        -OutputDirectory (Join-Path $stageRoot 'Assets') `
        -IconPath (Join-Path $buildRoot 'generated\icon\full.ico')

    $priConfig = Join-Path $buildRoot 'priconfig.xml'
    & $makePri createconfig /cf $priConfig /dq en-US_zh-CN /o
    if ($LASTEXITCODE -ne 0) {
        throw "PRI configuration failed with exit code $LASTEXITCODE."
    }
    & $makePri new `
        /pr $stageRoot `
        /cf $priConfig `
        /mn (Join-Path $stageRoot 'AppxManifest.xml') `
        /of (Join-Path $stageRoot 'resources.pri') `
        /o
    if ($LASTEXITCODE -ne 0) {
        throw "PRI generation failed with exit code $LASTEXITCODE."
    }

    $packagePath = Join-Path $packageRoot `
        "fulltrust-$PackageVersion.msix"
    if (Test-Path -LiteralPath $packagePath) {
        Remove-Item -LiteralPath $packagePath -Force
    }
    & $makeAppx pack /d $stageRoot /p $packagePath /l /o
    if ($LASTEXITCODE -ne 0) {
        throw "MSIX packaging failed with exit code $LASTEXITCODE."
    }

    if (-not $SkipSigning) {
        $privateCertificate = Get-ChildItem `
            -LiteralPath 'Cert:\CurrentUser\My' |
            Where-Object {
                $_.Thumbprint -eq $certificate.Thumbprint -and
                $_.HasPrivateKey
            } |
            Select-Object -First 1
        if (-not $privateCertificate) {
            throw (
                "The private key for $certificatePath is not available " +
                'in Cert:\CurrentUser\My.')
        }
        & $signTool sign `
            /fd SHA256 `
            /s My `
            /sha1 $privateCertificate.Thumbprint `
            $packagePath
        if ($LASTEXITCODE -ne 0) {
            throw "MSIX signing failed with exit code $LASTEXITCODE."
        }
    }

    if (-not $SkipSigning) {
        & (Join-Path $PSScriptRoot 'Validate-Host.ps1') `
            -PackagePath $packagePath `
            -ExecutablePath $builtExecutable
    }

    Write-Host "Built packaged full-trust host: $packagePath"
    if ($SkipSigning) {
        Write-Host 'Unsigned package requested; it cannot be installed directly.'
    }
} finally {
    $certificate.Dispose()
    if (Test-Path -LiteralPath $stageRoot -PathType Container) {
        Remove-Item -LiteralPath (Resolve-Path -LiteralPath $stageRoot).Path -Recurse -Force
    }
}

