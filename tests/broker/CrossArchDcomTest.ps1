# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [string] $X64BuildDirectory = (Join-Path `
            $PSScriptRoot `
            '..\..\..\build\x64'),
    [string] $X86BuildDirectory = (Join-Path `
            $PSScriptRoot `
            '..\..\..\build\x86')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path `
        $PSScriptRoot `
        'helpers.ps1')

$x64Build = (Resolve-Path -LiteralPath $X64BuildDirectory).Path
$x86Build = (Resolve-Path -LiteralPath $X86BuildDirectory).Path
$broker = Join-Path $x64Build "$Configuration\QiQiIMEConfigBroker.exe"
$proxy32 = Join-Path $x86Build "$Configuration\QiQiIMEConfigProxy.dll"
$probe32 = Join-Path $x86Build "$Configuration\qiqi_broker_probe.exe"
$subkey = "Software\QiQiIME_CrossArchDcom_$PID"
$server = $null
$client = $null
$diagnosticLog = Join-Path `
    $env:LOCALAPPDATA `
    'QiQiIME\ConfigBroker\broker-test.log'
$diagnosticLogStart = if (
    Test-Path -LiteralPath $diagnosticLog -PathType Leaf
) {
    (Get-Item -LiteralPath $diagnosticLog).Length
} else {
    0
}

foreach ($file in @($broker, $proxy32, $probe32)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required cross-architecture file was not found: $file"
    }
}

Stop-QiQiExistingBroker

$previousPerUser = [Environment]::GetEnvironmentVariable(
    'QIQI_CONFIG_BROKER_PER_USER',
    'Process')
$previousProxy32 = [Environment]::GetEnvironmentVariable(
    'QIQI_CONFIG_BROKER_PROXY32_PATH',
    'Process')
$previousDiagnosticLog = [Environment]::GetEnvironmentVariable(
    'QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG',
    'Process')

$useMachineRegistration =
    Test-QiQiAdministrator
if ($useMachineRegistration) {
    Assert-QiQiBrokerMachineRegistrationAbsent `
        -FailurePrefix (
            'Elevated cross-architecture test refuses to ' +
            'overwrite an existing machine registration:')
}

try {
    if ($useMachineRegistration) {
        Remove-Item `
            Env:QIQI_CONFIG_BROKER_PER_USER `
            -ErrorAction SilentlyContinue
    } else {
        $env:QIQI_CONFIG_BROKER_PER_USER = '1'
    }
    $env:QIQI_CONFIG_BROKER_PROXY32_PATH = $proxy32
    $env:QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG = '1'
    $registration = Start-Process `
        -FilePath $broker `
        -ArgumentList @(
            '--register'
            "--store-subkey=$subkey") `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($registration.ExitCode -ne 0) {
        throw "Cross-view registration failed: $($registration.ExitCode)."
    }

    $server = Start-Process `
        -FilePath $broker `
        -ArgumentList @(
            '-Embedding',
            "--store-subkey=$subkey") `
        -WindowStyle Hidden `
        -PassThru
    Wait-QiQiBrokerReady `
        -Process $server `
        -DiagnosticLog $diagnosticLog `
        -DiagnosticLogStart $diagnosticLogStart

    $stdout = Join-Path $env:TEMP "qiqi-cross-$PID.out.txt"
    $stderr = Join-Path $env:TEMP "qiqi-cross-$PID.err.txt"
    $client = Start-Process `
        -FilePath $probe32 `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr `
        -PassThru
    if (-not $client.WaitForExit(30000)) {
        Stop-Process -Id $client.Id -Force
        throw 'x86 client timed out while activating the x64 Broker.'
    }
    if ($client.ExitCode -ne 0) {
        if (Test-Path $stdout) {
            Get-Content $stdout | Write-Error
        }
        if (Test-Path $stderr) {
            Get-Content $stderr | Write-Error
        }
        throw "x86 client failed with exit code $($client.ExitCode)."
    }
    Remove-Item $stdout, $stderr `
        -Force `
        -ErrorAction SilentlyContinue
    Write-Host 'x64 Broker served the x86 native-async DCOM client.'
} finally {
    if ($server -and
        -not $server.HasExited) {
        $shutdown = Start-Process `
            -FilePath $broker `
            -ArgumentList @(
                '--shutdown',
                "--store-subkey=$subkey") `
            -WindowStyle Hidden `
            -Wait `
            -PassThru
        if ($shutdown.ExitCode -ne 0 -or
            -not $server.WaitForExit(5000)) {
            Stop-Process -Id $server.Id -Force
        }
    }
    $unregister = Start-Process `
        -FilePath $broker `
        -ArgumentList '--unregister' `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($unregister.ExitCode -ne 0) {
        Write-Warning "Cross-view unregister exited with code $($unregister.ExitCode)."
    }
    Remove-Item `
        -LiteralPath "Registry::HKEY_CURRENT_USER\$subkey" `
        -Recurse `
        -Force `
        -ErrorAction SilentlyContinue
    if ($previousPerUser) {
        $env:QIQI_CONFIG_BROKER_PER_USER =
            $previousPerUser
    } else {
        Remove-Item Env:QIQI_CONFIG_BROKER_PER_USER `
            -ErrorAction SilentlyContinue
    }
    if ($previousProxy32) {
        $env:QIQI_CONFIG_BROKER_PROXY32_PATH =
            $previousProxy32
    } else {
        Remove-Item Env:QIQI_CONFIG_BROKER_PROXY32_PATH `
            -ErrorAction SilentlyContinue
    }
    if ($previousDiagnosticLog) {
        $env:QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG =
            $previousDiagnosticLog
    } else {
        Remove-Item Env:QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG `
            -ErrorAction SilentlyContinue
    }
    if ($useMachineRegistration) {
        Assert-QiQiBrokerMachineRegistrationAbsent `
            -FailurePrefix (
                'Broker unregister left a machine registration:')
    }
}
