# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $OutputPath,
    [Parameter(Mandatory)]
    [string] $GlyphIconPath,
    [Parameter(Mandatory)]
    [string] $BackgroundColor,
    [Parameter(Mandatory)]
    [string] $BadgeColor,
    [Parameter(Mandatory)]
    [string] $BadgeTextColor,
    [Parameter(Mandatory)]
    [string] $BadgeText,
    [Parameter(Mandatory)]
    [ValidateSet('Circle', 'Square')]
    [string] $BadgeShape
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'Generate-HostAssets.ps1')

if (-not (Test-Path -LiteralPath $GlyphIconPath -PathType Leaf)) {
    throw "Host icon source does not exist: $GlyphIconPath"
}
$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$background = ConvertFrom-QiQiHostColor $BackgroundColor
$badgeBackground = ConvertFrom-QiQiHostColor $BadgeColor
$badgeForeground = ConvertFrom-QiQiHostColor $BadgeTextColor
$icon = [Drawing.Icon]::new(
    $GlyphIconPath,
    [Drawing.Size]::new(256, 256))
$frames = [Collections.Generic.List[object]]::new()
try {
    foreach ($size in @(16, 24, 32, 48, 64, 128, 256)) {
        $bitmap = [Drawing.Bitmap]::new(
            $size,
            $size,
            [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear($background)
                $graphics.SmoothingMode =
                    [Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.TextRenderingHint =
                    [Drawing.Text.TextRenderingHint]::AntiAliasGridFit

                $glyphSide = [int][Math]::Max(1, $size * 0.66)
                $glyphRect = [Drawing.Rectangle]::new(
                    [int](($size - $glyphSide) / 2),
                    [int](($size - $glyphSide) / 2),
                    $glyphSide,
                    $glyphSide)
                $graphics.DrawIcon($icon, $glyphRect)

                $badgeSide = [int][Math]::Max(6, $size * 0.34)
                $badgeMargin = [int][Math]::Max(1, $size * 0.04)
                $badgeRect = [Drawing.Rectangle]::new(
                    $size - $badgeSide - $badgeMargin,
                    $size - $badgeSide - $badgeMargin,
                    $badgeSide,
                    $badgeSide)
                $badgeBrush = [Drawing.SolidBrush]::new($badgeBackground)
                $badgeTextBrush = [Drawing.SolidBrush]::new($badgeForeground)
                $badgeFont = [Drawing.Font]::new(
                    'Segoe UI',
                    [single][Math]::Max(4, $badgeSide * 0.53),
                    [Drawing.FontStyle]::Bold,
                    [Drawing.GraphicsUnit]::Pixel)
                $format = [Drawing.StringFormat]::new()
                try {
                    if ($BadgeShape -eq 'Circle') {
                        $graphics.FillEllipse($badgeBrush, $badgeRect)
                    } else {
                        $graphics.FillRectangle($badgeBrush, $badgeRect)
                    }
                    $format.Alignment =
                        [Drawing.StringAlignment]::Center
                    $format.LineAlignment =
                        [Drawing.StringAlignment]::Center
                    $graphics.DrawString(
                        $BadgeText,
                        $badgeFont,
                        $badgeTextBrush,
                        [Drawing.RectangleF]::new(
                            $badgeRect.X,
                            $badgeRect.Y,
                            $badgeRect.Width,
                            $badgeRect.Height),
                        $format)
                } finally {
                    $format.Dispose()
                    $badgeFont.Dispose()
                    $badgeTextBrush.Dispose()
                    $badgeBrush.Dispose()
                }

                $memory = [IO.MemoryStream]::new()
                try {
                    $bitmap.Save(
                        $memory,
                        [Drawing.Imaging.ImageFormat]::Png)
                    $frames.Add([pscustomobject]@{
                        Size = $size
                        Bytes = $memory.ToArray()
                    })
                } finally {
                    $memory.Dispose()
                }
            } finally {
                $graphics.Dispose()
            }
        } finally {
            $bitmap.Dispose()
        }
    }
} finally {
    $icon.Dispose()
}

$stream = [IO.FileStream]::new(
    $OutputPath,
    [IO.FileMode]::Create,
    [IO.FileAccess]::Write,
    [IO.FileShare]::None)
try {
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$frames.Count)
        $offset = 6 + 16 * $frames.Count
        foreach ($frame in $frames) {
            $dimension = if ($frame.Size -eq 256) {
                [byte]0
            } else {
                [byte]$frame.Size
            }
            $writer.Write($dimension)
            $writer.Write($dimension)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$frame.Bytes.Length)
            $writer.Write([uint32]$offset)
            $offset += $frame.Bytes.Length
        }
        foreach ($frame in $frames) {
            $writer.Write($frame.Bytes)
        }
    } finally {
        $writer.Dispose()
    }
} finally {
    $stream.Dispose()
}
