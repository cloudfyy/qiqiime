# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [Parameter(Mandatory = $true)]
    [string]$UnihanZip,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (!$OutputPath) { $OutputPath = Join-Path $PSScriptRoot '..\data\pinyin.idx' }
$expected = 'F7A48B2B545ACFAA77B2D607AE28747404CE02BAEFEE16396C5D2D7A8EF34B5E'
if ((Get-FileHash -LiteralPath $UnihanZip -Algorithm SHA256).Hash -cne $expected) {
    throw 'Expected the pinned Unicode 17.0.0 Unihan.zip.'
}

$syllables = [Collections.Generic.Dictionary[uint32, Collections.Generic.Dictionary[uint32, uint32]]]::new()
function Add-PinyinReading([string]$Reading, [uint32]$Scalar, [uint32]$Rank) {
    $letters = $Reading.Normalize([Text.NormalizationForm]::FormD) -creplace 'u\u0308', 'v' -creplace '\p{M}', ''
    if ($letters -cnotmatch '^[a-z]{1,6}$') {
        throw "Unexpected Unihan reading '$Reading' for U+$('{0:X}' -f $Scalar)."
    }
    [uint32]$key = 0
    foreach ($letter in $letters.ToCharArray()) {
        $key = ($key -shl 5) -bor ([int]$letter - [int][char]'a' + 1)
    }
    [Collections.Generic.Dictionary[uint32, uint32]]$group = $null
    if (!$syllables.TryGetValue($key, [ref]$group)) {
        $group = [Collections.Generic.Dictionary[uint32, uint32]]::new()
        $syllables.Add($key, $group)
    }
    [uint32]$old = 0
    if (!$group.TryGetValue($Scalar, [ref]$old) -or $Rank -gt $old) {
        $group[$Scalar] = $Rank
    }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $UnihanZip).Path)
try {
    $reader = [IO.StreamReader]::new($archive.GetEntry('Unihan_Readings.txt').Open(),
        [Text.UTF8Encoding]::new($false, $true))
    try {
        while ($null -ne ($line = $reader.ReadLine())) {
            if ($line.Length -eq 0 -or $line.StartsWith('#')) { continue }
            $fields = $line.Split([char]9)
            $property = $fields[1]
            if ($property -cnotin 'kMandarin', 'kHanyuPinyin', 'kHanyuPinlu') { continue }
            $scalar = [Convert]::ToUInt32($fields[0].Substring(2), 16)
            foreach ($value in $fields[2].Split(' ')) {
                switch -CaseSensitive ($property) {
                    'kMandarin' { Add-PinyinReading $value $scalar 1 }
                    'kHanyuPinyin' {
                        foreach ($reading in $value.Split(':')[1].Split(',')) {
                            Add-PinyinReading $reading $scalar 0
                        }
                    }
                    'kHanyuPinlu' {
                        if ($value -cnotmatch '^([^(]+)\(([0-9]+)\)$') { throw 'Malformed kHanyuPinlu entry.' }
                        Add-PinyinReading $Matches[1] $scalar ([uint32]$Matches[2] + 1)
                    }
                }
            }
        }
    } finally {
        $reader.Dispose()
    }
} finally {
    $archive.Dispose()
}

[uint32[]]$keys = @($syllables.Keys)
[Array]::Sort($keys)
[uint32]$count = 0
foreach ($group in $syllables.Values) { $count += $group.Count }

$stream = [IO.MemoryStream]::new()
try {
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([uint32]0x49595051) # QPYI
        $writer.Write([uint32]1)
        $writer.Write($count)
        $writer.Write([uint32]0)
        foreach ($key in $keys) {
            $ranked = $syllables[$key].GetEnumerator() |
                Sort-Object @{ Expression = 'Value'; Descending = $true }, 'Key'
            foreach ($record in $ranked) {
                $writer.Write($key)
                $writer.Write([uint32]$record.Key)
            }
        }
        $writer.Flush()
        [IO.File]::WriteAllBytes([IO.Path]::GetFullPath($OutputPath), $stream.ToArray())
    } finally {
        $writer.Dispose()
    }
} finally {
    $stream.Dispose()
}
Write-Output "Generated $count distinct pinyin readings across $($keys.Length) syllables."
