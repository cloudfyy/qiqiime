[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$output = Join-Path (Split-Path -Parent $PSScriptRoot) 'data\qiqiime-monitor.ico'
$sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
$images = [Collections.Generic.List[byte[]]]::new()
$family = [Drawing.FontFamily]::new('Microsoft YaHei UI')
$brush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(35, 105, 200))
try {
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
            $graphics.FillPath($brush, $path)
            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $images.Add($stream.ToArray())
        } finally {
            $stream.Dispose()
            $transform.Dispose()
            $path.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
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
            $writer.Write([uint32]$images[$i].Length)
            $writer.Write($offset)
            $offset += [uint32]$images[$i].Length
        }
        foreach ($image in $images) { $writer.Write($image) }
        $writer.Flush()
        [IO.File]::WriteAllBytes($output, $stream.ToArray())
    } finally {
        $writer.Dispose()
        $stream.Dispose()
    }
} finally {
    $brush.Dispose()
    $family.Dispose()
}
Write-Output "Generated $output (native glyph outlines, 16..256 pixels, no bitmap upscaling)."
