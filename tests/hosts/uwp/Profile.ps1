# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

function Get-QiQiUwpProfile {
    param([ValidateSet('zero', 'com', 'reg')][string] $Name)

    $name = $Name.ToLowerInvariant()
    $profile = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot "$name\app.psd1")
    $capability = @{ zero = ''; com = 'lpacCom'; reg = 'registryRead' }[$name]
    if ($profile.Capability -cne $capability -or [string]::IsNullOrWhiteSpace($profile.Title)) {
        throw "Unexpected UWP test profile: $name."
    }
    return [pscustomobject]@{
        Name = $name
        Identity = "Cloudfyy.QiQiUwp.$name"
        Binary = "uwp-$name"
        Title = $profile.Title
        Capability = $capability
    }
}

function New-QiQiUwpManifest {
    param($Profile, [string] $Version)

    [xml] $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'app.xml') -Raw
    $root = $manifest.DocumentElement
    $identity = $root.SelectSingleNode("*[local-name()='Identity']")
    $identity.SetAttribute('Name', $Profile.Identity)
    $identity.SetAttribute('Version', $Version)
    $root.SelectSingleNode("*[local-name()='Properties']/*[local-name()='DisplayName']").InnerText = $Profile.Title
    $app = $root.SelectSingleNode("*[local-name()='Applications']/*[local-name()='Application']")
    $app.SetAttribute('Executable', "$($Profile.Binary).exe")
    $app.SelectSingleNode("*[local-name()='VisualElements']").SetAttribute('DisplayName', $Profile.Title)
    if ($Profile.Capability) {
        $capabilities = $manifest.CreateElement('Capabilities', $root.NamespaceURI)
        $capability = $manifest.CreateElement('rescap', 'Capability', $root.GetNamespaceOfPrefix('rescap'))
        $capability.SetAttribute('Name', $Profile.Capability)
        [void] $capabilities.AppendChild($capability)
        [void] $root.AppendChild($capabilities)
    }
    return $manifest
}
