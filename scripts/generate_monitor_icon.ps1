[CmdletBinding()]
param([switch]$ImeOnly)

$ErrorActionPreference = 'Stop'
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

$family = [Drawing.FontFamily]::new('Microsoft YaHei UI')
$brandBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(35, 105, 200))
$monitorBgBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(24, 34, 48))
$monitorHeaderBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(35, 105, 200))
try {
    # IME branding stays legible on both taskbar themes without changing the registered icon.
    $settingsFrames = [Collections.Generic.List[byte[]]]::new()
    $imeFrames = [Collections.Generic.List[byte[]]]::new()
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
            $settingsFrames.Add($stream.ToArray())

            $graphics.Clear([Drawing.Color]::Transparent)
            $border = [Drawing.Pen]::new([Drawing.Color]::FromArgb(128, 0, 0, 0),
                [single](2 * [math]::Max(1, $size / 32)))
            try {
                $border.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
                $transform.Reset()
                $transform.Translate([single](-$size / 2), [single](-$size / 2))
                $transform.Scale([single](0.80 / 0.90), [single](0.80 / 0.90),
                    [Drawing.Drawing2D.MatrixOrder]::Append)
                $transform.Translate([single]($size / 2), [single]($size / 2),
                    [Drawing.Drawing2D.MatrixOrder]::Append)
                $path.Transform($transform)
                # Fill after stroking so only the outer half of the black outline remains.
                $graphics.DrawPath($border, $path)
                $graphics.FillPath([Drawing.Brushes]::White, $path)
            } finally {
                $border.Dispose()
            }
            $stream.SetLength(0)
            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $imeFrames.Add($stream.ToArray())
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
    Write-Output "Generated $imeOutput (transparent white glyph with black outline, 16..256 pixels)."
    if ($ImeOnly) { return }

    $settingsOutput = Join-Path $repoRoot 'data\qiqiime-settings.ico'
    Write-QiQiIcoFile -OutputPath $settingsOutput -Frames $settingsFrames
    Write-Output "Generated $settingsOutput (native '奇' brand glyph outlines, 16..256 pixels)."

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

            $cardPath.AddArc($pad, $pad, $d, $d, 180, 90)
            $cardPath.AddArc($pad + $w - $d, $pad, $d, $d, 270, 90)
            $cardPath.AddArc($pad + $w - $d, $pad + $h - $d, $d, $d, 0, 90)
            $cardPath.AddArc($pad, $pad + $h - $d, $d, $d, 90, 90)
            $cardPath.CloseFigure()
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
