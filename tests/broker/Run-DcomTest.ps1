# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [string] $BuildDirectory = (Join-Path `
            $PSScriptRoot `
            '..\..\..\build\x64')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$broker = Join-Path $build "$Configuration\QiQiIMEConfigBroker.exe"
$proxy = Join-Path $build "$Configuration\QiQiIMEConfigProxy.dll"
$probe = Join-Path $build "$Configuration\qiqi_broker_probe.exe"
$subkey = "Software\QiQiIME_BrokerDcom_$PID"
$brokerProcess = $null
$previousDiagnosticLog = [Environment]::GetEnvironmentVariable(
    'QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG',
    'Process')
$previousPerUser = [Environment]::GetEnvironmentVariable(
    'QIQI_CONFIG_BROKER_PER_USER',
    'Process')

foreach ($file in @($broker, $proxy, $probe)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required file was not found: $file"
    }
}

try {
    $env:QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG = '1'
    $env:QIQI_CONFIG_BROKER_PER_USER = '1'
    $registration = Start-Process `
        -FilePath $broker `
        -ArgumentList '--register' `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($registration.ExitCode -ne 0) {
        throw "Broker registration failed with exit code $($registration.ExitCode)."
    }
    $directLaunch = Start-Process `
        -FilePath $broker `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($directLaunch.ExitCode -eq 0) {
        throw 'Broker accepted a direct non-COM launch.'
    }

    $argument = "--store-subkey=$subkey"
    $brokerProcess = Start-Process `
        -FilePath $broker `
        -ArgumentList @('-Embedding', $argument) `
        -WindowStyle Hidden `
        -PassThru
    Start-Sleep -Milliseconds 500
    if ($brokerProcess.HasExited) {
        throw "Broker exited during startup with code $($brokerProcess.ExitCode)."
    }
    $duplicate = Start-Process `
        -FilePath $broker `
        -ArgumentList @('-Embedding', $argument) `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($duplicate.ExitCode -ne 0) {
        throw "Duplicate broker did not exit cleanly: $($duplicate.ExitCode)."
    }
    if ($brokerProcess.HasExited) {
        throw 'Original broker exited after duplicate-session launch.'
    }

    & $probe
    if ($LASTEXITCODE -ne 0) {
        throw "DCOM probe failed with exit code $LASTEXITCODE."
    }
} finally {
    if ($brokerProcess -and
        -not $brokerProcess.HasExited) {
        $shutdownProcess = Start-Process `
            -FilePath $broker `
            -ArgumentList @(
                '--shutdown',
                "--store-subkey=$subkey") `
            -WindowStyle Hidden `
            -Wait `
            -PassThru
        if ($shutdownProcess.ExitCode -ne 0) {
            Write-Warning "Broker shutdown exited with code $($shutdownProcess.ExitCode)."
        }
        if (-not $brokerProcess.WaitForExit(5000)) {
            Stop-Process -Id $brokerProcess.Id -Force
        }
    }
    $unregistration = Start-Process `
        -FilePath $broker `
        -ArgumentList '--unregister' `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($unregistration.ExitCode -ne 0) {
        Write-Warning "Broker unregistration exited with code $($unregistration.ExitCode)."
    }
    if ($previousDiagnosticLog) {
        $env:QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG =
            $previousDiagnosticLog
    } else {
        Remove-Item Env:QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG `
            -ErrorAction SilentlyContinue
    }
    if ($previousPerUser) {
        $env:QIQI_CONFIG_BROKER_PER_USER =
            $previousPerUser
    } else {
        Remove-Item Env:QIQI_CONFIG_BROKER_PER_USER `
            -ErrorAction SilentlyContinue
    }
    Remove-Item `
        -LiteralPath "Registry::HKEY_CURRENT_USER\$subkey" `
        -Recurse `
        -Force `
        -ErrorAction SilentlyContinue
}
