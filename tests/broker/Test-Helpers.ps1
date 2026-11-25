# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

function Read-QiQiBrokerLogTail {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path,
        [long] $Start = 0
    )

    $stream =
        [IO.FileStream]::new(
            $Path,
            [IO.FileMode]::Open,
            [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite)
    try {
        if ($stream.Length -le $Start) {
            return ''
        }
        $stream.Position = $Start
        $reader =
            [IO.StreamReader]::new(
                $stream,
                [Text.UTF8Encoding]::new(
                    $false),
                $true,
                1024,
                $true)
        try {
            return $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function Wait-QiQiBrokerReady {
    param(
        [Parameter(Mandatory = $true)]
        [Diagnostics.Process] $Process,
        [Parameter(Mandatory = $true)]
        [string] $DiagnosticLog,
        [long] $DiagnosticLogStart = 0,
        [int] $TimeoutMilliseconds = 15000
    )

    $deadline =
        [DateTime]::UtcNow.AddMilliseconds(
            $TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) {
            throw (
                'Broker exited during startup with code ' +
                "$($Process.ExitCode).")
        }
        if (Test-Path `
                -LiteralPath $DiagnosticLog `
                -PathType Leaf) {
            $tail =
                Read-QiQiBrokerLogTail `
                    -Path $DiagnosticLog `
                    -Start $DiagnosticLogStart
            $readyPattern =
                'Broker started .*' +
                "pid=$($Process.Id)\)"
            if ($tail -match $readyPattern) {
                return
            }
        }
        Start-Sleep -Milliseconds 50
    }
    throw (
        'Broker class factory did not become ready ' +
        "within $TimeoutMilliseconds ms.")
}
