# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BrokerPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$broker = (Resolve-Path `
        -LiteralPath $BrokerPath).Path
$temporary = [IO.Path]::GetFullPath(
    (Join-Path `
        $env:TEMP `
        "qiqi-broker-cli-$PID-$([guid]::NewGuid())"))
New-Item `
    -ItemType Directory `
    -Path $temporary `
    -Force |
    Out-Null

function Invoke-BrokerCli {
    param(
        [string[]] $Arguments = @()
    )

    $sequence = [IO.Path]::GetFileNameWithoutExtension(
        [IO.Path]::GetRandomFileName())
    $stdout = Join-Path `
        $temporary `
        "$sequence.stdout.txt"
    $stderr = Join-Path `
        $temporary `
        "$sequence.stderr.txt"
    $process = Start-Process `
        -FilePath $broker `
        -ArgumentList $Arguments `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output = if (
            Test-Path `
                -LiteralPath $stdout `
                -PathType Leaf) {
            Get-Content `
                -LiteralPath $stdout `
                -Raw
        } else {
            ''
        }
        Error = if (
            Test-Path `
                -LiteralPath $stderr `
                -PathType Leaf) {
            Get-Content `
                -LiteralPath $stderr `
                -Raw
        } else {
            ''
        }
    }
}

try {
    $helpCases = @(
        [pscustomobject]@{
            Display = '<none>'
            Arguments = [string[]]@()
        }
        [pscustomobject]@{
            Display = '-h'
            Arguments = [string[]]@('-h')
        }
        [pscustomobject]@{
            Display = '--help'
            Arguments = [string[]]@('--help')
        }
        [pscustomobject]@{
            Display = '/Help'
            Arguments = [string[]]@('/Help')
        }
        [pscustomobject]@{
            Display = '/?'
            Arguments = [string[]]@('/?')
        })
    foreach ($case in $helpCases) {
        $result = Invoke-BrokerCli `
            -Arguments $case.Arguments
        if ($result.ExitCode -ne 0) {
            throw (
                "Broker CLI '$($case.Display)' exited " +
                "with code $($result.ExitCode).")
        }
        if ($result.Output -notmatch 'Usage:') {
            throw (
                "Broker CLI '$($case.Display)' " +
                'did not print usage.')
        }
    }

    $help = (
        Invoke-BrokerCli `
            -Arguments @('--help')).Output
    foreach ($alias in @(
            '/Embedding',
            '/RegisterServer',
            '/UnregisterServer',
            '/Shutdown',
            '/store-subkey=KEY',
            '/Help')) {
        if ($help -notmatch
                [regex]::Escape($alias)) {
            throw "Broker help is missing alias: $alias"
        }
    }

    $invalid = Invoke-BrokerCli `
        -Arguments @('--not-a-broker-option')
    if ($invalid.ExitCode -eq 0) {
        throw 'Broker accepted an unknown command-line option.'
    }
    if ($invalid.Output -notmatch 'Usage:') {
        throw 'Broker invalid-option path did not print usage.'
    }
} finally {
    Remove-Item `
        -LiteralPath $temporary `
        -Recurse `
        -Force `
        -ErrorAction SilentlyContinue
}
