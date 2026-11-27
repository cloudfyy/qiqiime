# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param([switch]$ImeOnly, [switch]$SettingsOnly)

$ErrorActionPreference = 'Stop'
if ($ImeOnly -and $SettingsOnly) { throw 'Choose either -ImeOnly or -SettingsOnly.' }
Add-Type -AssemblyName System.Drawing
$repoRoot = Split-Path -Parent $PSScriptRoot
$sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)

function Write-QiQiIcoFile([string]$OutputPath, [Collections.Generic.List[byte[]]]$Frames) {
    $stream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$sizes.Count)
        $offset = [uint32](6 + 16 * $sizes.Count)
        for ($i = 0; $i -lt $sizes.Count; $i++) {
            $dimension = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
            $writer.Write([byte]$dimension)
            $writer.Write([byte]$dimension)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$Frames[$i].Length)
            $writer.Write($offset)
            $offset += [uint32]$Frames[$i].Length
        }
        foreach ($image in $Frames) { $writer.Write($image) }
        $writer.Flush()
        [IO.File]::WriteAllBytes($OutputPath, $stream.ToArray())
    } finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

function Add-QiQiToolCard([Drawing.Drawing2D.GraphicsPath]$Path, [single]$Pad, [single]$Extent, [single]$Diameter) {
    $far = $Pad + $Extent - $Diameter
    $Path.AddArc($Pad, $Pad, $Diameter, $Diameter, 180, 90)
    $Path.AddArc($far, $Pad, $Diameter, $Diameter, 270, 90)
    $Path.AddArc($far, $far, $Diameter, $Diameter, 0, 90)
    $Path.AddArc($Pad, $far, $Diameter, $Diameter, 90, 90)
    $Path.CloseFigure()
}

function Write-QiQiSettingsIcon {
    $frames = [Collections.Generic.List[byte[]]]::new()
    $radii = @(0.245, 0.325, 0.325, 0.245)
    $offsets = @(-0.42, -0.23, 0.23, 0.42)
    foreach ($size in $sizes) {
        $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $card = [Drawing.Drawing2D.GraphicsPath]::new()
        $gear = [Drawing.Drawing2D.GraphicsPath]::new([Drawing.Drawing2D.FillMode]::Alternate)
        $start = [Drawing.PointF]::new(0, 0)
        $end = [Drawing.PointF]::new($size, $size)
        $background = [Drawing.Drawing2D.LinearGradientBrush]::new($start, $end,
            [Drawing.Color]::FromArgb(25, 54, 104), [Drawing.Color]::FromArgb(18, 29, 48))
        $accent = [Drawing.Drawing2D.LinearGradientBrush]::new($start, $end,
            [Drawing.Color]::FromArgb(67, 226, 247), [Drawing.Color]::FromArgb(46, 213, 182))
        $border = [Drawing.Pen]::new([Drawing.Color]::FromArgb(58, 134, 232),
            [single][math]::Max(1.0, $size * 0.055))
        $stream = [IO.MemoryStream]::new()
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $pad = [single]($size * 0.06)
            Add-QiQiToolCard $card $pad ($size - 2 * $pad) ($size * 0.36)
            $graphics.FillPath($background, $card)
            $graphics.DrawPath($border, $card)

            $center = $size * 0.5
            $points = [Drawing.PointF[]]@(for ($tooth = 0; $tooth -lt 8; $tooth++) {
                for ($i = 0; $i -lt $radii.Length; $i++) {
                    $angle = ($tooth + $offsets[$i]) * [math]::PI / 4 - [math]::PI / 2
                    $radius = $size * $radii[$i]
                    [Drawing.PointF]::new(($center + $radius * [math]::Cos($angle)),
                        ($center + $radius * [math]::Sin($angle)))
                }
            })
            $gear.AddPolygon($points)
            $gear.AddEllipse([single]($size * 0.375), [single]($size * 0.375),
                [single]($size * 0.25), [single]($size * 0.25))
            $graphics.FillPath($accent, $gear)
            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
        } finally {
            $stream.Dispose()
            $border.Dispose()
            $accent.Dispose()
            $background.Dispose()
            $gear.Dispose()
            $card.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
    $output = Join-Path $repoRoot 'data\qiqiime-settings.ico'
    Write-QiQiIcoFile -OutputPath $output -Frames $frames
    Write-Output "Generated $output (cyan gear on navy, 16..256 pixels)."
}

if ($SettingsOnly) {
    Write-QiQiSettingsIcon
    return
}

$family = [Drawing.FontFamily]::new('Microsoft YaHei UI')
$brandBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(35, 105, 200))
$monitorBgBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(24, 34, 48))
$monitorHeaderBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(35, 105, 200))
try {
    # Keep the registered brand icon separate from the theme-specific assets.
    $productFrames = [Collections.Generic.List[byte[]]]::new()
    $imeFrames = [Collections.Generic.List[byte[]]]::new()
    $themeIcons = @(
        @{ File = 'qiqiime-light.ico'; Channel = 0; Frames = [Collections.Generic.List[byte[]]]::new() },
        @{ File = 'qiqiime-dark.ico'; Channel = 255; Frames = [Collections.Generic.List[byte[]]]::new() }
    )
    foreach ($size in $sizes) {
        $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $path = [Drawing.Drawing2D.GraphicsPath]::new()
        $transform = [Drawing.Drawing2D.Matrix]::new()
        $stream = [IO.MemoryStream]::new()
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $path.AddString([string][char]0x5947, $family, [int][Drawing.FontStyle]::Bold,
                [single]$size, [Drawing.PointF]::new(0, 0), [Drawing.StringFormat]::GenericTypographic)
            $bounds = $path.GetBounds()
            $scale = [single](0.90 * $size / [math]::Max($bounds.Width, $bounds.Height))
            $transform.Scale($scale, $scale)
            $path.Transform($transform)
            $transform.Reset()
            $bounds = $path.GetBounds()
            $transform.Translate([single](($size - $bounds.Width) / 2 - $bounds.X),
                [single](($size - $bounds.Height) / 2 - $bounds.Y))
            $path.Transform($transform)
            $graphics.FillPath($brandBrush, $path)
            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $productFrames.Add($stream.ToArray())

            $transform.Reset()
            $transform.Translate([single](-$size / 2), [single](-$size / 2))
            $transform.Scale([single](0.80 / 0.90), [single](0.80 / 0.90),
                [Drawing.Drawing2D.MatrixOrder]::Append)
            $transform.Translate([single]($size / 2), [single]($size / 2),
                [Drawing.Drawing2D.MatrixOrder]::Append)
            $path.Transform($transform)
            $graphics.Clear([Drawing.Color]::FromArgb(128, 0, 0, 0))
            # Pixel-align the one-pixel frame; antialias only the glyph.
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::None
            $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::None
            $graphics.FillRectangle([Drawing.Brushes]::White, 1, 1, ($size - 2), ($size - 2))
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.FillPath([Drawing.Brushes]::Black, $path)
            $stream.SetLength(0)
            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $imeFrames.Add($stream.ToArray())
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.FillPath([Drawing.Brushes]::Black, $path)
            $graphics.Flush([Drawing.Drawing2D.FlushIntention]::Sync)
            # Share coverage and write exact channels to avoid GDI+ white-edge rounding.
            foreach ($themeIcon in $themeIcons) {
                for ($y = 0; $y -lt $size; $y++) {
                    for ($x = 0; $x -lt $size; $x++) {
                        $pixel = $bitmap.GetPixel($x, $y)
                        if ($pixel.A -gt 0 -and ($pixel.R -ne $themeIcon.Channel -or
                            $pixel.G -ne $themeIcon.Channel -or $pixel.B -ne $themeIcon.Channel)) {
                            $bitmap.SetPixel($x, $y, [Drawing.Color]::FromArgb(
                                $pixel.A, $themeIcon.Channel, $themeIcon.Channel, $themeIcon.Channel))
                        }
                    }
                }
                $stream.SetLength(0)
                $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
                $themeIcon.Frames.Add($stream.ToArray())
            }
        } finally {
            $stream.Dispose()
            $transform.Dispose()
            $path.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
    $imeOutput = Join-Path $repoRoot 'data\qiqiime.ico'
    Write-QiQiIcoFile -OutputPath $imeOutput -Frames $imeFrames
    Write-Output "Generated $imeOutput (white box, black glyph, 1px half-opaque black frame, 16..256 pixels)."
    foreach ($themeIcon in $themeIcons) {
        $themeOutput = Join-Path (Join-Path $repoRoot 'data') $themeIcon.File
        Write-QiQiIcoFile -OutputPath $themeOutput -Frames $themeIcon.Frames
        Write-Output "Generated $themeOutput (transparent theme glyph without outline, 16..256 pixels)."
    }
    if ($ImeOnly) { return }

    $productOutput = Join-Path $repoRoot 'data\qiqiime-product.ico'
    Write-QiQiIcoFile -OutputPath $productOutput -Frames $productFrames
    Write-Output "Generated $productOutput (native '奇' brand glyph outlines, 16..256 pixels)."
    Write-QiQiSettingsIcon

    # 2. Log Monitor Icon (data\qiqiime-monitor.ico): Dark diagnostic screen with teal pulse waveform & blue header (16..256 px)
    $monitorFrames = [Collections.Generic.List[byte[]]]::new()
    foreach ($size in $sizes) {
        $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $cardPath = [Drawing.Drawing2D.GraphicsPath]::new()
        $headerPath = [Drawing.Drawing2D.GraphicsPath]::new()
        $wavePath = [Drawing.Drawing2D.GraphicsPath]::new()
        $borderPen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(58, 134, 232), [single][math]::Max(1.0, $size * 0.055))
        $wavePen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(46, 213, 182), [single][math]::Max(1.5, $size * 0.085))
        $wavePen.StartCap = [Drawing.Drawing2D.LineCap]::Round
        $wavePen.EndCap = [Drawing.Drawing2D.LineCap]::Round
        $wavePen.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
        $stream = [IO.MemoryStream]::new()
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality

            $pad = [single]($size * 0.06)
            $w = [single]($size - 2 * $pad)
            $h = [single]($size - 2 * $pad)
            $r = [single]($size * 0.18)
            $d = [single](2 * $r)

            Add-QiQiToolCard $cardPath $pad $w $d
            $graphics.FillPath($monitorBgBrush, $cardPath)

            $headerH = [single]($size * 0.24)
            $headerPath.AddArc($pad, $pad, $d, $d, 180, 90)
            $headerPath.AddArc($pad + $w - $d, $pad, $d, $d, 270, 90)
            $headerPath.AddLine($pad + $w, $pad + $headerH, $pad, $pad + $headerH)
            $headerPath.CloseFigure()
            $graphics.FillPath($monitorHeaderBrush, $headerPath)
            $graphics.DrawPath($borderPen, $cardPath)

            $pts = [Drawing.PointF[]]@(
                [Drawing.PointF]::new([single]($size * 0.17), [single]($size * 0.64)),
                [Drawing.PointF]::new([single]($size * 0.33), [single]($size * 0.64)),
                [Drawing.PointF]::new([single]($size * 0.43), [single]($size * 0.41)),
                [Drawing.PointF]::new([single]($size * 0.56), [single]($size * 0.80)),
                [Drawing.PointF]::new([single]($size * 0.67), [single]($size * 0.54)),
                [Drawing.PointF]::new([single]($size * 0.83), [single]($size * 0.54))
            )
            $wavePath.AddLines($pts)
            $graphics.DrawPath($wavePen, $wavePath)

            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $monitorFrames.Add($stream.ToArray())
        } finally {
            $stream.Dispose()
            $wavePen.Dispose()
            $borderPen.Dispose()
            $wavePath.Dispose()
            $headerPath.Dispose()
            $cardPath.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
    $monitorOutput = Join-Path $repoRoot 'data\qiqiime-monitor.ico'
    Write-QiQiIcoFile -OutputPath $monitorOutput -Frames $monitorFrames
    Write-Output "Generated $monitorOutput (real-time diagnostic waveform icon, 16..256 pixels)."
} finally {
    $monitorHeaderBrush.Dispose()
    $monitorBgBrush.Dispose()
    $brandBrush.Dispose()
    $family.Dispose()
}
