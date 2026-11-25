# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding(
    SupportsShouldProcess = $true,
    ConfirmImpact = 'High')]
param(
    [ValidateSet(
        'List',
        'Diagnose',
        'Grant',
        'Revoke')]
    [string] $Action = 'List',

    [string] $PackageFamilyName,
    [string] $PackageName,
    [switch] $IncludeAncestors
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$brokerClsid =
    '{8E7B9D31-5A42-4C81-9F11-20261029000D}'
$brokerAppId =
    '{8E7B9D31-5A42-4C81-9F11-20261029000E}'
$brokerInterface =
    '{8E7B9D31-5A42-4C81-9F11-202610290020}'
$comAccessMask = 0x3
$comLaunchMask = 0xB

if (-not (
        'QiQiImeAppContainerSid' -as
            [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class QiQiImeAppContainerSid
{
    [DllImport(
        "userenv.dll",
        CharSet = CharSet.Unicode,
        SetLastError = true)]
    private static extern int
    DeriveAppContainerSidFromAppContainerName(
        string name,
        out IntPtr sid);

    [DllImport(
        "advapi32.dll",
        CharSet = CharSet.Unicode,
        SetLastError = true)]
    private static extern bool
    ConvertSidToStringSidW(
        IntPtr sid,
        out IntPtr text);

    [DllImport("advapi32.dll")]
    private static extern void FreeSid(IntPtr sid);

    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr value);

    public static string Derive(string packageFamilyName)
    {
        IntPtr sid;
        var result =
            DeriveAppContainerSidFromAppContainerName(
                packageFamilyName,
                out sid);
        if (result < 0)
        {
            throw new Win32Exception(result);
        }

        try
        {
            IntPtr text;
            if (!ConvertSidToStringSidW(sid, out text))
            {
                throw new Win32Exception(
                    Marshal.GetLastWin32Error());
            }

            try
            {
                return Marshal.PtrToStringUni(text);
            }
            finally
            {
                LocalFree(text);
            }
        }
        finally
        {
            FreeSid(sid);
        }
    }
}
'@
}

function Test-QiQiAdministrator {
    $principal = New-Object `
        Security.Principal.WindowsPrincipal(
            [Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-QiQiInstalledPackages {
    Get-AppxPackage |
        Where-Object {
            -not [string]::IsNullOrWhiteSpace(
                $_.PackageFamilyName)
        } |
        Sort-Object Name, PackageFamilyName |
        Select-Object `
            Name,
            Version,
            PackageFamilyName,
            PackageFullName,
            InstallLocation
}

function Resolve-QiQiPackageFamily {
    if ($PackageName -and
            $PackageFamilyName) {
        throw (
            'Specify either -PackageName or ' +
            '-PackageFamilyName, not both.')
    }
    if ($PackageFamilyName) {
        return $PackageFamilyName
    }
    if (-not $PackageName) {
        throw (
            "Provide -PackageFamilyName or " +
            "-PackageName. Use -Action List to " +
            "inspect installed packages.")
    }
    $matches = @(
        Get-QiQiInstalledPackages |
            Where-Object {
                $_.Name -eq $PackageName
            })
    if ($matches.Count -eq 0) {
        throw "Installed package was not found: $PackageName"
    }
    if ($matches.Count -ne 1) {
        throw (
            "Package name '$PackageName' is ambiguous; " +
            "pass -PackageFamilyName explicitly.")
    }
    return $matches[0].PackageFamilyName
}

function Open-QiQiRegistryView {
    param(
        [Microsoft.Win32.RegistryView] $View
    )
    return [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine,
        $View)
}

function Get-QiQiRegistryViews {
    if ([Environment]::Is64BitOperatingSystem) {
        return @(
            [Microsoft.Win32.RegistryView]::Registry64
            [Microsoft.Win32.RegistryView]::Registry32
        )
    }
    return @(
        [Microsoft.Win32.RegistryView]::Default
    )
}

function Get-QiQiRegisteredCommand {
    param(
        [Microsoft.Win32.RegistryView] $View,
        [string] $RelativePath
    )

    $base = Open-QiQiRegistryView -View $View
    try {
        $key = $base.OpenSubKey($RelativePath)
        if (-not $key) {
            return $null
        }
        try {
            return [string]$key.GetValue($null)
        } finally {
            $key.Dispose()
        }
    } finally {
        $base.Dispose()
    }
}

function ConvertFrom-QiQiCommandPath {
    param([string] $Command)

    if ([string]::IsNullOrWhiteSpace(
            $Command)) {
        return $null
    }
    $trimmed = $Command.Trim()
    if ($trimmed.StartsWith('"')) {
        $closing = $trimmed.IndexOf(
            '"',
            1)
        if ($closing -lt 2) {
            throw "COM registration command is malformed: $Command"
        }
        return $trimmed.Substring(
            1,
            $closing - 1)
    }
    return ($trimmed -split '\s+')[0]
}

function Get-QiQiBrokerPaths {
    $paths = New-Object `
        System.Collections.Generic.HashSet[string](
            [StringComparer]::OrdinalIgnoreCase)
    $views = @(
        [Microsoft.Win32.RegistryView]::Registry64
        [Microsoft.Win32.RegistryView]::Registry32
    )
    foreach ($view in $views) {
        foreach ($keyPath in @(
                "SOFTWARE\Classes\CLSID\$brokerClsid\LocalServer32"
                "SOFTWARE\Classes\CLSID\$brokerInterface\InprocServer32")) {
            $command = Get-QiQiRegisteredCommand `
                -View $view `
                -RelativePath $keyPath
            $path = ConvertFrom-QiQiCommandPath `
                -Command $command
            if ($path) {
                [void]$paths.Add(
                    [IO.Path]::GetFullPath(
                        $path))
            }
        }
    }
    if ($paths.Count -eq 0) {
        throw (
            'QiQiIME Config Broker is not registered. ' +
            'Install/register QiQiIME before repairing package access.')
    }
    foreach ($path in @($paths)) {
        if (-not (Test-Path `
                -LiteralPath $path `
                -PathType Leaf)) {
            throw "Registered Broker binary was not found: $path"
        }
    }
    return @($paths)
}

function Get-QiQiPathSids {
    param([string] $Path)

    $security = Get-Acl `
        -LiteralPath $Path
    $descriptor =
        [System.Security.AccessControl.RawSecurityDescriptor]::new(
            $security.GetSecurityDescriptorBinaryForm(),
            0)
    return @(
        $descriptor.DiscretionaryAcl |
            Where-Object {
                $_.AceType -eq
                    [System.Security.AccessControl.AceType]::AccessAllowed
            } |
            Select-Object `
                @{
                    Name = 'Sid'
                    Expression = {
                        $_.SecurityIdentifier.Value
                    }
                },
                @{
                    Name = 'Mask'
                    Expression = {
                        $_.AccessMask
                    }
                },
                @{
                    Name = 'Flags'
                    Expression = {
                        $_.AceFlags
                    }
                })
}

function Test-QiQiReadExecuteAce {
    param(
        [string] $Path,
        [string] $Sid,
        [int] $RequiredMask
    )

    return @(
        Get-QiQiPathSids -Path $Path |
            Where-Object {
                $_.Sid -eq $Sid -and
                ($_.Mask -band $RequiredMask) -eq
                    $RequiredMask
            }).Count -gt 0
}

function Get-QiQiDcomState {
    param([string] $Sid)

    $path =
        "SOFTWARE\Classes\AppID\$brokerAppId"
    $result = [ordered]@{}
    foreach ($view in
            Get-QiQiRegistryViews) {
        $base = Open-QiQiRegistryView `
            -View $view
        try {
            $key = $base.OpenSubKey($path)
            if ($key) {
                try {
                    $viewState =
                        [ordered]@{}
                    foreach ($name in @(
                            'AccessPermission'
                            'LaunchPermission')) {
                        $bytes = [byte[]]$key.GetValue($name)
                        if (-not $bytes) {
                            $viewState[$name] =
                                $null
                            continue
                        }
                        $descriptor =
                            [System.Security.AccessControl.RawSecurityDescriptor]::new(
                                $bytes,
                                0)
                        $viewState[$name] = @(
                            $descriptor.DiscretionaryAcl |
                                Where-Object {
                                    $_.AceType -eq
                                        [System.Security.AccessControl.AceType]::AccessAllowed -and
                                    $_.SecurityIdentifier.Value -eq
                                        $Sid
                                } |
                                ForEach-Object {
                                    "0x$($_.AccessMask.ToString('X'))"
                                })
                    }
                    $result[$view.ToString()] =
                        [pscustomobject]$viewState
                } finally {
                    $key.Dispose()
                }
            }
        } finally {
            $base.Dispose()
        }
    }
    if ($result.Count -eq 0) {
        throw 'QiQiIME Config Broker AppID is not registered.'
    }
    return [pscustomobject]$result
}

function Update-QiQiDcomDescriptor {
    param(
        [byte[]] $Bytes,
        [System.Security.Principal.SecurityIdentifier] $Sid,
        [int] $Mask,
        [ValidateSet('Grant', 'Revoke')]
        [string] $Mode
    )

    $descriptor =
        [System.Security.AccessControl.RawSecurityDescriptor]::new(
            $Bytes,
            0)
    for ($index =
            $descriptor.DiscretionaryAcl.Count - 1;
         $index -ge 0;
         --$index) {
        if ($descriptor.DiscretionaryAcl[
                $index].SecurityIdentifier.Value -eq
                $Sid.Value) {
            $descriptor.DiscretionaryAcl.RemoveAce(
                $index)
        }
    }
    if ($Mode -eq 'Grant') {
        $ace =
            [System.Security.AccessControl.CommonAce]::new(
                [System.Security.AccessControl.AceFlags]::None,
                [System.Security.AccessControl.AceQualifier]::AccessAllowed,
                $Mask,
                $Sid,
                $false,
                $null)
        $descriptor.DiscretionaryAcl.InsertAce(
            $descriptor.DiscretionaryAcl.Count,
            $ace)
    }
    $updated =
        New-Object byte[] $descriptor.BinaryLength
    $descriptor.GetBinaryForm(
        $updated,
        0)
    return $updated
}

function Set-QiQiDcomPackageAccess {
    param(
        [System.Security.Principal.SecurityIdentifier] $Sid,
        [ValidateSet('Grant', 'Revoke')]
        [string] $Mode
    )

    $updatedViewCount = 0
    foreach ($view in Get-QiQiRegistryViews) {
        $base = Open-QiQiRegistryView `
            -View $view
        try {
            $key = $base.OpenSubKey(
                "SOFTWARE\Classes\AppID\$brokerAppId",
                [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree)
            if ($key) {
                try {
                    foreach ($entry in @(
                            @{
                                Name = 'AccessPermission'
                                Mask = $comAccessMask
                            }
                            @{
                                Name = 'LaunchPermission'
                                Mask = $comLaunchMask
                            })) {
                        $bytes = [byte[]]$key.GetValue(
                            $entry.Name)
                        if (-not $bytes) {
                            throw (
                                "$($entry.Name) is missing. " +
                                'Re-register the Broker first.')
                        }
                        $updated = [byte[]](Update-QiQiDcomDescriptor `
                            -Bytes $bytes `
                            -Sid $Sid `
                            -Mask $entry.Mask `
                            -Mode $Mode)
                        $key.SetValue(
                            $entry.Name,
                            $updated,
                            [Microsoft.Win32.RegistryValueKind]::Binary)
                    }
                    ++$updatedViewCount
                } finally {
                    $key.Dispose()
                }
            }
        } finally {
            $base.Dispose()
        }
    }
    if ($updatedViewCount -eq 0) {
        throw 'QiQiIME Config Broker AppID is not registered.'
    }
}

function Set-QiQiFilePackageAccess {
    param(
        [string] $Path,
        [int] $Mask,
        [bool] $Inheritable,
        [System.Security.Principal.SecurityIdentifier] $Sid,
        [ValidateSet('Grant', 'Revoke')]
        [string] $Mode
    )

    $security = Get-Acl `
        -LiteralPath $Path
    if ($Mode -eq 'Grant') {
        $rule = if ($Inheritable) {
            [System.Security.AccessControl.FileSystemAccessRule]::new(
                $Sid,
                [System.Security.AccessControl.FileSystemRights]$Mask,
                [System.Security.AccessControl.InheritanceFlags](
                    [System.Security.AccessControl.InheritanceFlags]::ContainerInherit -bor
                    [System.Security.AccessControl.InheritanceFlags]::ObjectInherit),
                [System.Security.AccessControl.PropagationFlags]::None,
                [System.Security.AccessControl.AccessControlType]::Allow)
        } else {
            [System.Security.AccessControl.FileSystemAccessRule]::new(
                $Sid,
                [System.Security.AccessControl.FileSystemRights]$Mask,
                [System.Security.AccessControl.AccessControlType]::Allow)
        }
        $security.SetAccessRule($rule)
    } else {
        foreach ($rule in @(
                $security.Access |
                    Where-Object {
                        try {
                            $_.IdentityReference.Translate(
                                [System.Security.Principal.SecurityIdentifier]).Value -eq
                                $Sid.Value
                        } catch [
                            System.Security.Principal.IdentityNotMappedException] {
                            $_.IdentityReference.Value -eq
                                $Sid.Value
                        }
                    })) {
            [void]$security.RemoveAccessRuleSpecific(
                $rule)
        }
    }
    Set-Acl `
        -LiteralPath $Path `
        -AclObject $security
}

function Get-QiQiAccessTargets {
    param(
        [string[]] $Files,
        [switch] $IncludeAncestors
    )

    $readExecuteMask =
        [int][System.Security.AccessControl.FileSystemRights]::ReadAndExecute
    $traverseMask = 0x20
    $finalDirectories =
        New-Object `
            System.Collections.Generic.HashSet[string](
                [StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $Files) {
        $directory = Split-Path `
            -Parent $file
        [void]$finalDirectories.Add(
            $directory)
    }

    $ancestors =
        New-Object `
            System.Collections.Generic.HashSet[string](
                [StringComparer]::OrdinalIgnoreCase)
    if ($IncludeAncestors) {
        foreach ($directory in
                @($finalDirectories)) {
            $parent = Split-Path `
                -Parent $directory
            while ($parent) {
                if (-not $finalDirectories.Contains(
                        $parent)) {
                    [void]$ancestors.Add(
                        $parent)
                }
                $next = Split-Path `
                    -Parent $parent
                if ($next -eq $parent) {
                    break
                }
                $parent = $next
            }
        }
    }

    $targets = @()
    foreach ($ancestor in
            @($ancestors | Sort-Object)) {
        $targets += [pscustomobject]@{
            Path = $ancestor
            Mask = $traverseMask
            Inheritable = $false
            Label = 'Traverse'
        }
    }
    foreach ($directory in
            @($finalDirectories | Sort-Object)) {
        $targets += [pscustomobject]@{
            Path = $directory
            Mask = $readExecuteMask
            Inheritable = $true
            Label = 'ReadAndExecute'
        }
    }
    foreach ($file in (
            @($Files | Sort-Object))) {
        $targets += [pscustomobject]@{
            Path = $file
            Mask = $readExecuteMask
            Inheritable = $false
            Label = 'ReadAndExecute'
        }
    }
    return $targets
}

if ($Action -eq 'List') {
    Get-QiQiInstalledPackages |
        Format-Table `
            Name,
            Version,
            PackageFamilyName `
            -AutoSize
    exit 0
}

$family = Resolve-QiQiPackageFamily
$sidText =
    [QiQiImeAppContainerSid]::Derive(
        $family)
$sid =
    [System.Security.Principal.SecurityIdentifier]::new(
        $sidText)
$files = Get-QiQiBrokerPaths
$targets = @(
    Get-QiQiAccessTargets `
        -Files $files `
        -IncludeAncestors:$IncludeAncestors)

if ($Action -eq 'Diagnose') {
    Write-Host "Package family: $family"
    Write-Host "Package SID:    $sidText"
    $dcom = Get-QiQiDcomState `
        -Sid $sidText
    foreach ($view in
            $dcom.PSObject.Properties) {
        Write-Host (
            "$($view.Name) AccessPermission:  " +
            "$($view.Value.AccessPermission -join ', ')")
        Write-Host (
            "$($view.Name) LaunchPermission:  " +
            "$($view.Value.LaunchPermission -join ', ')")
    }
    foreach ($target in $targets) {
        $state = if (
            Test-QiQiReadExecuteAce `
                -Path $target.Path `
                -Sid $sidText `
                -RequiredMask $target.Mask) {
            'present'
        } else {
            'missing'
        }
        Write-Host (
            "$($target.Label) [$state]: " +
            $target.Path)
    }
    exit 0
}

if (-not (Test-QiQiAdministrator) -and
        -not $WhatIfPreference) {
    throw (
        "$Action requires an elevated PowerShell. " +
        'Use -WhatIf to preview without elevation.')
}

$mode = if ($Action -eq 'Grant') {
    'Grant'
} else {
    'Revoke'
}
$operationCount =
    1 +
    $targets.Count
$appliedCount = 0
if ($PSCmdlet.ShouldProcess(
        $brokerAppId,
        "$mode package SID $sidText for DCOM Access/Launch")) {
    Set-QiQiDcomPackageAccess `
        -Sid $sid `
        -Mode $mode
    ++$appliedCount
}
foreach ($target in $targets) {
    if ($PSCmdlet.ShouldProcess(
            $target.Path,
            "$mode package SID $sidText $($target.Label)")) {
        Set-QiQiFilePackageAccess `
            -Path $target.Path `
            -Mask $target.Mask `
            -Inheritable $target.Inheritable `
            -Sid $sid `
            -Mode $mode
        ++$appliedCount
    }
}

if ($WhatIfPreference) {
    Write-Host (
        "Preview complete for package family " +
        "'$family' ($sidText). No changes were made.")
} else {
    if ($appliedCount -ne $operationCount) {
        Write-Warning (
            "$mode applied $appliedCount of " +
            "$operationCount operations; skipped operations " +
            'remain unapplied.')
        exit 2
    }
    Write-Host (
        "$mode complete for package family " +
        "'$family' ($sidText). Restart the Broker " +
        'after granting access.')
}
