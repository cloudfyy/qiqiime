# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string] $PackageVersion = '0.1.0.0',
    [string] $CertificatePath,
    [securestring] $CertificatePassword,
    [switch] $RotateDevelopmentCertificate,
    [switch] $SkipSigning
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Convert-SecureStringToPlainText {
    param([securestring] $Value)
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Value)
    try {
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    } finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    }
    $signerThumbprint = $certificate.Thumbprint
}

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
$buildRoot = Join-Path $repositoryRoot 'build\hosts\uwp-search'
$stageRoot = Join-Path $buildRoot 'staging'
$certificateRoot = Join-Path $buildRoot 'certificate'
$sourceCertificatePath = Join-Path $PSScriptRoot `
    'cert\QiQiImeDevelopment.cer'
$packageRoot = Join-Path $repositoryRoot 'dist\test-hosts'
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkVersion = '10.0.26100.0'
$sdkBin = Join-Path $sdkRoot "bin\$sdkVersion\x64"
$makeAppx = Join-Path $sdkBin 'makeappx.exe'
$makePri = Join-Path $sdkBin 'makepri.exe'
$signTool = Join-Path $sdkBin 'signtool.exe'

foreach ($tool in @($makeAppx, $makePri, $signTool)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required Windows SDK tool was not found: $tool"
    }
}

& (Join-Path $PSScriptRoot 'Validate-Host.ps1')

if (Test-Path -LiteralPath $buildRoot) {
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
}
New-Item -ItemType Directory `
    -Path $stageRoot, (Join-Path $stageRoot 'Assets'), $certificateRoot, $packageRoot `
    -Force | Out-Null

$cmake = (Get-Command cmake -ErrorAction Stop).Source
$generator = Resolve-VisualStudioGenerator
& $cmake `
    -S $PSScriptRoot `
    -B $buildRoot `
    -G $generator `
    -A x64
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}
& $cmake `
    --build $buildRoot `
    --config $Configuration `
    --target QiQiImeUwpSearchHost
if ($LASTEXITCODE -ne 0) {
    throw "UWP host build failed with exit code $LASTEXITCODE."
}

$builtExecutable = Get-ChildItem -LiteralPath $buildRoot `
    -Recurse `
    -Filter 'QiQiImeUwpSearchHost.exe' `
    -File |
    Where-Object FullName -notlike "*\staging\*" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $builtExecutable) {
    throw 'The built QiQiImeUwpSearchHost.exe was not found.'
}
Copy-Item -LiteralPath $builtExecutable.FullName `
    -Destination (Join-Path $stageRoot 'QiQiImeUwpSearchHost.exe')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Package.appxmanifest') `
    -Destination (Join-Path $stageRoot 'AppxManifest.xml')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'TestData.json') `
    -Destination (Join-Path $stageRoot 'Assets\TestData.json')

$manifest = [xml](Get-Content -LiteralPath (
    Join-Path $stageRoot 'AppxManifest.xml') -Raw)
$manifestIdentity = $manifest.SelectSingleNode(
    "//*[local-name()='Identity']")
$manifestIdentity.SetAttribute('Version', $PackageVersion)

$selectedCertificate = $CertificatePath
$selectedPassword = $CertificatePassword
$generatedCertificate = $false
$certificate = $null
$storeCertificate = $false
$signerThumbprint = $null

if (-not $SkipSigning) {
    if (-not $selectedCertificate) {
        $sourceCertificate = $null
        if (-not $RotateDevelopmentCertificate) {
            if (-not (Test-Path -LiteralPath $sourceCertificatePath -PathType Leaf)) {
                throw "The checked-in public development certificate is missing: $sourceCertificatePath"
            }
            $sourceCertificate = [
                Security.Cryptography.X509Certificates.X509Certificate2]::new(
                    $sourceCertificatePath)
            try {
                $certificate = Get-ChildItem `
                    -LiteralPath 'Cert:\CurrentUser\My' |
                    Where-Object {
                        $_.Thumbprint -eq $sourceCertificate.Thumbprint -and
                        $_.HasPrivateKey -and
                        $_.NotAfter -gt (Get-Date).AddDays(30)
                    } |
                    Select-Object -First 1
                if (-not $certificate) {
                    throw (
                        "The private key matching $sourceCertificatePath is not " +
                        'available in Cert:\CurrentUser\My. Import that PFX, pass ' +
                        '-CertificatePath/-CertificatePassword, or explicitly use ' +
                        '-RotateDevelopmentCertificate.')
                }
            } finally {
                $sourceCertificate.Dispose()
                $sourceCertificate = $null
            }
        } else {
            $certificate = New-SelfSignedCertificate `
                -Type Custom `
                -KeyUsage DigitalSignature `
                -Subject 'CN=QiQiIME Development' `
                -KeyAlgorithm RSA `
                -KeyLength 2048 `
                -HashAlgorithm SHA256 `
                -CertStoreLocation 'Cert:\CurrentUser\My' `
                -NotAfter (Get-Date).AddYears(2) `
                -TextExtension @(
                    '2.5.29.37={text}1.3.6.1.5.5.7.3.3',
                    '2.5.29.19={text}')
            $generatedCertificate = $true
            New-Item -ItemType Directory `
                -Path (Split-Path -Parent $sourceCertificatePath) `
                -Force | Out-Null
            Export-Certificate `
                -Cert $certificate.PSPath `
                -FilePath $sourceCertificatePath `
                -Force | Out-Null
        }
        Export-Certificate `
            -Cert $certificate.PSPath `
            -FilePath (Join-Path $certificateRoot 'QiQiImeDevelopment.cer') |
            Out-Null
        $manifestIdentity.SetAttribute(
            'Publisher',
            $certificate.Subject)
        $storeCertificate = $true
    } elseif (-not $selectedPassword) {
        throw 'CertificatePassword is required when CertificatePath is supplied.'
    } else {
        $plainPassword = Convert-SecureStringToPlainText $selectedPassword
        try {
            $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
                $selectedCertificate,
                $plainPassword,
                [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet)
            $manifestIdentity.SetAttribute('Publisher', $certificate.Subject)
            if (-not $certificate.HasPrivateKey) {
                throw "The selected certificate has no private key: $selectedCertificate"
            }
        } finally {
            $plainPassword = $null
        }
    }
}

$manifest.Save((Join-Path $stageRoot 'AppxManifest.xml'))

Add-Type -AssemblyName System.Drawing
$icon = [Drawing.Icon]::new(
    (Join-Path $repositoryRoot 'data\qiqiime.ico'))
try {
    $assets = @(
        [pscustomobject]@{
            Name = 'StoreLogo.png'; Width = 50; Height = 50
        },
        [pscustomobject]@{
            Name = 'Square44x44Logo.png'; Width = 44; Height = 44
        },
        [pscustomobject]@{
            Name = 'Square150x150Logo.png'; Width = 150; Height = 150
        },
        [pscustomobject]@{
            Name = 'Wide310x150Logo.png'; Width = 310; Height = 150
        },
        [pscustomobject]@{
            Name = 'SplashScreen.png'; Width = 620; Height = 300
        }
    )
    foreach ($asset in $assets) {
        $bitmap = [Drawing.Bitmap]::new(
            $asset.Width,
            $asset.Height)
        try {
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear(
                    [Drawing.Color]::FromArgb(255, 31, 31, 31))
                $graphics.SmoothingMode =
                    [Drawing.Drawing2D.SmoothingMode]::HighQuality
                $side = [int]([Math]::Min(
                    $asset.Width,
                    $asset.Height) * 0.70)
                $x = [int](($asset.Width - $side) / 2)
                $y = [int](($asset.Height - $side) / 2)
                $graphics.DrawIcon(
                    $icon,
                    [Drawing.Rectangle]::new($x, $y, $side, $side))
            } finally {
                $graphics.Dispose()
            }
            $bitmap.Save(
                (Join-Path $stageRoot "Assets\$($asset.Name)"),
                [Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $bitmap.Dispose()
        }
    }
} finally {
    $icon.Dispose()
}

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
    "QiQiImeUwpSearchHost_$PackageVersion.msix"
if (Test-Path -LiteralPath $packagePath) {
    Remove-Item -LiteralPath $packagePath -Force
}
& $makeAppx pack /d $stageRoot /p $packagePath /l /o
if ($LASTEXITCODE -ne 0) {
    throw "MSIX packaging failed with exit code $LASTEXITCODE."
}

if (-not $SkipSigning) {
    if ($storeCertificate) {
        & $signTool sign `
            /fd SHA256 `
            /s My `
            /sha1 $certificate.Thumbprint `
            $packagePath
    } else {
        $plainPassword = Convert-SecureStringToPlainText $selectedPassword
        try {
            & $signTool sign `
                /fd SHA256 `
                /f $selectedCertificate `
                /p $plainPassword `
                $packagePath
        } finally {
            $plainPassword = $null
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "MSIX signing failed with exit code $LASTEXITCODE."
    }
}

if ($certificate) {
    $certificate.Dispose()
}

if (-not $SkipSigning) {
    & (Join-Path $PSScriptRoot 'Validate-Host.ps1') `
        -PackagePath $packagePath `
        -ExpectedPublisher $manifestIdentity.GetAttribute('Publisher') `
        -ExpectedSignerThumbprint $signerThumbprint
}

Write-Host "Built UWP host package: $packagePath"
if ($SkipSigning) {
    Write-Host 'Unsigned package requested; it cannot be installed directly.'
} elseif ($generatedCertificate) {
    Write-Host "Rotated public test certificate: $sourceCertificatePath"
}
