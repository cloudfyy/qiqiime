# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = 'HKCU:\Software\QiQiIME'
if (-not (Test-Path -LiteralPath $root)) {
    throw 'The desktop QiQiIME registry root is unavailable.'
}

$config = Get-ItemProperty -LiteralPath $root
$skins = 'HKCU:\Software\QiQiIME\Skins'
$catalog = $null
if (Test-Path -LiteralPath $skins) {
    $catalog = (Get-ItemProperty -LiteralPath $skins).ActiveCatalog
}
$shadowProperty = $config.PSObject.Properties |
    Where-Object Name -eq 'PackagedHostShadow' |
    Select-Object -First 1

[pscustomobject]@{
    ConfigSequence = $config.ConfigSequence
    ImeSkinId = $config.ImeSkinId
    ActiveCatalog = $catalog
    PackagedHostShadow = if ($shadowProperty) {
        $shadowProperty.Value
    } else {
        $null
    }
} | ConvertTo-Json -Depth 2

