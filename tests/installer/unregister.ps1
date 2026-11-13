# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:commonPath = Join-Path $root 'scripts\common.ps1'
$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ("qiqi unregister " + [guid]::NewGuid().ToString('N'))
$script:events = [System.Collections.Generic.List[string]]::new()
$script:isAdmin = $true
$script:unregisterFails = $false
$script:registrationRemains = $false
$script:cleanupExit = 0
$script:elevationExit = 0
$script:elevationDenied = $false
try {
    $scripts = Join-Path $fixture 'scripts'
    New-Item -ItemType Directory -Path $scripts -Force | Out-Null
    $unregister = Join-Path $scripts 'unregister_ime.ps1'
    $script:unregisterPath = $unregister
    Copy-Item -LiteralPath (Join-Path $root 'scripts\unregister_ime.ps1') -Destination $unregister
    $script:testDlls = @{}
    foreach ($arch in @('x64', 'x86')) {
        $directory = Join-Path $fixture $arch
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        $script:testDlls[$arch] = Join-Path $directory 'qiqiime.dll'
        [System.IO.File]::WriteAllText((Join-Path $directory 'QiQiIMESettings.exe'), '')
    }
    $nativeArch = if ([Environment]::Is64BitOperatingSystem) { 'x64' } else { 'x86' }
    $script:expectedCleaner = Join-Path $fixture "$nativeArch\QiQiIMESettings.exe"
    $mockCommon = @'
. $script:commonPath

function Resolve-QiQiDllPaths {
    return $script:testDlls
}
function Test-QiQiAdmin {
    return $script:isAdmin
}
function Invoke-QiQiRegsvr32([string]$DllPath, [switch]$Is32Bit, [switch]$Unregister) {
    if (-not $Unregister) { throw 'Test must never register an input method.' }
    $script:events.Add('unregister')
    if ($script:unregisterFails) { throw 'Simulated unregister failure' }
}
function Get-QiQiComDllPath {
    $script:events.Add('verify')
    if ($script:registrationRemains) { return 'still registered' }
}
function Start-Process(
    [string]$FilePath, [string]$Verb, [string[]]$ArgumentList, [switch]$Wait, [switch]$PassThru
) {
    if (-not $Wait -or -not $PassThru) { throw 'Processes must be waited for and their exit codes checked.' }
    if ($Verb -eq 'RunAs') {
        $script:events.Add('elevate')
        if ($FilePath -ne 'powershell.exe' -or
            ($ArgumentList -join '|') -ne ('-NoProfile|-ExecutionPolicy|Bypass|-File|"' + $script:unregisterPath + '"|-Config|Debug')) {
            throw 'Elevation must quote the script path, forward Config, and never forward data removal.'
        }
        if ($script:elevationDenied) { throw 'Simulated UAC refusal' }
        return [pscustomobject]@{ ExitCode = $script:elevationExit }
    }
    if ($Verb -or $FilePath -ne $script:expectedCleaner -or
        ($ArgumentList -join '|') -ne '--remove-user-data') {
        throw 'Cleanup must run the native cleaner without elevation, for the initiating user.'
    }
    $script:events.Add('cleanup')
    return [pscustomobject]@{ ExitCode = $script:cleanupExit }
}
'@
    [System.IO.File]::WriteAllText((Join-Path $scripts 'common.ps1'), $mockCommon)

    . $unregister
    Assert-True (-not $script:events.Contains('cleanup')) 'Default unregister must preserve user data.'
    Assert-True ($script:events.Contains('unregister')) 'Default unregister must still unregister the IME.'

    $script:events.Clear()
    . $unregister -RemoveUserData
    Assert-True ($script:events[$script:events.Count - 1] -eq 'cleanup') 'Cleanup must run after unregistration and verification.'
    Assert-True (@($script:events | Where-Object { $_ -eq 'cleanup' }).Count -eq 1) 'Cleanup must run exactly once.'

    foreach ($failure in @('unregisterFails', 'registrationRemains')) {
        $script:events.Clear()
        Set-Variable -Name $failure -Scope Script -Value $true
        $rejected = $false
        try {
            . $unregister -RemoveUserData
        } catch {
            if ($_.Exception.Message -notlike '*unregister*') { throw }
            $rejected = $true
        } finally {
            Set-Variable -Name $failure -Scope Script -Value $false
        }
        Assert-True $rejected 'Failed unregister/verification must report failure.'
        Assert-True (-not $script:events.Contains('cleanup')) 'Failed unregister/verification must preserve user data.'
    }

    $script:events.Clear()
    $script:cleanupExit = 7
    $rejected = $false
    try {
        . $unregister -RemoveUserData
    } catch {
        if ($_.Exception.Message -notlike '*user data cleanup failed, exit code: 7*') { throw }
        $rejected = $true
    }
    Assert-True $rejected 'Cleaner failure must retain its exit code and report partial cleanup.'
    $script:cleanupExit = 0

    Remove-Item -LiteralPath $script:expectedCleaner
    $script:events.Clear()
    . $unregister
    Assert-True (-not $script:events.Contains('cleanup')) 'Default unregister must not require the cleaner.'
    $script:events.Clear()
    $rejected = $false
    try {
        . $unregister -RemoveUserData
    } catch {
        if ($_.Exception.Message -notlike 'Missing native settings executable*') { throw }
        $rejected = $true
    }
    Assert-True ($rejected -and $script:events.Count -eq 0) 'Missing cleaner must fail before unregistering.'
    [System.IO.File]::WriteAllText($script:expectedCleaner, '')

    $script:isAdmin = $false
    $script:events.Clear()
    . $unregister -Config Debug -AutoElevate -RemoveUserData
    Assert-True (($script:events -join '|') -eq ('elevate|' + ('verify|' * @(Get-QiQiRuntimeArchitectures).Count) + 'cleanup')) 'Elevated unregistration must return to the initiating process before cleanup.'

    foreach ($failure in @('elevationExit', 'elevationDenied')) {
        $script:events.Clear()
        if ($failure -eq 'elevationExit') { $script:elevationExit = 5 } else { $script:elevationDenied = $true }
        $rejected = $false
        try {
            . $unregister -Config Debug -AutoElevate -RemoveUserData
        } catch {
            $expected = if ($failure -eq 'elevationExit') { 'Elevated operation failed, exit code: 5' } else { 'Simulated UAC refusal' }
            if ($_.Exception.Message -ne $expected) { throw }
            $rejected = $true
        } finally {
            $script:elevationExit = 0
            $script:elevationDenied = $false
        }
        Assert-True ($rejected -and ($script:events -join '|') -eq 'elevate') 'Failed or refused elevation must prevent data cleanup.'
    }
    Write-Host 'ZIP unregister data retention, opt-in, initiating-user elevation and failure checks passed.'
} finally {
    if (Test-Path -LiteralPath $fixture) {
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
