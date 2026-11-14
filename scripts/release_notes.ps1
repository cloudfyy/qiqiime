# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$NotesPath,
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [Parameter(Mandatory = $true)]
    [string]$Tag,
    [Parameter(Mandatory = $true)]
    [string]$Repository,
    [Parameter(Mandatory = $true)]
    [string[]]$AssetNames,
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$notes = Get-Content -LiteralPath $NotesPath -Raw -Encoding UTF8
$headings = [regex]::Matches($notes, '(?m)^# [^\r\n]+')
$current = @($headings | Where-Object {
    $_.Value -match ('^# QiQiIME ' + [regex]::Escape($Version) + '(?:\s|$)')
})
if ($current.Count -ne 1) {
    throw "Expected exactly one release-notes section for version $Version; found $($current.Count)."
}
$start = $current[0].Index
$end = $notes.Length
foreach ($heading in $headings) {
    if ($heading.Index -gt $start) {
        $end = $heading.Index
        break
    }
}
$section = $notes.Substring($start, $end - $start).Trim()
$section = [regex]::Replace($section, '\r?\n---\s*$', '').TrimEnd()
$englishHeadings = [regex]::Matches($section, '(?m)^## English\r?$')
if ($englishHeadings.Count -ne 1) {
    throw "Expected exactly one English translation for version $Version; found $($englishHeadings.Count)."
}
$englishHeading = $englishHeadings[0]
$chineseSection = $section.Substring(0, $englishHeading.Index).TrimEnd()
$englishSection = $section.Substring($englishHeading.Index + $englishHeading.Length).Trim()
if ([string]::IsNullOrWhiteSpace($englishSection)) {
    throw "English translation for version $Version is empty."
}
$releaseUrl = "https://github.com/$Repository"
$repositoryParts = $Repository.Split('/')
$websiteUrl = "https://$($repositoryParts[0]).github.io/$($repositoryParts[1])/"
$encodedTag = [uri]::EscapeDataString($Tag)
$downloads = @()
$orderedAssets = $AssetNames | Sort-Object -Unique -Property @{
    Expression = { $_ -notmatch '\.msi$' }
}, @{
    Expression = { $_ }
}
foreach ($name in $orderedAssets) {
    if ([string]::IsNullOrWhiteSpace($name) -or [IO.Path]::GetFileName($name) -ne $name -or
        $name -notmatch '\.(msi|zip)$') {
        throw "Invalid release asset name: $name"
    }
    $encodedName = [uri]::EscapeDataString($name)
    $downloads += "- [$name]($releaseUrl/releases/download/$encodedTag/$encodedName)"
}
$lines = @(
    '## 下载',
    '',
    "[项目官网]($websiteUrl) | [下载安装](${websiteUrl}install.html)",
    ''
) + $downloads + @(
    '',
    '请按 Windows 系统架构选择 MSI：x64 安装包包含 WOW64 支持；x86 安装包用于 32 位 Windows。',
    '双架构 ZIP 用于手动部署；symbols ZIP 为调试符号，普通用户无需下载。',
    '',
    "[安装指南]($releaseUrl/blob/$encodedTag/docs/install.md) | [完整版本历史]($releaseUrl/blob/$encodedTag/docs/release-notes.md)",
    '',
    $chineseSection,
    '',
    '<details>',
    '<summary>English</summary>',
    '',
    '## Downloads',
    '',
    "[Website](${websiteUrl}?lang=en) | [Download & install](${websiteUrl}install.html?lang=en)",
    ''
) + $downloads + @(
    '',
    'Choose the MSI by operating-system architecture: x64 includes WOW64 support; x86 is for 32-bit Windows.',
    'The dual-architecture ZIP is for manual deployment; the symbols ZIP is for debugging.',
    '',
    "[Installation guide]($releaseUrl/blob/$encodedTag/docs/install.md) | [Full release history]($releaseUrl/blob/$encodedTag/docs/release-notes.md)",
    '',
    "# QiQiIME $Version",
    '',
    $englishSection,
    '',
    '</details>'
)
[IO.File]::WriteAllText($OutputPath, ($lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
