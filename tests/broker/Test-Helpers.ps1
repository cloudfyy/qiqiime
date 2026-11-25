# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

function Test-QiQiAdministrator {
    $principal =
        [Security.Principal.WindowsPrincipal]::new(
            [Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-QiQiBrokerMachineRegistrationAbsent {
    param(
        [Parameter(Mandatory = $true)]
        [string] $FailurePrefix
    )

    $paths = @(
        'SOFTWARE\Classes\CLSID\{8E7B9D31-5A42-4C81-9F11-20261029000D}'
        'SOFTWARE\Classes\AppID\{8E7B9D31-5A42-4C81-9F11-20261029000E}'
        'SOFTWARE\Classes\CLSID\{8E7B9D31-5A42-4C81-9F11-202610290020}'
        'SOFTWARE\Classes\Interface\{8E7B9D31-5A42-4C81-9F11-202610290020}'
        'SOFTWARE\Classes\Interface\{8E7B9D31-5A42-4C81-9F11-202610290021}')
    $views =
        if ([Environment]::Is64BitOperatingSystem) {
            @(
                [Microsoft.Win32.RegistryView]::Registry64
                [Microsoft.Win32.RegistryView]::Registry32)
        } else {
            @([Microsoft.Win32.RegistryView]::Default)
        }
    foreach ($view in $views) {
        $base =
            [Microsoft.Win32.RegistryKey]::OpenBaseKey(
                [Microsoft.Win32.RegistryHive]::LocalMachine,
                $view)
        try {
            foreach ($path in $paths) {
                $key = $base.OpenSubKey($path)
                if ($key) {
                    $key.Dispose()
                    throw (
                        "$FailurePrefix " +
                        "$($view.ToString()) $path")
                }
            }
        } finally {
            $base.Dispose()
        }
    }
}

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
