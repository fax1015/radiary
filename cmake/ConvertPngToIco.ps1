param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$resolvedInputPath = (Resolve-Path -LiteralPath $InputPath).Path
$outputDirectory = Split-Path -Path $OutputPath -Parent
if ($outputDirectory) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$iconSizes = @(16, 24, 32, 48, 64, 128, 256)
$sourceBitmap = [System.Drawing.Bitmap]::new($resolvedInputPath)

try {
    $iconFrames = New-Object System.Collections.Generic.List[object]

    foreach ($size in $iconSizes) {
        $resizedBitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($resizedBitmap)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.DrawImage($sourceBitmap, 0, 0, $size, $size)
            } finally {
                $graphics.Dispose()
            }

            $pngStream = New-Object System.IO.MemoryStream
            try {
                $resizedBitmap.Save($pngStream, [System.Drawing.Imaging.ImageFormat]::Png)
                $iconFrames.Add([PSCustomObject]@{
                    Size = $size
                    Bytes = $pngStream.ToArray()
                }) | Out-Null
            } finally {
                $pngStream.Dispose()
            }
        } finally {
            $resizedBitmap.Dispose()
        }
    }

    $fileStream = [System.IO.File]::Open($OutputPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    try {
        $writer = New-Object System.IO.BinaryWriter($fileStream)
        try {
            $writer.Write([UInt16]0)
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]$iconFrames.Count)

            $imageOffset = 6 + (16 * $iconFrames.Count)
            foreach ($frame in $iconFrames) {
                $dimensionByte = if ($frame.Size -ge 256) { 0 } else { [byte]$frame.Size }
                $writer.Write([byte]$dimensionByte)
                $writer.Write([byte]$dimensionByte)
                $writer.Write([byte]0)
                $writer.Write([byte]0)
                $writer.Write([UInt16]1)
                $writer.Write([UInt16]32)
                $writer.Write([UInt32]$frame.Bytes.Length)
                $writer.Write([UInt32]$imageOffset)
                $imageOffset += $frame.Bytes.Length
            }

            foreach ($frame in $iconFrames) {
                $writer.Write($frame.Bytes)
            }
        } finally {
            $writer.Dispose()
        }
    } finally {
        $fileStream.Dispose()
    }
} finally {
    $sourceBitmap.Dispose()
}
