# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing

$requiredSizes = @(16, 20, 24, 32, 40, 48)
$expectedSizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Read-IcoFrames([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        Assert-True ($reader.ReadUInt16() -eq 0 -and $reader.ReadUInt16() -eq 1) "Invalid ICO header: $Path"
        $count = $reader.ReadUInt16()
        Assert-True ($count -ge $requiredSizes.Count) "Insufficient ICO frames: $Path"
        $entries = for ($i = 0; $i -lt $count; $i++) {
            $widthByte = $reader.ReadByte()
            $heightByte = $reader.ReadByte()
            $width = if ($widthByte -eq 0) { 256 } else { [int]$widthByte }
            $height = if ($heightByte -eq 0) { 256 } else { [int]$heightByte }
            Assert-True ($width -gt 0 -and $height -eq $width) "Invalid ICO dimensions at frame ${i}: $Path"
            Assert-True ($reader.ReadByte() -eq 0 -and $reader.ReadByte() -eq 0) "Invalid ICO color directory: $Path"
            Assert-True ($reader.ReadUInt16() -eq 1 -and $reader.ReadUInt16() -eq 32) "Expected 32-bit alpha icon frame: $Path"
            $length = $reader.ReadUInt32()
            $offset = $reader.ReadUInt32()
            Assert-True ($length -gt 0 -and $offset -ge (6 + 16 * $count) -and
                ([long]$offset + $length) -le $stream.Length) "Invalid ICO frame bounds: $Path"
            [PSCustomObject]@{ Size = $width; Length = $length; Offset = $offset }
        }

        $seen = [Collections.Generic.HashSet[int]]::new()
        $frames = [Collections.Generic.List[object]]::new()
        foreach ($entry in $entries) {
            $added = $seen.Add($entry.Size)
            Assert-True $added "Duplicate $($entry.Size)px ICO frame: $Path"
            $stream.Position = $entry.Offset
            $bytes = $reader.ReadBytes([int]$entry.Length)
            Assert-True ($bytes.Length -eq $entry.Length) "Truncated ICO frame: $Path"
            $imageStream = [IO.MemoryStream]::new($bytes)
            $image = $null
            try {
                $image = [Drawing.Bitmap]::new($imageStream)
                $dimensionsMatch = $image.Width -eq $entry.Size -and $image.Height -eq $entry.Size
                Assert-True $dimensionsMatch "Encoded image dimensions differ from the ICO directory: $Path"
                $alphaFormat = $image.PixelFormat -eq [Drawing.Imaging.PixelFormat]::Format32bppArgb -or
                               $image.PixelFormat -eq [Drawing.Imaging.PixelFormat]::Format32bppPArgb
                Assert-True $alphaFormat "Decoded ICO frame does not contain a 32-bit alpha plane: $Path"
                $pixels = [uint32[]]::new($entry.Size * $entry.Size)
                for ($y = 0; $y -lt $entry.Size; $y++) {
                    for ($x = 0; $x -lt $entry.Size; $x++) {
                        $pixel = $image.GetPixel($x, $y)
                        $pixels[$y * $entry.Size + $x] =
                            ([uint32]$pixel.A -shl 24) -bor
                            ([uint32]$pixel.R -shl 16) -bor
                            ([uint32]$pixel.G -shl 8) -bor
                            [uint32]$pixel.B
                    }
                }
                $frames.Add([PSCustomObject]@{
                    Size = $entry.Size
                    Pixels = $pixels
                    PixelFormat = $image.PixelFormat
                }) | Out-Null
            } finally {
                if ($null -ne $image) { $image.Dispose() }
                $imageStream.Dispose()
            }
        }
        foreach ($required in $requiredSizes) {
            Assert-True $seen.Contains($required) "Missing required $required px ICO frame: $Path"
        }
        $frames.ToArray()
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Test-BrandIconAsset([string]$Path) {
    $frames = @(Read-IcoFrames $Path)
    foreach ($frame in $frames) {
        $size = $frame.Size
        $black = 0
        $white = 0
        $antialiased = 0
        for ($y = 0; $y -lt $size; $y++) {
            for ($x = 0; $x -lt $size; $x++) {
                $pixel = $frame.Pixels[$y * $size + $x]
                $alpha = [byte](($pixel -shr 24) -band 0xFF)
                $red = [byte](($pixel -shr 16) -band 0xFF)
                $green = [byte](($pixel -shr 8) -band 0xFF)
                $blue = [byte]($pixel -band 0xFF)
                $neutral = $red -eq $green -and $green -eq $blue
                Assert-True $neutral "Brand icon contains a colored pixel at ${size}px (${x}, ${y}): $Path"
                if ($x -eq 0 -or $y -eq 0 -or $x -eq ($size - 1) -or $y -eq ($size - 1)) {
                    $validStroke = $alpha -eq 128 -and $red -eq 0
                    Assert-True $validStroke "Brand icon outer stroke is not 1px black at 50% opacity: $Path"
                } else {
                    Assert-True ($alpha -eq 255) "Brand icon interior is not opaque at ${size}px: $Path"
                    if ($red -eq 0) { $black++ }
                    elseif ($red -eq 255) { $white++ }
                    else { $antialiased++ }
                }
            }
        }
        $glyphValid = $black -gt ($frame.Pixels.Length / 12) -and
                      $black -lt ($frame.Pixels.Length / 2)
        Assert-True $glyphValid "Brand glyph shape is empty or overfilled at ${size}px: $Path"
        $boxValid = $white -gt ($frame.Pixels.Length / 8)
        Assert-True $boxValid "Brand white box or glyph antialiasing is missing at ${size}px: $Path"
        $topLeftInterior = $frame.Pixels[$size + 1]
        $topRightInterior = $frame.Pixels[$size * 2 - 2]
        $squareCorner =
            [byte](($topLeftInterior -shr 24) -band 0xFF) -eq 255 -and
            [byte](($topLeftInterior -shr 16) -band 0xFF) -eq 255 -and
            [byte](($topLeftInterior -shr 8) -band 0xFF) -eq 255 -and
            [byte]($topLeftInterior -band 0xFF) -eq 255 -and
            [byte](($topRightInterior -shr 24) -band 0xFF) -eq 255 -and
            [byte](($topRightInterior -shr 16) -band 0xFF) -eq 255 -and
            [byte](($topRightInterior -shr 8) -band 0xFF) -eq 255 -and
            [byte]($topRightInterior -band 0xFF) -eq 255
        Assert-True $squareCorner "Standard square brand corner is not present at ${size}px: $Path"
    }
}

function Test-ThemeGlyphAsset([string]$Path, [byte]$Channel) {
    $frames = @(Read-IcoFrames $Path)
    $frameSizes = @($frames | ForEach-Object { $_.Size })
    $expectedFrames = ($frameSizes -join ',') -ceq ($expectedSizes -join ',')
    Assert-True $expectedFrames "Unexpected experimental theme icon frames: $Path"
    foreach ($frame in $frames) {
        $size = $frame.Size
        $alpha = [byte[]]::new($size * $size)
        $opaque = 0
        $transparent = 0
        $partial = 0
        for ($y = 0; $y -lt $size; $y++) {
            for ($x = 0; $x -lt $size; $x++) {
                $pixel = $frame.Pixels[$y * $size + $x]
                $a = [byte](($pixel -shr 24) -band 0xFF)
                $red = [byte](($pixel -shr 16) -band 0xFF)
                $green = [byte](($pixel -shr 8) -band 0xFF)
                $blue = [byte]($pixel -band 0xFF)
                $alpha[$y * $size + $x] = $a
                if ($a -eq 0) {
                    $transparent++
                    continue
                }
                $expectedColor = $red -eq $Channel -and $green -eq $Channel -and $blue -eq $Channel
                Assert-True $expectedColor "Unexpected experimental glyph color at ${size}px (${x}, ${y}): $Path"
                $inside = -not ($x -eq 0 -or $y -eq 0 -or $x -eq ($size - 1) -or $y -eq ($size - 1))
                Assert-True $inside "Experimental glyph touches the outer edge at ${size}px: $Path"
                if ($a -eq 255) { $opaque++ } else { $partial++ }
            }
        }
        $glyphDensity = $opaque -gt ($alpha.Length / 12) -and $opaque -lt ($alpha.Length / 2)
        Assert-True $glyphDensity "Empty or filled experimental glyph at ${size}px: $Path"
        $hasEdges = $transparent -gt ($alpha.Length / 3) -and $partial -gt 0
        Assert-True $hasEdges "Missing transparency or antialiasing at ${size}px: $Path"
        [PSCustomObject]@{ Size = $size; Alpha = $alpha }
    }
}

$brandPath = Join-Path $repoRoot 'data\qiqiime.ico'
Test-BrandIconAsset $brandPath

$diagPath = Join-Path $repoRoot 'data\qiqiime-diag.ico'
$diagFrames = @(Read-IcoFrames $diagPath)
$diagSizes = @($diagFrames | ForEach-Object { $_.Size })
Assert-True (($diagSizes -join ',') -ceq ($expectedSizes -join ',')) "Unexpected QiQi IME Diag icon frames: $diagPath"
$diagPng = Join-Path $repoRoot 'data\qiqiime-diag.png'
Assert-True (Test-Path -LiteralPath $diagPng -PathType Leaf) "Missing QiQi IME Diag icon source PNG."

$light = @(Test-ThemeGlyphAsset (Join-Path $repoRoot 'data\qiqiime-light.ico') 0)
$dark = @(Test-ThemeGlyphAsset (Join-Path $repoRoot 'data\qiqiime-dark.ico') 255)
for ($i = 0; $i -lt $expectedSizes.Count; $i++) {
    $sameAlpha = [Convert]::ToBase64String($light[$i].Alpha) -ceq
                 [Convert]::ToBase64String($dark[$i].Alpha)
    Assert-True $sameAlpha "Experimental theme glyph shapes differ at $($expectedSizes[$i])px."
}

Write-Host 'IME icon assets passed: required frames, 32-bit alpha, black/white conformance, exact brand stroke, and unclipped glyph geometry.'
