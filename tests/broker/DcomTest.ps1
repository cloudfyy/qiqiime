# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [string] $BuildDirectory = (Join-Path `
            $PSScriptRoot `
            '..\..\..\build\x64'),
    [switch] $AutomaticActivation,
    [Parameter(Mandatory = $true)]
    [string] $LaunchLowPath,
    [switch] $LowBrokerHandoff
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path `
        $PSScriptRoot `
        'Test-Helpers.ps1')

$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$broker = Join-Path $build "$Configuration\QiQiIMEConfigBroker.exe"
$proxy = Join-Path $build "$Configuration\QiQiIMEConfigProxy.dll"
$probe = Join-Path $build "$Configuration\qiqi_broker_probe.exe"
$launchLow =
    (Resolve-Path `
        -LiteralPath $LaunchLowPath).Path
$subkey = "Software\QiQiIME_BrokerDcom_$PID"
$brokerProcess = $null
$lowBrokerProcess = $null
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
$previousDiagnosticLog = [Environment]::GetEnvironmentVariable(
    'QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG',
    'Process')
$previousPerUser = [Environment]::GetEnvironmentVariable(
    'QIQI_CONFIG_BROKER_PER_USER',
    'Process')
$previousTestStoreSubkey = [Environment]::GetEnvironmentVariable(
    'QIQI_CONFIG_BROKER_TEST_STORE_SUBKEY',
    'Process')
$appid = '{8E7B9D31-5A42-4C81-9F11-20261029000E}'
$clsid = '{8E7B9D31-5A42-4C81-9F11-20261029000D}'
$installedMachineRegistration =
    Test-Path `
        -LiteralPath (
            "Registry::HKEY_LOCAL_MACHINE\" +
            "SOFTWARE\Classes\CLSID\$clsid\LocalServer32")
$isAdministrator =
    Test-QiQiAdministrator
if ($AutomaticActivation -and
        -not $isAdministrator) {
    throw (
        '-AutomaticActivation requires an elevated ' +
        'PowerShell because it validates the HKLM ' +
        'registration used by the installer.')
}
if ($installedMachineRegistration -and
        -not $AutomaticActivation) {
    Write-Host (
        'SKIP: installed machine Broker registration ' +
        'owns the shared CLSID; clean CI runs the isolated ' +
        'DCOM and Low handoff fixture.')
    exit 77
}
$useMachineRegistration =
    $isAdministrator
if ($useMachineRegistration) {
    Assert-QiQiBrokerMachineRegistrationAbsent `
        -FailurePrefix (
            'Broker test refuses to overwrite an ' +
            'existing machine registration:')
}
$runLowBrokerHandoff =
    $LowBrokerHandoff

foreach ($file in @(
        $broker,
        $proxy,
        $probe,
        $launchLow)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required file was not found: $file"
    }
}

function Assert-PackageFileAccess {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path,
        [Parameter(Mandatory = $true)]
        [string[]] $ExpectedSids,
        [int] $RequiredMask =
            [int][System.Security.AccessControl.FileSystemRights]::ReadAndExecute,
        [switch] $Directory,
        [string] $Label = 'ReadAndExecute'
    )

    $acl = Get-Acl `
        -LiteralPath $Path
        $rawDescriptor =
            [System.Security.AccessControl.RawSecurityDescriptor]::new(
                $acl.GetSecurityDescriptorBinaryForm(),
                0)
    foreach ($expectedSid in
        $ExpectedSids) {
        $matches = @(
                $rawDescriptor.DiscretionaryAcl |
                Where-Object {
                        $_.AceType -eq
                            [System.Security.AccessControl.AceType]::AccessAllowed
                } |
                Where-Object {
                        $_.SecurityIdentifier.Value -eq
                            $expectedSid
                } |
                Where-Object {
                        ($_.AccessMask -band
                            $RequiredMask) -eq
                            $RequiredMask
                })
        if ($matches.Count -eq 0) {
            throw (
                "Package principal $expectedSid lacks " +
                "$Label on $Path.")
        }
        if ($Directory) {
            $inheritable = @(
                $matches |
                    Where-Object {
                        $_.AceFlags -band
                            [System.Security.AccessControl.AceFlags]::ContainerInherit -and
                        $_.AceFlags -band
                            [System.Security.AccessControl.AceFlags]::ObjectInherit
                    })
            if ($inheritable.Count -eq 0) {
                throw (
                    "Package principal $expectedSid " +
                    "is not inheritable on $Path.")
            }
        }
    }
}

function Grant-LowReadAccess {
    param([string] $Subkey)

    $lowSid =
        [Security.Principal.SecurityIdentifier]::new(
            'S-1-16-4096')
    $root =
        "Registry::HKEY_CURRENT_USER\$Subkey"
    $keys = @(
        Get-Item `
            -LiteralPath $root
        Get-ChildItem `
            -LiteralPath $root `
            -Recurse)
    foreach ($key in $keys) {
        $acl = Get-Acl `
            -LiteralPath $key.PSPath
        $rule =
            [Security.AccessControl.RegistryAccessRule]::new(
                $lowSid,
                [Security.AccessControl.RegistryRights]::ReadKey,
                [Security.AccessControl.InheritanceFlags](
                    [Security.AccessControl.InheritanceFlags]::ContainerInherit -bor
                    [Security.AccessControl.InheritanceFlags]::ObjectInherit),
                [Security.AccessControl.PropagationFlags]::None,
                [Security.AccessControl.AccessControlType]::Allow)
        $acl.SetAccessRule($rule)
        Set-Acl `
            -LiteralPath $key.PSPath `
            -AclObject $acl
    }
}

function Start-ManualBroker {
    $process = Start-Process `
        -FilePath $broker `
        -ArgumentList @(
            '-Embedding'
            "--store-subkey=$subkey"
        ) `
        -WindowStyle Hidden `
        -PassThru
    Wait-QiQiBrokerReady `
        -Process $process `
        -DiagnosticLog $diagnosticLog `
        -DiagnosticLogStart $diagnosticLogStart
    return $process
}

function Stop-ManualBroker {
    param([Diagnostics.Process] $Process)

    if (-not $Process -or
        $Process.HasExited) {
        return
    }
    $shutdown = Start-Process `
        -FilePath $broker `
        -ArgumentList @(
            '--shutdown',
            "--store-subkey=$subkey") `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($shutdown.ExitCode -ne 0) {
        throw (
            'Broker shutdown exited with code ' +
            "$($shutdown.ExitCode).")
    }
    if (-not $Process.WaitForExit(5000)) {
        throw 'Broker did not exit after shutdown.'
    }
}

Stop-QiQiExistingBroker

try {
    $env:QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG = '1'
    if ($useMachineRegistration) {
        Remove-Item `
            Env:QIQI_CONFIG_BROKER_PER_USER `
            -ErrorAction SilentlyContinue
    } else {
        $env:QIQI_CONFIG_BROKER_PER_USER = '1'
    }
    $env:QIQI_CONFIG_BROKER_TEST_STORE_SUBKEY =
        $subkey
    $registration = Start-Process `
        -FilePath $broker `
        -ArgumentList @(
            '--register'
            "--store-subkey=$subkey"
        ) `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($registration.ExitCode -ne 0) {
        throw "Broker registration failed with exit code $($registration.ExitCode)."
    }
    $appidRoot = if ($useMachineRegistration) {
        'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes'
    } else {
        'Registry::HKEY_CURRENT_USER\Software\Classes'
    }
    $appidKey = "$appidRoot\AppID\$appid"
    $expectedMasks = @{
        AccessPermission = 0x3
        LaunchPermission = 0xB
    }
    foreach ($permission in
        $expectedMasks.Keys) {
        $bytes =
            [byte[]](Get-ItemPropertyValue `
                -LiteralPath $appidKey `
                -Name $permission)
        $descriptor =
            [System.Security.AccessControl.RawSecurityDescriptor]::new(
                $bytes,
                0)
        if ($descriptor.Owner.Value -ne
                'S-1-5-18' -or
            $descriptor.Group.Value -ne
                'S-1-5-18') {
            throw (
                "$permission has invalid " +
                'owner/group security identity.')
        }
        $masks = @(
            $descriptor.DiscretionaryAcl |
                ForEach-Object {
                    $_.AccessMask
                } |
                Sort-Object -Unique)
        if ($masks.Count -ne 1 -or
            $masks[0] -ne
                $expectedMasks[$permission]) {
            throw (
                "$permission has invalid COM " +
                "rights: $($masks -join ', ')")
        }
        $presentSids = @(
            $descriptor.DiscretionaryAcl |
                ForEach-Object {
                    $_.SecurityIdentifier.Value
                })
        $packageSids = @(
            $presentSids |
                Where-Object {
                    $_ -like 'S-1-15-*'
                })
        Assert-PackageFileAccess `
            -Path (Split-Path `
                -Parent $broker) `
            -ExpectedSids $packageSids `
            -Directory
        Assert-PackageFileAccess `
            -Path $broker `
            -ExpectedSids $packageSids
        Assert-PackageFileAccess `
            -Path $proxy `
            -ExpectedSids $packageSids
        $windowsDirectory =
            [Environment]::GetFolderPath(
                [Environment+SpecialFolder]::Windows)
        $installedSearchPackage =
            Get-ChildItem `
                -LiteralPath (
                    Join-Path `
                        $windowsDirectory `
                        'SystemApps') `
                -Directory `
                -ErrorAction Stop |
            Where-Object {
                (Test-Path `
                    -LiteralPath (
                        Join-Path `
                            $_.FullName `
                            'SearchHost.exe')) -or
                (Test-Path `
                    -LiteralPath (
                        Join-Path `
                            $_.FullName `
                            'SearchApp.exe'))
            } |
            Select-Object -First 1
        $searchPackageSids = @(
            $presentSids |
                Where-Object {
                    $_ -like 'S-1-15-2-*' -and
                    $_ -ne 'S-1-15-2-1' -and
                    $_ -ne 'S-1-15-2-2'
                })
        if ($installedSearchPackage -and
                $searchPackageSids.Count -eq 0) {
            throw (
                "$permission does not contain " +
                'the discovered Windows Search package SID.')
        }
    }
    $appidValueNames = (
        Get-Item `
            -LiteralPath $appidKey).GetValueNames()
    if ($appidValueNames -notcontains
            'LaunchPermission') {
        throw (
            'LaunchPermission is missing; AppContainer ' +
            'callers require explicit local activation.')
    }
    if ($AutomaticActivation) {
        & $probe
        if ($LASTEXITCODE -ne 0) {
            throw (
                'Automatic DCOM probe failed ' +
                "with exit code $LASTEXITCODE.")
        }
        $activationDeadline =
            (Get-Date).AddSeconds(5)
        do {
            $brokerCim = Get-CimInstance `
                Win32_Process `
                -Filter "Name = 'QiQiIMEConfigBroker.exe'" `
                -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.CommandLine -like
                        "*--store-subkey=*$subkey*"
                } |
                Select-Object -First 1
            if (-not $brokerCim) {
                Start-Sleep -Milliseconds 100
            }
        } while (
            -not $brokerCim -and
            (Get-Date) -lt $activationDeadline)
        if (-not $brokerCim) {
            throw (
                'SCM did not leave the ' +
                'automatically activated Broker running.')
        }
        $brokerProcess = Get-Process `
            -Id $brokerCim.ProcessId
    } else {
        $brokerProcess = Start-ManualBroker
        if ($runLowBrokerHandoff) {
            & $probe
            if ($LASTEXITCODE -ne 0) {
                throw (
                    'Medium Broker seed probe failed with ' +
                    "exit code $LASTEXITCODE.")
            }
            Stop-ManualBroker `
                -Process $brokerProcess
            Grant-LowReadAccess `
                -Subkey $subkey
            $lowOutput =
                & $launchLow `
                    $broker `
                    '-Embedding' `
                    "--store-subkey=$subkey"
            if ($LASTEXITCODE -ne 0) {
                throw (
                    'Low Broker launcher failed with ' +
                    "exit code $LASTEXITCODE.")
            }
            $lowPid = [int](
                $lowOutput |
                    Select-Object -Last 1)
            $lowBrokerProcess =
                Get-Process `
                    -Id $lowPid
            Stop-QiQiExistingBroker `
                -ExceptProcessId $lowPid
            & $probe '--mutation-handoff'
            if ($LASTEXITCODE -ne 0) {
                throw (
                    'Low Broker mutation handoff failed with ' +
                    "exit code $LASTEXITCODE.")
            }
            if (-not (
                    $lowBrokerProcess.WaitForExit(
                        5000))) {
                throw 'Low Broker did not exit during handoff.'
            }
            $brokerCim =
                Get-CimInstance `
                    Win32_Process `
                    -Filter "Name = 'QiQiIMEConfigBroker.exe'" `
                    -ErrorAction SilentlyContinue |
                    Where-Object {
                        $_.CommandLine -like
                            "*--store-subkey=*$subkey*"
                    } |
                    Select-Object -First 1
            if (-not $brokerCim) {
                throw 'No Medium Broker remained after handoff.'
            }
            $brokerProcess =
                Get-Process `
                    -Id $brokerCim.ProcessId
            Wait-QiQiBrokerReady `
                -Process $brokerProcess `
                -DiagnosticLog $diagnosticLog `
                -DiagnosticLogStart $diagnosticLogStart
            $handoffTail =
                Read-QiQiBrokerLogTail `
                    -Path $diagnosticLog `
                    -Start $diagnosticLogStart
            $mediumPattern =
                'Broker started .*' +
                'integrity_rid=8192, ' +
                "pid=$($brokerProcess.Id)\)"
            if ($handoffTail -notmatch
                    $mediumPattern) {
                throw (
                    'Replacement Broker is not ' +
                    'running at Medium integrity.')
            }
        }
    }
    $duplicate = Start-Process `
        -FilePath $broker `
        -ArgumentList @(
            '-Embedding'
            "--store-subkey=$subkey"
        ) `
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
        throw (
            'DCOM access probe failed with ' +
            "exit code $LASTEXITCODE.")
    }
    1..2 |
        ForEach-Object {
            & $probe '--read-only'
            if ($LASTEXITCODE -ne 0) {
                throw (
                    'Subscriber probe failed ' +
                    "with exit code $LASTEXITCODE.")
            }
        }
} finally {
    Stop-QiQiExistingBroker
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
    if ($useMachineRegistration) {
        Assert-QiQiBrokerMachineRegistrationAbsent `
            -FailurePrefix (
                'Broker unregister left a machine registration:')
    }
}

if (-not (
        Test-Path `
            -LiteralPath $diagnosticLog `
            -PathType Leaf)) {
    throw 'Broker diagnostic log was not created.'
}
$diagnosticBytes =
    [IO.File]::ReadAllBytes(
        $diagnosticLog)
if ($diagnosticBytes.Length -le
        $diagnosticLogStart) {
    throw 'Broker diagnostic log did not receive events.'
}
$diagnosticTail =
    [Text.Encoding]::UTF8.GetString(
        $diagnosticBytes,
        [int]$diagnosticLogStart,
        $diagnosticBytes.Length -
            [int]$diagnosticLogStart)
$diagnosticLines = @(
    $diagnosticTail -split "`r?`n" |
        Where-Object {
            -not [string]::IsNullOrEmpty($_)
        })
$expectedStages = @(
    'Broker logging initialized',
    'Broker process starting',
    'operation=argument-parse',
    'Broker registration command started',
    'Broker server startup requested',
    'Broker singleton mutex acquired',
    'Broker COM security initialized',
    'Broker class factory created',
    'Broker started',
    'Broker stop event received',
    'operation=class-object-revoke, hr=0x00000000',
    'Broker unregistration command started'
)
foreach ($stage in $expectedStages) {
    if (-not (
            $diagnosticLines -match
                [regex]::Escape($stage))) {
        throw "Broker diagnostic log is missing stage: $stage"
    }
}
$tracePattern =
    'ConfigBroker operation=save ' +
    'started revision=.*' +
    '\[trace=(?<trace>\{[0-9A-F-]+\}) ' +
    'activity=(?<activity>\{[0-9A-F-]+\}) ' +
    'related=(?<related>\{[0-9A-F-]+\})\]$'
$saveTraceStarts =
    @($diagnosticLines |
        Select-String -Pattern $tracePattern)
if ($saveTraceStarts.Count -lt 2) {
    throw (
        'Broker diagnostic log did not ' +
        'contain sync and async save trace starts.')
}
foreach ($start in $saveTraceStarts) {
    $trace = $start.Matches[0].Groups['trace'].Value
    $activity =
        $start.Matches[0].Groups['activity'].Value
    $stopPattern =
        'ConfigBroker operation=save ' +
        'completed .*' +
        "\[trace=$([regex]::Escape($trace)) " +
        "activity=$([regex]::Escape($activity)) "
    if (-not (
            $diagnosticLines -match
                $stopPattern)) {
        throw (
            'Broker save trace did not have ' +
            "a matching stop event: $trace")
    }
}
$sensitiveFields = @(
    'candidate_font_family=',
    'primary_dict_path=',
    'user_dict_path=')
foreach ($field in $sensitiveFields) {
    if ($diagnosticLines -match
            [regex]::Escape($field)) {
        throw (
            'Broker trace leaked a ' +
            "configuration field: $field")
    }
}
$timestampPattern =
    '^\d{4}-\d{2}-\d{2} ' +
    '\d{2}:\d{2}:\d{2}\.\d{3} '
$unframedLines = @(
    $diagnosticLines |
        Where-Object {
            $_ -notmatch
                $timestampPattern
        })
if ($unframedLines.Count -ne 0) {
    throw (
        'Broker diagnostic log contains ' +
        "unframed lines: $($unframedLines -join ' | ')")
}
