# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $OutputPath,
    [Parameter(Mandatory)]
    [ValidateSet('zero', 'com', 'reg', 'full', 'group', 'pack')]
    [string] $Kind
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing

$navy = [Drawing.Color]::FromArgb(18, 38, 69)
$cyan = [Drawing.Color]::FromArgb(83, 229, 232)
$frames = [Collections.Generic.List[object]]::new()
foreach ($size in 16, 24, 32, 48, 64, 128, 256) {
    $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $tile = [Drawing.Drawing2D.GraphicsPath]::new()
    $base = [Drawing.SolidBrush]::new($navy)
    $mark = [Drawing.SolidBrush]::new($cyan)
    $line = [Drawing.Pen]::new($cyan, 6)
    $bytes = [IO.MemoryStream]::new()
    try {
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.ScaleTransform([single]($size / 100), [single]($size / 100))
        $tile.AddArc(2, 2, 36, 36, 180, 90)
        $tile.AddArc(62, 2, 36, 36, 270, 90)
        $tile.AddArc(62, 62, 36, 36, 0, 90)
        $tile.AddArc(2, 62, 36, 36, 90, 90)
        $tile.CloseFigure()
        $graphics.FillPath($base, $tile)
        $line.StartCap = [Drawing.Drawing2D.LineCap]::Round
        $line.EndCap = [Drawing.Drawing2D.LineCap]::Round
        $line.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
        switch ($Kind) {
            'zero' {
                $graphics.DrawArc($line, 35, 22, 30, 32, 180, 180)
                $graphics.FillRectangle($mark, 27, 44, 46, 31)
                $graphics.FillEllipse($base, 46, 53, 8, 8)
                $graphics.FillRectangle($base, 48, 58, 4, 8)
            }
            'com' {
                $graphics.DrawLine($line, 39, 43, 61, 58)
                $graphics.DrawEllipse($line, 22, 23, 23, 23)
                $graphics.DrawEllipse($line, 55, 55, 23, 23)
            }
            'reg' {
                $graphics.FillRectangle($mark, 40, 23, 20, 17)
                $graphics.DrawLine($line, 50, 39, 50, 52)
                $graphics.DrawLine($line, 28, 52, 72, 52)
                $graphics.DrawLine($line, 28, 52, 28, 63)
                $graphics.DrawLine($line, 72, 52, 72, 63)
                $graphics.FillRectangle($mark, 19, 62, 18, 15)
                $graphics.FillRectangle($mark, 63, 62, 18, 15)
            }
            'full' {
                $graphics.DrawRectangle($line, 22, 25, 56, 49)
                $graphics.DrawLine($line, 22, 37, 78, 37)
                $graphics.DrawLine($line, 39, 55, 47, 63)
                $graphics.DrawLine($line, 47, 63, 62, 48)
            }
            'group' {
                $graphics.DrawRectangle($line, 22, 26, 34, 33)
                $graphics.FillRectangle($base, 43, 41, 35, 33)
                $graphics.DrawRectangle($line, 43, 41, 35, 33)
            }
            'pack' {
                $points = [Drawing.Point[]]@(
                    [Drawing.Point]::new(50, 21), [Drawing.Point]::new(77, 36),
                    [Drawing.Point]::new(77, 65), [Drawing.Point]::new(50, 80),
                    [Drawing.Point]::new(23, 65), [Drawing.Point]::new(23, 36))
                $graphics.DrawPolygon($line, $points)
                $graphics.DrawLine($line, 23, 36, 50, 51)
                $graphics.DrawLine($line, 77, 36, 50, 51)
                $graphics.DrawLine($line, 50, 51, 50, 80)
            }
        }
        $bitmap.Save($bytes, [Drawing.Imaging.ImageFormat]::Png)
        $frames.Add([pscustomobject]@{ Size = $size; Bytes = $bytes.ToArray() })
    } finally {
        $bytes.Dispose()
        $line.Dispose()
        $mark.Dispose()
        $base.Dispose()
        $tile.Dispose()
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$writer = [IO.BinaryWriter]::new([IO.File]::Create($OutputPath))
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$frames.Count)
    $offset = 6 + 16 * $frames.Count
    foreach ($frame in $frames) {
        $dimension = [byte]($frame.Size % 256)
        $writer.Write($dimension)
        $writer.Write($dimension)
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$frame.Bytes.Length)
        $writer.Write([uint32]$offset)
        $offset += $frame.Bytes.Length
    }
    foreach ($frame in $frames) { $writer.Write([byte[]]$frame.Bytes) }
} finally {
    $writer.Dispose()
}
