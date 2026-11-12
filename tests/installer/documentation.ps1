$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "..\..\scripts\common.ps1")

$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ("qiqi-documentation-" + [guid]::NewGuid().ToString("N"))
$expected = @(
    "LICENSE",
    "README.md",
    "README.zh-CN.md",
    "PRIVACY.md",
    "CODE_SIGNING.md",
    "THIRD_PARTY_NOTICES.md",
    "deploy.md",
    "log.md",
    "release-notes.md",
    "licenses\Apache-2.0.txt",
    "licenses\rime-wubi86-jidian-MIT.txt",
    "licenses\wubi-haifeng-BSD-3-Clause.txt",
    "licenses\microsoft-gsl-MIT.txt"
)
try {
    $destination = Join-Path $fixture "stage"
    Copy-QiQiReleaseDocumentation -Destination $destination
    $actual = @(Get-ChildItem -LiteralPath $destination -Recurse -File)
    if ($actual.Count -ne $expected.Count) { throw "Unexpected number of staged documentation files." }
    foreach ($relativePath in $expected) {
        $sourceRelativePath = if ($relativePath -in @("deploy.md", "log.md", "release-notes.md")) {
            "docs\$relativePath"
        } else {
            $relativePath
        }
        $source = Join-Path $QiQiRepoRoot $sourceRelativePath
        $target = Join-Path $destination $relativePath
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            throw "Missing staged documentation: $relativePath"
        }
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
            throw "Documentation changed during staging: $relativePath"
        }
    }

    [xml]$installer = Get-Content (Join-Path $QiQiRepoRoot "installer\QiQiIME.wxs") -Raw -Encoding UTF8
    $ns = [System.Xml.XmlNamespaceManager]::new($installer.NameTable)
    $ns.AddNamespace("w", "http://wixtoolset.org/schemas/v4/wxs")
    $directory = $installer.SelectSingleNode("//w:Directory[@Id='DirLicenses']", $ns)
    if ($null -eq $directory -or $directory.Name -ne "licenses" -or $directory.ParentNode.Id -ne "INSTALLFOLDER") {
        throw "MSI licenses must be installed under INSTALLFOLDER\licenses."
    }
    $components = $installer.SelectNodes("//w:ComponentGroup[@Id='ProductComponents']/w:Component[@Id='CmpDocs' or @Id='CmpLicenses']", $ns)
    $msiPaths = @(
        foreach ($component in $components) {
            $expectedDirectory = if ($component.Id -eq "CmpDocs") { "INSTALLFOLDER" } else { "DirLicenses" }
            if ($component.Directory -ne $expectedDirectory) { throw "Incorrect MSI documentation component directory." }
            foreach ($file in $component.SelectNodes("w:File", $ns)) {
                $prefix = '$(var.StageDir)\'
                if (-not $file.Source.StartsWith($prefix, [StringComparison]::Ordinal)) {
                    throw "MSI documentation must use the release staging directory."
                }
                $file.Source.Substring($prefix.Length)
            }
        }
    )
    if (@(Compare-Object $expected $msiPaths).Count -ne 0) {
        throw "MSI and ZIP staging must contain the same required documentation and licenses."
    }

    $sourceFixture = Join-Path $fixture "source"
    foreach ($relativePath in $expected) {
        $sourceRelativePath = if ($relativePath -in @("deploy.md", "log.md", "release-notes.md")) {
            "docs\$relativePath"
        } else {
            $relativePath
        }
        $target = Join-Path $sourceFixture $sourceRelativePath
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $destination $relativePath) -Destination $target
    }
    $missing = Join-Path $sourceFixture "licenses\wubi-haifeng-BSD-3-Clause.txt"
    Remove-Item -LiteralPath $missing
    $rejected = $false
    try {
        Copy-QiQiReleaseDocumentation -Destination $destination -SourceDirectory $sourceFixture
    } catch {
        if ($_.Exception.Message -ne "Missing release documentation: $missing") { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Missing license must fail even when an older staged copy exists." }
    $header = [System.Collections.Generic.List[string]]::new()
    $inLicense = $false
    foreach ($line in (Get-Content -LiteralPath (Join-Path $QiQiRepoRoot "data\wubi86_haifeng.txt") -Encoding UTF8)) {
        if ($line.StartsWith("# Copyright (c) 2010, SUN Haifeng", [StringComparison]::Ordinal)) {
            $inLicense = $true
        }
        if ($inLicense) {
            if (-not $line.StartsWith("#", [StringComparison]::Ordinal)) { break }
            $text = $line.Substring(1)
            if ($text.StartsWith(" ", [StringComparison]::Ordinal)) { $text = $text.Substring(1) }
            $header.Add($text)
        }
    }
    $bsd = Get-Content -LiteralPath (Join-Path $QiQiRepoRoot "licenses\wubi-haifeng-BSD-3-Clause.txt") -Raw -Encoding UTF8
    $expectedNotice = $bsd.Replace("`r`n", "`n").TrimEnd()
    if (-not [string]::Equals(($header -join "`n").TrimEnd(), $expectedNotice, [StringComparison]::Ordinal)) {
        throw "Haifeng dictionary header must retain the complete BSD copyright, conditions and disclaimer."
    }
    Write-Host "Documentation staging, MSI payload parity, missing-license and dictionary-notice checks passed."
} finally {
    if (Test-Path -LiteralPath $fixture) {
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
