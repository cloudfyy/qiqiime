# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BrokerPath,
    [Parameter(Mandatory = $true)]
    [string] $LaunchLowPath,
    [Parameter(Mandatory = $true)]
    [string] $ProbePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path `
        $PSScriptRoot `
        'helpers.ps1')

$broker =
    (Resolve-Path `
        -LiteralPath $BrokerPath).Path
$launchLow =
    (Resolve-Path `
        -LiteralPath $LaunchLowPath).Path
$probe =
    (Resolve-Path `
        -LiteralPath $ProbePath).Path
$subkey =
    "Software\QiQiIME_BrokerHandoffControl_$PID"
$diagnosticLog = Join-Path `
    $env:LOCALAPPDATA `
    'QiQiIME\ConfigBroker\broker-test.log'
$diagnosticLogStart = if (
    Test-Path `
        -LiteralPath $diagnosticLog `
        -PathType Leaf
) {
    (Get-Item `
        -LiteralPath $diagnosticLog).Length
} else {
    0
}
$previousDiagnosticLog =
    [Environment]::GetEnvironmentVariable(
        'QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG',
        'Process')
$previousTestStoreSubkey =
    [Environment]::GetEnvironmentVariable(
        'QIQI_CONFIG_BROKER_TEST_STORE_SUBKEY',
        'Process')
$lowBrokerProcess = $null
$mediumBrokerProcess = $null

function Get-TestBroker {
    param([int[]] $ExceptProcessId = @())

    return Get-CimInstance `
        Win32_Process `
        -Filter "Name = 'QiQiIMEConfigBroker.exe'" `
        -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ProcessId -notin $ExceptProcessId -and
            $_.CommandLine -like
                "*--store-subkey=*$subkey*"
        } |
        Select-Object -First 1
}

function Wait-TestBroker {
    param([int[]] $ExceptProcessId = @())

    $deadline =
        [DateTime]::UtcNow.AddSeconds(15)
    do {
        $process =
            Get-TestBroker `
                -ExceptProcessId $ExceptProcessId
        if (-not $process) {
            Start-Sleep `
                -Milliseconds 50
        }
    } while (
        -not $process -and
        [DateTime]::UtcNow -lt $deadline)
    if (-not $process) {
        throw 'Replacement Broker did not start.'
    }
    return Get-Process `
        -Id $process.ProcessId
}

function Assert-BrokerIntegrity {
    param(
        [int] $ProcessId,
        [int] $IntegrityRid
    )

    $deadline =
        [DateTime]::UtcNow.AddSeconds(15)
    do {
        $tail =
            Read-QiQiBrokerLogTail `
                -Path $diagnosticLog `
                -Start $diagnosticLogStart
        $pattern =
            'Broker started .*' +
            "integrity_rid=$IntegrityRid, " +
            "pid=$ProcessId\)"
        if ($tail -match $pattern) {
            return
        }
        Start-Sleep `
            -Milliseconds 50
    } while (
        [DateTime]::UtcNow -lt $deadline)
    throw (
        "Broker PID $ProcessId did not report " +
        "integrity RID $IntegrityRid.")
}

function Stop-TestBroker {
    $shutdown =
        Start-Process `
            -FilePath $broker `
            -ArgumentList @(
                '--shutdown',
                "--store-subkey=$subkey") `
            -WindowStyle Hidden `
            -Wait `
            -PassThru
    if ($shutdown.ExitCode -ne 0) {
        throw (
            'Broker shutdown failed with exit code ' +
            "$($shutdown.ExitCode).")
    }
}

try {
    $env:QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG = '1'
    $env:QIQI_CONFIG_BROKER_TEST_STORE_SUBKEY =
        $subkey

    $lowOutput =
        & $launchLow `
            $broker `
            '-Embedding' `
            "--store-subkey=$subkey"
    if ($LASTEXITCODE -ne 0) {
        throw (
            'Low Broker launcher failed with exit code ' +
            "$LASTEXITCODE.")
    }
    $lowPid =
        [int]($lowOutput |
            Select-Object -Last 1)
    $lowBrokerProcess =
        Get-Process `
            -Id $lowPid

    & $probe '--handoff-control'
    if ($LASTEXITCODE -ne 0) {
        throw (
            'Low-to-Medium handoff failed with exit code ' +
            "$LASTEXITCODE.")
    }
    if (-not
            $lowBrokerProcess.WaitForExit(
                5000)) {
        throw 'Low Broker did not exit during handoff.'
    }
    $mediumBrokerProcess =
        Wait-TestBroker `
            -ExceptProcessId $lowPid
    Assert-BrokerIntegrity `
        -ProcessId $mediumBrokerProcess.Id `
        -IntegrityRid 8192

    Stop-TestBroker
    if (-not
            $mediumBrokerProcess.WaitForExit(
                5000)) {
        throw 'Medium Broker did not stop.'
    }

    & $probe '--handoff-control'
    if ($LASTEXITCODE -ne 0) {
        throw (
            'Missing-Broker handoff failed with exit code ' +
            "$LASTEXITCODE.")
    }
    $mediumBrokerProcess =
        Wait-TestBroker
    Assert-BrokerIntegrity `
        -ProcessId $mediumBrokerProcess.Id `
        -IntegrityRid 8192
    $replacementPid =
        $mediumBrokerProcess.Id

    & $probe '--handoff-control'
    if ($LASTEXITCODE -ne 0) {
        throw (
            'Writable-Broker handoff failed with exit code ' +
            "$LASTEXITCODE.")
    }
    $unchangedProcess =
        Get-TestBroker
    if (-not $unchangedProcess -or
            $unchangedProcess.ProcessId -ne
                $replacementPid) {
        throw (
            'Writable-Broker fast path replaced ' +
            'the active Broker.')
    }

    Write-Host (
        '[PASS] Broker control handoff covered ' +
        'Low stop, missing start, and writable fast path.')
} finally {
    Stop-TestBroker
    if ($mediumBrokerProcess -and
            -not $mediumBrokerProcess.HasExited) {
        if (-not
                $mediumBrokerProcess.WaitForExit(
                    5000)) {
            throw 'Replacement Broker did not exit in cleanup.'
        }
    }
    if ($lowBrokerProcess -and
            -not $lowBrokerProcess.HasExited) {
        if (-not
                $lowBrokerProcess.WaitForExit(
                    5000)) {
            throw 'Low Broker did not exit in cleanup.'
        }
    }
    if ($previousDiagnosticLog) {
        $env:QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG =
            $previousDiagnosticLog
    } else {
        Remove-Item `
            Env:QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG `
            -ErrorAction SilentlyContinue
    }
    if ($previousTestStoreSubkey) {
        $env:QIQI_CONFIG_BROKER_TEST_STORE_SUBKEY =
            $previousTestStoreSubkey
    } else {
        Remove-Item `
            Env:QIQI_CONFIG_BROKER_TEST_STORE_SUBKEY `
            -ErrorAction SilentlyContinue
    }
    Remove-Item `
        -LiteralPath "Registry::HKEY_CURRENT_USER\$subkey" `
        -Recurse `
        -Force `
        -ErrorAction SilentlyContinue
}
