# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$roles = @(
    'Background', 'Border', 'CodeText', 'CandidateText', 'SecondaryText',
    'SelectedBackground', 'SelectedText', 'SelectedBorder', 'EmptyText',
    'PageEnabledText', 'PageDisabledText', 'MenuHoverBackground', 'MenuHoverText'
)

function Assert-Contract {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Convert-Rgb24 {
    param([string]$Value)
    return [uint32]::Parse($Value.Substring(2), [Globalization.NumberStyles]::HexNumber)
}

function Read-PaletteValues {
    param([string]$Text)
    $values = @([regex]::Matches($Text, '0x[0-9A-Fa-f]{8}'))
    Assert-Contract ($values.Count -eq $roles.Count) "Expected $($roles.Count) palette fields, found $($values.Count)."
    return @($values | ForEach-Object { Convert-Rgb24 $_.Value })
}

$skinHeaderPath = Join-Path $RepositoryRoot 'src\common\skin.h'
$skinHeaderText = [IO.File]::ReadAllText($skinHeaderPath, [Text.Encoding]::UTF8)
$enumColorMatch = [regex]::Match(
    $skinHeaderText,
    'enum class SkinColor\s*\{(?<Body>.*?)\};',
    [Text.RegularExpressions.RegexOptions]::Singleline)
Assert-Contract $enumColorMatch.Success 'Unable to parse SkinColor.'
$enumRoles = @()
foreach ($entry in ($enumColorMatch.Groups['Body'].Value -split ',')) {
    if ($entry -match '^\s*(?<Name>[A-Za-z]\w*)') {
        $name = $Matches.Name
        if ($name -ne 'Count') {
            $enumRoles += $name
        }
    }
}
$roleMatch = [regex]::Match(
    $skinHeaderText,
    'kSkinColorNames\s*\{(?<Body>.*?)\};',
    [Text.RegularExpressions.RegexOptions]::Singleline)
Assert-Contract $roleMatch.Success 'Unable to parse kSkinColorNames.'
$registryRoles = @([regex]::Matches($roleMatch.Groups['Body'].Value, 'L"(?<Role>[^"]+)"') |
    ForEach-Object { $_.Groups['Role'].Value })
Assert-Contract ($enumRoles.Count -eq $roles.Count) 'SkinColor enum role count differs from the design.'
Assert-Contract ($registryRoles.Count -eq $roles.Count) 'Registry role count differs from the design.'
for ($roleIndex = 0; $roleIndex -lt $roles.Count; ++$roleIndex) {
    Assert-Contract ($enumRoles[$roleIndex] -eq $roles[$roleIndex]) "SkinColor order differs at role $roleIndex."
    Assert-Contract ($registryRoles[$roleIndex] -eq $roles[$roleIndex]) "Registry role order differs at role $roleIndex."
}

$designPath = Join-Path $RepositoryRoot 'docs\ime-skin-palettes.md'
$designText = [IO.File]::ReadAllText($designPath, [Text.Encoding]::UTF8)
$design = [ordered]@{}
$currentDesign = $null
foreach ($line in ($designText -split '\r?\n')) {
    if ($line -match '^## [AB]\d+ (?<Zh>.+?) / (?<En>.+)$') {
        $currentDesign = [ordered]@{
            Id = $null
            NameZh = $Matches.Zh
            NameEn = $Matches.En
            Light = @{}
            Dark = @{}
        }
        continue
    }
    if ($null -eq $currentDesign) {
        continue
    }
    if ($line -match '^稳定标识：`(?<Id>[^`]+)`。$') {
        $currentDesign.Id = $Matches.Id
        Assert-Contract (-not $design.Contains($Matches.Id)) "Duplicate design skin ID: $($Matches.Id)"
        $design[$Matches.Id] = $currentDesign
        continue
    }
    if ($line -match '^\| (?<Role>\w+) \| `#(?<Light>[0-9A-Fa-f]{6})` \| `#(?<Dark>[0-9A-Fa-f]{6})` \|$') {
        $role = $Matches.Role
        Assert-Contract $roles.Contains($role) "Unknown design color role: $role"
        Assert-Contract (-not $currentDesign.Light.Contains($role)) "Duplicate design color role '$role' for '$($currentDesign.Id)'."
        $currentDesign.Light[$role] = [Convert]::ToUInt32($Matches.Light, 16)
        $currentDesign.Dark[$role] = [Convert]::ToUInt32($Matches.Dark, 16)
    }
}
Assert-Contract ($design.Count -eq 21) "Expected 21 design skins, found $($design.Count)."
foreach ($skin in $design.Values) {
    Assert-Contract ($null -ne $skin.Id) "A design skin is missing its stable ID."
    Assert-Contract ($skin.Light.Count -eq $roles.Count) "Design Light variant '$($skin.Id)' does not have 13 roles."
    Assert-Contract ($skin.Dark.Count -eq $roles.Count) "Design Dark variant '$($skin.Id)' does not have 13 roles."
}

$configPath = Join-Path $RepositoryRoot 'src\common\config.h'
$configText = [IO.File]::ReadAllText($configPath, [Text.Encoding]::UTF8)
$enumMatch = [regex]::Match(
    $configText,
    'enum class ImeSkinId\s*:\s*uint32_t\s*\{(?<Body>.*?)\};',
    [Text.RegularExpressions.RegexOptions]::Singleline)
Assert-Contract $enumMatch.Success 'Unable to parse ImeSkinId.'
$enumNames = @()
foreach ($entry in ($enumMatch.Groups['Body'].Value -split ',')) {
    if ($entry -match '^\s*(?<Name>[A-Za-z]\w*)') {
        $name = $Matches.Name
        if ($name -ne 'Invalid') {
            $enumNames += $name
        }
    }
}
$idMatch = [regex]::Match(
    $configText,
    'kImeSkinIds\s*\{(?<Body>.*?)\};',
    [Text.RegularExpressions.RegexOptions]::Singleline)
Assert-Contract $idMatch.Success 'Unable to parse kImeSkinIds.'
$skinIds = @([regex]::Matches($idMatch.Groups['Body'].Value, '"(?<Id>[^"]+)"') |
    ForEach-Object { $_.Groups['Id'].Value })
Assert-Contract ($enumNames.Count -eq 22) "Expected 22 valid ImeSkinId entries, found $($enumNames.Count)."
Assert-Contract ($skinIds.Count -eq $enumNames.Count) 'ImeSkinId and kImeSkinIds lengths differ.'
$idByEnum = @{}
for ($index = 0; $index -lt $enumNames.Count; ++$index) {
    $idByEnum[$enumNames[$index]] = $skinIds[$index]
}

$sourcePath = Join-Path $RepositoryRoot 'src\common\skin.cpp'
$sourceText = [IO.File]::ReadAllText($sourcePath, [Text.Encoding]::UTF8)
$sourceSkins = @()
foreach ($line in ($sourceText -split '\r?\n')) {
    $skinMatch = [regex]::Match(
        $line,
        '^\s*\{ImeSkinId::(?<Enum>\w+), L"(?<Zh>[^"]+)", L"(?<En>[^"]+)",')
    if (-not $skinMatch.Success) {
        continue
    }
    $enumName = $skinMatch.Groups['Enum'].Value
    Assert-Contract $idByEnum.ContainsKey($enumName) "Unknown source ImeSkinId: $enumName"
    $palettes = @([regex]::Matches($line, 'MakePalette\(\{(?<Colors>[^}]*)\}\)'))
    Assert-Contract ($palettes.Count -eq 2) "Source skin '$enumName' must contain Light and Dark palettes."
    $light = @(Read-PaletteValues $palettes[0].Groups['Colors'].Value)
    $dark = @(Read-PaletteValues $palettes[1].Groups['Colors'].Value)
    $sourceSkins += [pscustomobject]@{
        EnumName = $enumName
        Id = $idByEnum[$enumName]
        NameZh = $skinMatch.Groups['Zh'].Value
        NameEn = $skinMatch.Groups['En'].Value
        Light = $light
        Dark = $dark
    }
}
Assert-Contract ($sourceSkins.Count -eq 21) "Expected 21 source skins, found $($sourceSkins.Count)."
$sourceById = @{}
foreach ($skin in $sourceSkins) {
    Assert-Contract (-not $sourceById.ContainsKey($skin.Id)) "Duplicate source skin ID: $($skin.Id)"
    $sourceById[$skin.Id] = $skin
}

$registryPath = Join-Path $RepositoryRoot 'docs\ime-skins.reg'
$registryText = [IO.File]::ReadAllText($registryPath, [Text.Encoding]::Unicode)
$registryPrefix = 'HKEY_CURRENT_USER\Software\QiQiIME\Skins\Catalogs\builtin-v1'
$registryCatalog = @{}
$registrySkins = [ordered]@{}
$currentSkin = $null
$currentVariant = $null
foreach ($line in ($registryText -split '\r?\n')) {
    if ($line -match '^\[(?<Path>[^\]]+)\]$') {
        $path = $Matches.Path
        if ($path -eq $registryPrefix) {
            $currentSkin = $null
            $currentVariant = $null
            continue
        }
        Assert-Contract $path.StartsWith("$registryPrefix\", [StringComparison]::Ordinal) "Unexpected registry section: $path"
        $parts = $path.Substring($registryPrefix.Length + 1) -split '\\'
        Assert-Contract ($parts.Count -le 2) "Unexpected nested registry section: $path"
        $skinId = $parts[0]
        if (-not $registrySkins.Contains($skinId)) {
            $registrySkins[$skinId] = [ordered]@{
                NameZh = $null
                NameEn = $null
                DisplayOrder = $null
                Light = @{}
                Dark = @{}
            }
        }
        $currentSkin = $registrySkins[$skinId]
        if ($parts.Count -eq 2) {
            $currentVariant = $parts[1]
        } else {
            $currentVariant = $null
        }
        if ($null -ne $currentVariant) {
            Assert-Contract ($currentVariant -eq 'Light' -or $currentVariant -eq 'Dark') "Unknown skin variant '$currentVariant'."
        }
        continue
    }
    if ($null -eq $currentSkin -and $null -ne $currentVariant) {
        throw 'Registry parser entered an invalid section state.'
    }
    if ($line -match '^"(?<Name>[^"]+)"="(?<Value>.*)"$') {
        $name = $Matches.Name
        if ($null -eq $currentSkin) {
            $registryCatalog[$name] = $Matches.Value
        } elseif ($null -eq $currentVariant) {
            Assert-Contract ($name -eq 'NameZh' -or $name -eq 'NameEn') "Unexpected string field '$name' in registry skin."
            $currentSkin[$name] = $Matches.Value
        } else {
            throw "Unexpected string field '$name' in registry palette."
        }
        continue
    }
    if ($line -match '^"(?<Name>[^"]+)"=dword:(?<Value>[0-9A-Fa-f]{8})$') {
        $name = $Matches.Name
        $value = [uint32]::Parse($Matches.Value, [Globalization.NumberStyles]::HexNumber)
        if ($null -eq $currentSkin) {
            $registryCatalog[$name] = $value
        } elseif ($null -eq $currentVariant) {
            Assert-Contract ($name -eq 'DisplayOrder') "Unexpected DWORD field '$name' in registry skin."
            $currentSkin.DisplayOrder = $value
        } else {
            Assert-Contract $roles.Contains($name) "Unknown registry color role: $name"
            Assert-Contract (-not $currentSkin[$currentVariant].Contains($name)) "Duplicate registry color field: $currentVariant/$name"
            $currentSkin[$currentVariant][$name] = $value
        }
    }
}
Assert-Contract ($registryCatalog.SchemaVersion -eq 1) 'Registry template schema version must be 1.'
Assert-Contract ($registryCatalog.ColorEncoding -eq 'RGB24') 'Registry template color encoding must be RGB24.'
Assert-Contract ($registrySkins.Count -eq 21) "Expected 21 registry skins, found $($registrySkins.Count)."

$fieldCount = 0
$expectedIds = @($design.Keys)
for ($index = 0; $index -lt $expectedIds.Count; ++$index) {
    $skinId = $expectedIds[$index]
    $expected = $design[$skinId]
    Assert-Contract $sourceById.ContainsKey($skinId) "Source is missing design skin '$skinId'."
    Assert-Contract $registrySkins.Contains($skinId) "Registry template is missing design skin '$skinId'."
    $source = $sourceById[$skinId]
    $registry = $registrySkins[$skinId]
    Assert-Contract ($sourceSkins[$index].Id -eq $skinId) "Source display order differs at position $index."
    Assert-Contract ($source.NameZh -eq $expected.NameZh) "Source NameZh differs for '$skinId'."
    Assert-Contract ($source.NameEn -eq $expected.NameEn) "Source NameEn differs for '$skinId'."
    Assert-Contract ($registry.NameZh -eq $expected.NameZh) "Registry NameZh differs for '$skinId'."
    Assert-Contract ($registry.NameEn -eq $expected.NameEn) "Registry NameEn differs for '$skinId'."
    Assert-Contract ($registry.DisplayOrder -eq ($index + 1)) "Registry DisplayOrder differs for '$skinId'."
    foreach ($variant in @('Light', 'Dark')) {
        Assert-Contract ($registry[$variant].Count -eq $roles.Count) "Registry $variant '$skinId' does not have 13 fields."
        $sourcePalette = $source.$variant
        for ($roleIndex = 0; $roleIndex -lt $roles.Count; ++$roleIndex) {
            $role = $roles[$roleIndex]
            $expectedValue = $expected[$variant][$role]
            $sourceValue = $sourcePalette[$roleIndex]
            Assert-Contract ($sourceValue -eq $expectedValue) "Source $variant/$role differs for '$skinId'."
            Assert-Contract $registry[$variant].ContainsKey($role) "Registry $variant/$role is missing for '$skinId'."
            Assert-Contract ($registry[$variant][$role] -eq $expectedValue) "Registry $variant/$role differs for '$skinId'."
            ++$fieldCount
        }
    }
}
Assert-Contract ($fieldCount -eq 546) "Expected 546 verified color fields, found $fieldCount."
Write-Host '[PASS] 21 skins, 42 variants, and 546 design/source/registry color fields match.'
