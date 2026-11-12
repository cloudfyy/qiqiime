# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

param([Parameter(Mandatory = $true)][string]$BinaryDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class QiQiStartupWindow {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr window, StringBuilder name, int capacity);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")]
    public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr window, int command);
}
'@

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Invoke-TestProcessCleanup([System.Diagnostics.Process]$Process) {
    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        Assert-True ($Process.WaitForExit(5000)) "Test process $($Process.Id) did not terminate."
    }
    $Process.Dispose()
}

$tools = @(
    @{ File = 'QiQiIMESettings.exe'; Class = 'QiQiIMESettingsDialogClass'; SingleInstance = $true },
    @{ File = 'QiQiIMEDictManager.exe'; Class = 'QiQiIMEDictManagerWindow'; SingleInstance = $false },
    @{ File = 'QiQiIMELogMonitor.exe'; Class = 'QiQiIMELogMonitorWindow'; SingleInstance = $false }
)
foreach ($tool in $tools) {
    $executable = Join-Path $BinaryDirectory $tool.File
    Assert-True (Test-Path -LiteralPath $executable -PathType Leaf) "Missing executable: $executable"
    for ($attempt = 0; $attempt -lt 2; ++$attempt) {
        $process = Start-Process -FilePath $executable -WorkingDirectory $BinaryDirectory -PassThru
        try {
            $deadline = [System.Diagnostics.Stopwatch]::StartNew()
            $window = [IntPtr]::Zero
            while ($deadline.ElapsedMilliseconds -lt 15000) {
                $process.Refresh()
                Assert-True (-not $process.HasExited) "$($tool.File) exited before showing its main window."
                $window = $process.MainWindowHandle
                if ($window -ne [IntPtr]::Zero -and [QiQiStartupWindow]::IsWindowVisible($window)) { break }
                Start-Sleep -Milliseconds 50
            }
            Assert-True ($window -ne [IntPtr]::Zero -and [QiQiStartupWindow]::IsWindowVisible($window)) "$($tool.File) did not show a visible window."
            $name = [System.Text.StringBuilder]::new(128)
            Assert-True ([QiQiStartupWindow]::GetClassName($window, $name, $name.Capacity) -gt 0) "Cannot inspect $($tool.File) main window."
            Assert-True ($name.ToString() -eq $tool.Class) "$($tool.File) showed an unexpected window: $name"
            if ($tool.SingleInstance) {
                [void][QiQiStartupWindow]::ShowWindow($window, 6)
                Assert-True ([QiQiStartupWindow]::IsIconic($window)) 'Settings window did not minimize.'
                $duplicate = Start-Process -FilePath $executable -WorkingDirectory $BinaryDirectory -PassThru
                try {
                    Assert-True ($duplicate.WaitForExit(15000)) 'Duplicate settings process did not exit.'
                    Assert-True ($duplicate.ExitCode -eq 0) 'Duplicate settings process failed.'
                    $process.Refresh()
                    Assert-True (-not $process.HasExited -and $process.MainWindowHandle -eq $window) 'Duplicate startup replaced the original settings window.'
                    Assert-True (-not [QiQiStartupWindow]::IsIconic($window)) 'Duplicate startup did not restore the settings window.'
                } finally {
                    Invoke-TestProcessCleanup $duplicate
                }
            }
            Assert-True ($process.CloseMainWindow()) "Could not close $($tool.File) main window."
            Assert-True ($process.WaitForExit(15000)) "$($tool.File) did not exit after closing."
            Assert-True ($process.ExitCode -eq 0) "$($tool.File) exited with code $($process.ExitCode)."
        } finally {
            Invoke-TestProcessCleanup $process
        }
    }
}
Write-Output 'Standalone tool startup, close/reopen, and settings duplicate activation passed.'
