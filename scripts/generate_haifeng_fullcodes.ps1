# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [Parameter(Mandatory = $true)]
    [string]$UpstreamDirectory,
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\data\full.idx')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$hashes = @{
    'COPYING' = '818fe81683dc95644bbe5ee1c561920d9f8c4230e0b1372d638b84bc4bd48e40'
    'GBK.tab' = 'c3e06bdadd0c540285a345f07d7a79bdafd2778eb6546514421ae393acdca153'
    'CJKa.tab' = 'daf32ca238ce776cc5b971ce2d59b914bbbb8940723de7998b21e5e32cbe1ffb'
    'CJKb.tab' = 'ed108da1f1135ae0c5708b2d16341f7a3bd93f4c6e526c7efc548daff0e5f09b'
    'GBSimplified.tab' = 'e024ac5309ffa7ef6459bf416a31f512d102606abad3973f92f7d53af1c65d79'
}
foreach ($name in $hashes.Keys) {
    $path = Join-Path $UpstreamDirectory $name
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $hashes[$name]) {
        throw "Unexpected Haifeng source hash: $name"
    }
}

$codes = [Collections.Generic.Dictionary[int, string]]::new()
foreach ($name in 'GBK.tab', 'CJKa.tab', 'CJKb.tab') {
    $path = Join-Path $UpstreamDirectory $name
    foreach ($line in [IO.File]::ReadLines($path, $utf8) | Select-Object -Skip 1) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $fields = $line.Split([char]9)
        $code = $fields[3].Trim().ToLowerInvariant()
        if ($code -ceq '.') { continue }
        $word = if ($name -eq 'CJKb.tab') { $fields[2].TrimEnd('.') } else { $fields[2] }
        $scalar = [char]::ConvertToUtf32($word, 0)
        if ($code -cnotmatch '^[a-y]{1,4}$' -or $word -cne [char]::ConvertFromUtf32($scalar) -or
            ($name -ne 'GBK.tab' -and $scalar -ne [Convert]::ToInt32($fields[1], 16))) {
            throw "Invalid full-code row in $name (U+$('{0:X}' -f $scalar))."
        }
        $codes.Add($scalar, $code)
    }
}

$crossCheckPath = Join-Path $UpstreamDirectory 'GBSimplified.tab'
foreach ($line in [IO.File]::ReadLines($crossCheckPath, $utf8) | Select-Object -Skip 1) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $fields = $line.Split([char]9)
    foreach ($index in 0, 3) {
        $code = $fields[$index + 2]
        if ($code -cnotmatch '^[a-y]{1,4}$') { continue }
        $scalar = [char]::ConvertToUtf32($fields[$index], 0)
        if (!$codes.ContainsKey($scalar) -or $codes[$scalar] -cne $code) {
            throw "The Haifeng primary and shorthand tables disagree for U+$('{0:X}' -f $scalar)."
        }
    }
}

[int[]]$sorted = @($codes.Keys)
[Array]::Sort($sorted)
$output = [IO.MemoryStream]::new()
try {
    $writer = [IO.BinaryWriter]::new($output)
    try {
        $writer.Write([uint32]0x58444951) # QIDX
        $writer.Write([uint32]1)
        $writer.Write([uint32]$sorted.Length)
        $writer.Write([uint32]0)
        foreach ($scalar in $sorted) {
            $code = $codes[$scalar]
            $packed = 0
            for ($letter = 0; $letter -lt $code.Length; ++$letter) {
                $packed = $packed -bor (([int]$code[$letter] - [int][char]'a' + 1) -shl (8 * (3 - $letter)))
            }
            $writer.Write([uint32]$packed)
            $writer.Write([uint32]$scalar)
        }
        $writer.Flush()
        [IO.File]::WriteAllBytes([IO.Path]::GetFullPath($OutputPath), $output.ToArray())
    } finally {
        $writer.Dispose()
    }
} finally {
    $output.Dispose()
}
Write-Output "Generated $($codes.Count) Haifeng Wubi 86 full codes."
