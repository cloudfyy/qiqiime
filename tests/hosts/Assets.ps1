# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

Add-Type -AssemblyName System.Drawing

function New-QiQiHostPackageAssets {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $OutputDirectory,
        [Parameter(Mandatory)]
        [string] $IconPath
    )

    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $assets = @(
        @{ Name = 'store.png'; Width = 50; Height = 50 },
        @{ Name = 'small.png'; Width = 44; Height = 44 },
        @{ Name = 'tile.png'; Width = 150; Height = 150 },
        @{ Name = 'wide.png'; Width = 310; Height = 150 },
        @{ Name = 'splash.png'; Width = 620; Height = 300 }
    )
    $icon = [Drawing.Icon]::new($IconPath, [Drawing.Size]::new(256, 256))
    try {
        foreach ($asset in $assets) {
            $bitmap = [Drawing.Bitmap]::new($asset.Width, $asset.Height)
            try {
                $graphics = [Drawing.Graphics]::FromImage($bitmap)
                try {
                    $graphics.Clear([Drawing.Color]::Transparent)
                    $side = [int]($asset.Height * 0.8)
                    $graphics.DrawIcon($icon, [Drawing.Rectangle]::new(
                        [int](($asset.Width - $side) / 2), [int](($asset.Height - $side) / 2), $side, $side))
                    $bitmap.Save((Join-Path $OutputDirectory $asset.Name), [Drawing.Imaging.ImageFormat]::Png)
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
