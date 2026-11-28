# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [Parameter(Mandatory = $true)][string]$ExecutablePath,
    [Parameter(Mandatory = $true)][string]$IconPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class QiQiFullTrustTestWindow {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll", EntryPoint = "SendMessageTimeoutW", SetLastError = true)]
    public static extern IntPtr SendMessageTimeout(IntPtr window, uint message,
        UIntPtr wparam, IntPtr lparam, uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll", EntryPoint = "GetClassLongPtrW", SetLastError = true)]
    public static extern IntPtr GetClassLongPtr(IntPtr window, int index);
}
'@

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-WindowIcon([IntPtr]$Handle) {
    Assert-True ($Handle -ne [IntPtr]::Zero) 'The host window icon is missing.'
    $actualIcon = [Drawing.Icon]::FromHandle($Handle)
    try {
        $expectedIcon = [Drawing.Icon]::new($IconPath, $actualIcon.Size)
        try {
            $actual = $actualIcon.ToBitmap()
            try {
                $expected = $expectedIcon.ToBitmap()
                try {
                    Assert-True ($actual.Size -eq $expected.Size) 'Window icon dimensions differ from the generated host icon.'
                    for ($y = 0; $y -lt $actual.Height; $y++) {
                        for ($x = 0; $x -lt $actual.Width; $x++) {
                            Assert-True ($actual.GetPixel($x, $y).ToArgb() -eq $expected.GetPixel($x, $y).ToArgb()) `
                                'The runtime window icon differs from the generated host icon.'
                        }
                    }
                } finally {
                    $expected.Dispose()
                }
            } finally {
                $actual.Dispose()
            }
        } finally {
            $expectedIcon.Dispose()
        }
    } finally {
        $actualIcon.Dispose()
    }
}

Assert-True (Test-Path -LiteralPath $ExecutablePath -PathType Leaf) 'The Full Trust executable is missing.'
Assert-True (Test-Path -LiteralPath $IconPath -PathType Leaf) 'The generated Full Trust icon is missing.'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('qiqi-fulltrust-ui-' + [guid]::NewGuid().ToString('N'))
$process = [Diagnostics.Process]::new()
$started = $false
try {
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $process.StartInfo = [Diagnostics.ProcessStartInfo]::new($ExecutablePath)
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.WorkingDirectory = Split-Path -Parent $ExecutablePath
    $process.StartInfo.EnvironmentVariables['LOCALAPPDATA'] = $fixture
    $started = $process.Start()
    Assert-True $started 'The Full Trust host did not start.'
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    $window = [IntPtr]::Zero
    while ($deadline.ElapsedMilliseconds -lt 15000) {
        $process.Refresh()
        Assert-True (-not $process.HasExited) 'The Full Trust host exited before creating its window.'
        $window = $process.MainWindowHandle
        if ($window -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 50
    }
    Assert-True ($window -ne [IntPtr]::Zero) 'The Full Trust host did not create its window.'
    $input = [QiQiFullTrustTestWindow]::GetDlgItem($window, 100)
    Assert-True ($input -ne [IntPtr]::Zero) 'The Full Trust input control is missing.'
    $inputLength = [UIntPtr]::Zero
    $messageResult = [QiQiFullTrustTestWindow]::SendMessageTimeout(
        $input, 0x000E, [UIntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$inputLength)
    $messageError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    Assert-True ($messageResult -ne [IntPtr]::Zero) "Reading the actual input length failed (error=$messageError)."
    Assert-True ($inputLength.ToUInt64() -eq 0) `
        'The Full Trust input must start empty rather than contain instructional text.'
    Assert-WindowIcon ([QiQiFullTrustTestWindow]::GetClassLongPtr($window, -14))
    Assert-WindowIcon ([QiQiFullTrustTestWindow]::GetClassLongPtr($window, -34))
    Assert-True ($process.CloseMainWindow()) 'The Full Trust host window could not close.'
    Assert-True ($process.WaitForExit(10000)) 'The Full Trust host did not exit after closing.'
    Assert-True ($process.ExitCode -eq 0) 'The Full Trust host failed during shutdown.'
    Write-Host 'Full Trust initially empty input and actual large/small window icons passed.'
} finally {
    try {
        if ($started -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
            Assert-True ($process.WaitForExit(5000)) 'The test host did not terminate.'
        }
    } finally {
        $process.Dispose()
        if (Test-Path -LiteralPath $fixture) {
            Remove-Item -LiteralPath (Resolve-Path -LiteralPath $fixture).Path -Recurse -Force
        }
    }
}
