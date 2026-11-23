# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

Add-Type -AssemblyName System.Drawing

function ConvertFrom-QiQiHostColor {
    param([string] $Value)
    return [Drawing.ColorTranslator]::FromHtml($Value)
}

function New-QiQiHostPackageAssets {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $OutputDirectory,
        [Parameter(Mandatory)]
        [string] $GlyphIconPath,
        [Parameter(Mandatory)]
        [string] $BackgroundColor,
        [Parameter(Mandatory)]
        [string] $ForegroundColor,
        [Parameter(Mandatory)]
        [string] $BadgeColor,
        [Parameter(Mandatory)]
        [string] $BadgeTextColor,
        [Parameter(Mandatory)]
        [string] $BadgeText,
        [Parameter(Mandatory)]
        [ValidateSet('Circle', 'Square')]
        [string] $BadgeShape,
        [Parameter(Mandatory)]
        [string] $HostLabel
    )

    if (-not (Test-Path -LiteralPath $GlyphIconPath -PathType Leaf)) {
        throw "Host icon source does not exist: $GlyphIconPath"
    }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

    $assets = @(
        [pscustomobject]@{
            Name = 'StoreLogo.png'; Width = 50; Height = 50
        },
        [pscustomobject]@{
            Name = 'Square44x44Logo.png'; Width = 44; Height = 44
        },
        [pscustomobject]@{
            Name = 'Square150x150Logo.png'; Width = 150; Height = 150
        },
        [pscustomobject]@{
            Name = 'Wide310x150Logo.png'; Width = 310; Height = 150
        },
        [pscustomobject]@{
            Name = 'SplashScreen.png'; Width = 620; Height = 300
        }
    )

    $background = ConvertFrom-QiQiHostColor $BackgroundColor
    $foreground = ConvertFrom-QiQiHostColor $ForegroundColor
    $badgeBackground = ConvertFrom-QiQiHostColor $BadgeColor
    $badgeForeground = ConvertFrom-QiQiHostColor $BadgeTextColor
    $icon = [Drawing.Icon]::new(
        $GlyphIconPath,
        [Drawing.Size]::new(256, 256))
    try {
        foreach ($asset in $assets) {
            $bitmap = [Drawing.Bitmap]::new($asset.Width, $asset.Height)
            try {
                $graphics = [Drawing.Graphics]::FromImage($bitmap)
                try {
                    $graphics.Clear($background)
                    $graphics.SmoothingMode =
                        [Drawing.Drawing2D.SmoothingMode]::HighQuality
                    $graphics.TextRenderingHint =
                        [Drawing.Text.TextRenderingHint]::AntiAliasGridFit

                    if ($asset.Width -gt $asset.Height * 1.5) {
                        $glyphSide = [int]($asset.Height * 0.62)
                        $glyphX = [int]($asset.Height * 0.16)
                        $glyphY = [int](($asset.Height - $glyphSide) / 2)
                        $graphics.DrawIcon(
                            $icon,
                            [Drawing.Rectangle]::new(
                                $glyphX,
                                $glyphY,
                                $glyphSide,
                                $glyphSide))

                        $textLeft = $glyphX + $glyphSide +
                            [int]($asset.Height * 0.10)
                        $textWidth = $asset.Width - $textLeft -
                            [int]($asset.Height * 0.08)
                        $titleSize = [single]([Math]::Max(
                            13, $asset.Height * 0.17))
                        $labelSize = [single]([Math]::Max(
                            9, $asset.Height * 0.095))
                        $titleFont = [Drawing.Font]::new(
                            'Segoe UI',
                            $titleSize,
                            [Drawing.FontStyle]::Bold,
                            [Drawing.GraphicsUnit]::Pixel)
                        $labelFont = [Drawing.Font]::new(
                            'Segoe UI Semibold',
                            $labelSize,
                            [Drawing.FontStyle]::Regular,
                            [Drawing.GraphicsUnit]::Pixel)
                        $foregroundBrush = [Drawing.SolidBrush]::new($foreground)
                        try {
                            $graphics.DrawString(
                                'QiQiIME',
                                $titleFont,
                                $foregroundBrush,
                                [Drawing.RectangleF]::new(
                                    $textLeft,
                                    [single]($asset.Height * 0.24),
                                    $textWidth,
                                    [single]($asset.Height * 0.28)))
                            $graphics.DrawString(
                                $HostLabel,
                                $labelFont,
                                $foregroundBrush,
                                [Drawing.RectangleF]::new(
                                    $textLeft,
                                    [single]($asset.Height * 0.52),
                                    $textWidth,
                                    [single]($asset.Height * 0.22)))
                        } finally {
                            $foregroundBrush.Dispose()
                            $labelFont.Dispose()
                            $titleFont.Dispose()
                        }
                    } else {
                        $glyphSide = [int]($asset.Width * 0.62)
                        $glyphX = [int](($asset.Width - $glyphSide) / 2)
                        $glyphY = [int](($asset.Height - $glyphSide) * 0.36)
                        $graphics.DrawIcon(
                            $icon,
                            [Drawing.Rectangle]::new(
                                $glyphX,
                                $glyphY,
                                $glyphSide,
                                $glyphSide))

                        $badgeSide = [int]($asset.Width * 0.31)
                        $badgeMargin = [int]([Math]::Max(
                            2, $asset.Width * 0.055))
                        $badgeRect = [Drawing.Rectangle]::new(
                            $asset.Width - $badgeSide - $badgeMargin,
                            $asset.Height - $badgeSide - $badgeMargin,
                            $badgeSide,
                            $badgeSide)
                        $badgeBrush = [Drawing.SolidBrush]::new(
                            $badgeBackground)
                        $badgeTextBrush = [Drawing.SolidBrush]::new(
                            $badgeForeground)
                        $badgeFont = [Drawing.Font]::new(
                            'Segoe UI',
                            [single]($badgeSide * 0.53),
                            [Drawing.FontStyle]::Bold,
                            [Drawing.GraphicsUnit]::Pixel)
                        $format = [Drawing.StringFormat]::new()
                        try {
                            if ($BadgeShape -eq 'Circle') {
                                $graphics.FillEllipse(
                                    $badgeBrush,
                                    $badgeRect)
                            } else {
                                $graphics.FillRectangle(
                                    $badgeBrush,
                                    $badgeRect)
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
                    }

                    $bitmap.Save(
                        (Join-Path $OutputDirectory $asset.Name),
                        [Drawing.Imaging.ImageFormat]::Png)
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
}
