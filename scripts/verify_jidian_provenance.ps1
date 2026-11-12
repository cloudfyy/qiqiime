param(
    [Parameter(Mandatory = $true)]
    [string]$UpstreamDirectory,
    [string]$DictionaryDirectory = (Join-Path $PSScriptRoot "..\data")
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$utf8 = [System.Text.UTF8Encoding]::new($false, $true)
$licensePath = Join-Path $UpstreamDirectory "LICENSE"
$licenseHash = "43070e2d4e532684de521b885f385d0841030efa2b1a20bafb76133a5e1379c1"
if ((Get-FileHash -LiteralPath $licensePath -Algorithm SHA256).Hash -ne $licenseHash) {
    throw "Upstream LICENSE does not match the pinned Apache-2.0 snapshot."
}

foreach ($dictionary in @(
    @{ Name = "wubi86_jidian"; Hash = "89df3685e4a0c3d319a1c56a3a64e3df4ee3bfa3ba3892a6dadc77b21c989c5b"; Filtered = 3; Duplicates = 3 },
    @{ Name = "wubi86_jidian_extra"; Hash = "3cf5f6110ab0df9157009bb88d3164c5405c970218e6888d08c5e67fc97a6e9c"; Filtered = 10; Duplicates = 53 }
)) {
    $source = Join-Path $UpstreamDirectory "$($dictionary.Name).dict.yaml"
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $dictionary.Hash) {
        throw "Upstream dictionary does not match the pinned snapshot: $($dictionary.Name)"
    }
    $lines = [System.IO.File]::ReadAllLines((Resolve-Path -LiteralPath $source).Path, $utf8)
    $tableStart = [Array]::IndexOf($lines, "...")
    if ($tableStart -lt 0) { throw "Missing Rime table delimiter: $($dictionary.Name)" }
    $groups = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    $filtered = 0
    $duplicates = 0
    for ($i = $tableStart + 1; $i -lt $lines.Length; $i++) {
        $line = $lines[$i]
        if ([string]::IsNullOrWhiteSpace($line) -or $line.TrimStart().StartsWith("#")) { continue }
        $fields = $line.Split([char]9)
        if ($fields.Length -lt 2) { throw "Missing table columns at source line $($i + 1)." }
        $word = $fields[0].Trim()
        $code = $fields[1].Trim().ToLowerInvariant()
        if ($word.Length -eq 0 -or $word -match '\s' -or $code -cnotmatch '^[a-y]{1,4}$') {
            $filtered++
            continue
        }
        $weightText = if ($fields.Length -gt 2) { $fields[2].Split([char]35)[0].Trim() } else { "" }
        $weight = [decimal]0
        if ($weightText.Length -gt 0) {
            if ($weightText -notmatch '^\d+(\.\d+)?$') { throw "Invalid weight at source line $($i + 1)." }
            $weight = [decimal]::Parse($weightText, [Globalization.CultureInfo]::InvariantCulture)
        }
        if (-not $groups.ContainsKey($code)) {
            $groups.Add($code, [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal))
        }
        $words = $groups[$code]
        if ($words.ContainsKey($word)) {
            $duplicates++
            if ($weight -le $words[$word].Weight) { continue }
        }
        $words[$word] = [pscustomobject]@{ Word = $word; Weight = $weight; Row = $i }
    }
    if ($filtered -ne $dictionary.Filtered -or $duplicates -ne $dictionary.Duplicates) {
        throw "Filtering or duplicate counts changed: $($dictionary.Name)"
    }
    [string[]]$codes = @($groups.Keys)
    [Array]::Sort($codes, [StringComparer]::Ordinal)
    $expected = [System.Collections.Generic.List[string]]::new()
    $candidateCount = 0
    foreach ($code in $codes) {
        $entries = $groups[$code].Values
        if ($entries.Count -gt 1) {
            $entries = @($entries | Sort-Object @{ Expression = "Weight"; Descending = $true }, Row)
        }
        $words = @($entries | ForEach-Object { $_.Word })
        $candidateCount += $words.Count
        $expected.Add($code + " " + ($words -join " "))
    }
    $target = Join-Path $DictionaryDirectory "$($dictionary.Name).txt"
    $actual = @(
        foreach ($line in [System.IO.File]::ReadAllLines((Resolve-Path -LiteralPath $target).Path, $utf8)) {
            if (-not [string]::IsNullOrWhiteSpace($line) -and -not $line.TrimStart().StartsWith("#")) { $line }
        }
    )
    if ($actual.Count -ne $expected.Count) { throw "Converted row count mismatch: $($dictionary.Name)" }
    for ($i = 0; $i -lt $expected.Count; $i++) {
        if (-not [string]::Equals($actual[$i], $expected[$i], [StringComparison]::Ordinal)) {
            throw "Converted dictionary mismatch: $($dictionary.Name), data row $($i + 1)."
        }
    }
    Write-Output "$($dictionary.Name): exact match; codes=$($codes.Count), candidates=$candidateCount, filtered=$filtered, duplicates=$duplicates."
}
