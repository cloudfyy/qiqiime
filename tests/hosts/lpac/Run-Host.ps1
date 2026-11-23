# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding(DefaultParameterSetName = 'Single')]
param(
    [Parameter(ParameterSetName = 'Single')]
    [ValidateSet('zero', 'registryRead', 'lpacCom')]
    [string] $Profile = 'lpacCom',

    [Parameter(ParameterSetName = 'Single')]
    [switch] $Smoke,

    [Parameter(ParameterSetName = 'All', Mandatory = $true)]
    [switch] $SmokeAll,

    [string] $LauncherPath = (Join-Path `
            $PSScriptRoot `
            '..\..\..\build\hosts\lpac\Release\QiQiImeLpacLauncher.exe')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$launcher = (Resolve-Path -LiteralPath $LauncherPath).Path
$profiles = @{
    zero         = 'QiQiImeLpac.Zero'
    registryRead = 'QiQiImeLpac.RegistryRead'
    lpacCom      = 'QiQiImeLpac.LpacCom'
}

function Assert-ProfileRemoved {
    param([string] $Moniker)

    $directory = Join-Path `
        $env:LOCALAPPDATA `
        "Packages\$Moniker"
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        if (-not (Test-Path -LiteralPath $directory)) {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "LPAC profile was not removed: $Moniker"
}

function Invoke-Smoke {
    param([string] $Name)

    & $launcher $Name --smoke
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "LPAC smoke failed for '$Name' with exit code $exitCode."
    }
    Assert-ProfileRemoved -Moniker $profiles[$Name]
}

if ($SmokeAll) {
    foreach ($name in @('zero', 'registryRead', 'lpacCom')) {
        Invoke-Smoke -Name $name
    }

    & $launcher unsupported-profile --smoke
    $unsupportedExit = $LASTEXITCODE
    if ($unsupportedExit -ne 1) {
        throw "Unsupported profile returned $unsupportedExit; expected 1."
    }

    & $launcher zero --smoke extra-argument
    $argumentExit = $LASTEXITCODE
    if ($argumentExit -ne 2) {
        throw "Excess arguments returned $argumentExit; expected 2."
    }
    Write-Host 'LPAC smoke profiles and launcher rejection passed.'
    exit 0
}

if ($Smoke) {
    Invoke-Smoke -Name $Profile
    exit 0
}

& $launcher $Profile
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "LPAC host exited with code $exitCode."
}
Assert-ProfileRemoved -Moniker $profiles[$Profile]
