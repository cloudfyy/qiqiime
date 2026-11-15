# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing

$sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Read-ThemeIcon([string]$Path, [byte]$Channel) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        Assert-True ($reader.ReadUInt16() -eq 0 -and $reader.ReadUInt16() -eq 1) "Invalid ICO header: $Path"
        $count = $reader.ReadUInt16()
        Assert-True ($count -eq $sizes.Count) "Incorrect frame count: $Path"
        $entries = for ($i = 0; $i -lt $count; $i++) {
            $widthByte = $reader.ReadByte()
            $heightByte = $reader.ReadByte()
            $width = if ($widthByte -eq 0) { 256 } else { [int]$widthByte }
            $height = if ($heightByte -eq 0) { 256 } else { [int]$heightByte }
            Assert-True ($width -eq $sizes[$i] -and $height -eq $width) "Incorrect ICO dimensions at frame ${i}: $Path"
            Assert-True ($reader.ReadByte() -eq 0 -and $reader.ReadByte() -eq 0) "Invalid 32-bit ICO directory: $Path"
            Assert-True ($reader.ReadUInt16() -eq 1 -and $reader.ReadUInt16() -eq 32) "Expected 32-bit icon frame: $Path"
            $length = $reader.ReadUInt32()
            $offset = $reader.ReadUInt32()
            Assert-True ($length -gt 0 -and $offset -ge (6 + 16 * $count) -and
                ([long]$offset + $length) -le $stream.Length) "Invalid ICO frame bounds: $Path"
            [PSCustomObject]@{ Size = $width; Length = $length; Offset = $offset }
        }

        foreach ($entry in $entries) {
            $stream.Position = $entry.Offset
            $bytes = $reader.ReadBytes([int]$entry.Length)
            Assert-True ($bytes.Length -eq $entry.Length) "Truncated ICO frame: $Path"
            $imageStream = [IO.MemoryStream]::new($bytes)
            $image = $null
            try {
                $image = [Drawing.Bitmap]::new($imageStream)
                $size = $entry.Size
                Assert-True ($image.Width -eq $size -and $image.Height -eq $size) "Encoded image dimensions differ: $Path"
                $alpha = [byte[]]::new($size * $size)
                $opaque = 0
                $transparent = 0
                $partial = 0
                for ($y = 0; $y -lt $size; $y++) {
                    for ($x = 0; $x -lt $size; $x++) {
                        $pixel = $image.GetPixel($x, $y)
                        $alpha[$y * $size + $x] = $pixel.A
                        if ($pixel.A -eq 0) {
                            $transparent++
                            continue
                        }
                        if ($pixel.R -ne $Channel -or $pixel.G -ne $Channel -or $pixel.B -ne $Channel) {
                            throw "Unexpected glyph color at ${size}px (${x}, ${y}): $Path"
                        }
                        if ($x -eq 0 -or $y -eq 0 -or $x -eq ($size - 1) -or $y -eq ($size - 1)) {
                            throw "Glyph touches the outer edge at ${size}px: $Path"
                        }
                        if ($pixel.A -eq 255) { $opaque++ } else { $partial++ }
                    }
                }
                Assert-True ($opaque -gt ($alpha.Length / 12) -and $opaque -lt ($alpha.Length / 2)) "Empty or filled icon at ${size}px: $Path"
                Assert-True ($transparent -gt ($alpha.Length / 3) -and $partial -gt 0) "Missing transparency or antialiasing at ${size}px: $Path"
                [PSCustomObject]@{ Size = $size; Alpha = $alpha }
            } finally {
                if ($null -ne $image) { $image.Dispose() }
                $imageStream.Dispose()
            }
        }
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

$light = @(Read-ThemeIcon (Join-Path $repoRoot 'data\qiqiime-light.ico') 0)
$dark = @(Read-ThemeIcon (Join-Path $repoRoot 'data\qiqiime-dark.ico') 255)
for ($i = 0; $i -lt $sizes.Count; $i++) {
    Assert-True ([Convert]::ToBase64String($light[$i].Alpha) -ceq [Convert]::ToBase64String($dark[$i].Alpha)) "Theme glyph shapes differ at $($sizes[$i])px."
}
Write-Host 'IME brand icon assets passed: ten sizes, black/white glyphs, identical alpha, transparent margins.'
