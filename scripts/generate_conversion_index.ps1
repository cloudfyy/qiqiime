# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [Parameter(Mandatory = $true)]
    [string]$OpenCCDictionaryDirectory,
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\data\conv.idx')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# OpenCC ver.1.4.2 (025f371dc76b598d77384fbdab90c937471844d8), Apache-2.0.
$expected = @{
    'STCharacters.txt' = 'A0CA1601C70648CF48B33C3C6210CCBECC5C7EEAD4B4C3DAF76587BA2C03582B'
    'STPhrases.txt' = 'F6EAB5E5C6DD7640597878D3DFC6599EE1279D2BC91561EADD8E114194E2925A'
    'TSCharacters.txt' = '737C21C66F55A419DD6956CB3089476CDEFC5A36877452631617696DF1E5D925'
    'TSPhrases.txt' = '362FA1B9A7D6EDD04B462A32E12C9FEF3ADAE822AB1DEE9C83561CC37C06CB1F'
}

function Read-Mappings([string[]]$Names) {
    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $table = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
    foreach ($name in $Names) {
        $path = Join-Path $OpenCCDictionaryDirectory $name
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -cne $expected[$name]) {
            throw "Unexpected OpenCC source checksum: $name."
        }
        foreach ($line in [IO.File]::ReadLines($path, $utf8)) {
            if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) { continue }
            $fields = $line.Split([char]9)
            if ($fields.Length -ne 2) { throw "Invalid OpenCC row in $name." }
            $key = $fields[0]
            $value = $fields[1].Split(' ')[0]
            if ($key.Length -lt 1 -or $key.Length -gt 32 -or $value.Length -lt 1 -or $value.Length -gt 32) {
                throw "Invalid OpenCC key or value length in $name."
            }
            # A phrase row takes precedence over an identical key in the character table.
            $table[$key] = $value
        }
    }
    return ,$table
}

$simplified = Read-Mappings @('STCharacters.txt', 'STPhrases.txt')
$traditional = Read-Mappings @('TSCharacters.txt', 'TSPhrases.txt')
$utf16 = [Text.UnicodeEncoding]::new($false, $false, $true)
$index = [IO.MemoryStream]::new()
$pool = [IO.MemoryStream]::new()
try {
    $writer = [IO.BinaryWriter]::new($index)
    try {
        $writer.Write([uint32]0x564E4351) # QCNV
        $writer.Write([uint32]1)
        $writer.Write([uint32]$simplified.Count)
        $writer.Write([uint32]$traditional.Count)
        foreach ($table in $simplified, $traditional) {
            [string[]]$keys = @($table.Keys)
            [Array]::Sort($keys, [StringComparer]::Ordinal)
            foreach ($key in $keys) {
                $value = $table[$key]
                $writer.Write([uint32]($pool.Length / 2))
                $writer.Write([uint16]$key.Length)
                $writer.Write([uint16]$value.Length)
                $bytes = $utf16.GetBytes($key + $value)
                $pool.Write($bytes, 0, $bytes.Length)
            }
        }
        $writer.Write($pool.ToArray())
        $writer.Flush()
        [IO.File]::WriteAllBytes([IO.Path]::GetFullPath($OutputPath), $index.ToArray())
    } finally {
        $writer.Dispose()
    }
} finally {
    $pool.Dispose()
    $index.Dispose()
}
Write-Output "Generated $($simplified.Count) simplified-to-traditional and $($traditional.Count) traditional-to-simplified mappings."
