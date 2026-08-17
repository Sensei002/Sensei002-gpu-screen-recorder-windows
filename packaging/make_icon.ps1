# =============================================================================
# make_icon.ps1 — generate the Windows branding assets from the vendored
# upstream logo (ui/images/gpu_screen_recorder_logo.png).
#
# Produces (all committed so packaging is reproducible without this script):
#   packaging/gsr.ico              — multi-resolution icon (16..256) embedded
#                                    in the three executables and the installer
#   packaging/installer_banner.bmp — Inno Setup WizardImageFile (164x314, 24-bit)
#   packaging/installer_banner_small.bmp — Inno WizardSmallImageFile (55x58)
#
# Zero dependencies: plain PowerShell + System.Drawing (present on every
# Windows system and the GitHub-hosted runners). Run:
#   powershell -ExecutionPolicy Bypass -File packaging/make_icon.ps1
# =============================================================================
param(
    [string]$LogoPath  = (Join-Path $PSScriptRoot "..\ui\images\gpu_screen_recorder_logo.png"),
    [string]$OutDir    = $PSScriptRoot
)

Add-Type -AssemblyName System.Drawing

# The app's color theme (ui/include/Theme.hpp): page background rgb(38,43,47),
# accent tint rgb(118,185,0) — the installer banner matches the app itself.
$bgColor   = [System.Drawing.Color]::FromArgb(255, 38, 43, 47)
$accent    = [System.Drawing.Color]::FromArgb(255, 118, 185, 0)

$logo = [System.Drawing.Image]::FromFile((Resolve-Path $LogoPath))

function New-TransparentBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    return $bmp, $g
}

# ---- gsr.ico ----------------------------------------------------------------
# Multi-size icon with PNG-compressed entries (Windows Vista+; the port targets
# Windows 10/11, which render PNG entries at every size). 256 is stored as
# width/height 0 per the ICO format.
$icoSizes = @(16, 24, 32, 48, 64, 128, 256)
$entries = @()   # each: @{ size = N; data = byte[] (PNG) }
foreach ($s in $icoSizes) {
    $bmp, $g = New-TransparentBitmap $s
    $g.DrawImage($logo, 0, 0, $s, $s)
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $entries += ,@{ size = $s; data = $ms.ToArray() }
    $bmp.Dispose()
    $ms.Dispose()
}

$icoPath = Join-Path $OutDir "gsr.ico"
$fs = [System.IO.File]::Create($icoPath)
try {
    $bw = New-Object System.IO.BinaryWriter($fs)
    $bw.Write([UInt16]0)                        # reserved
    $bw.Write([UInt16]1)                        # type: icon
    $bw.Write([UInt16]$entries.Count)           # image count
    $offset = 6 + 16 * $entries.Count
    foreach ($e in $entries) {
        $dim = if ($e.size -ge 256) { 0 } else { $e.size }
        $bw.Write([Byte]$dim)                   # width (0 = 256)
        $bw.Write([Byte]$dim)                   # height (0 = 256)
        $bw.Write([Byte]0)                      # palette count
        $bw.Write([Byte]0)                      # reserved
        $bw.Write([UInt16]1)                    # color planes
        $bw.Write([UInt16]32)                   # bits per pixel
        $bw.Write([UInt32]$e.data.Length)       # bytes in resource
        $bw.Write([UInt32]$offset)              # image data offset
        $offset += $e.data.Length
    }
    foreach ($e in $entries) { $bw.Write($e.data) }
    $bw.Flush()
} finally {
    $fs.Close()
}
Write-Host "Wrote $icoPath ($($entries.Count) sizes)"

# ---- installer banner (Inno Setup WizardImageFile, 164x314, 24-bit) ---------
# Logo centered on the app's page background color, with the accent-green
# underline to echo the app's branding.
function New-BannerBitmap([int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear($bgColor)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    return $bmp, $g
}

# Big banner: logo at ~60% width, accent bar under it.
$bannerPath = Join-Path $OutDir "installer_banner.bmp"
$bmp, $g = New-BannerBitmap 164 314
$logoW = [int](164 * 0.62)
$logoH = [int]($logoW * ($logo.Height / $logo.Width))
$logoX = [int](($bmp.Width - $logoW) / 2)
$logoY = 96
$g.DrawImage($logo, $logoX, $logoY, $logoW, $logoH)
$barW = [int]($bmp.Width * 0.5)
$barX = [int](($bmp.Width - $barW) / 2)
$barY = $logoY + $logoH + 28
$barBrush = New-Object System.Drawing.SolidBrush($accent)
$g.FillRectangle($barBrush, $barX, $barY, $barW, 4)
$g.Dispose()
$bmp.Save($bannerPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
$bmp.Dispose()
Write-Host "Wrote $bannerPath"

# Small banner (top-right of the wizard pages, 55x58).
$smallPath = Join-Path $OutDir "installer_banner_small.bmp"
$bmp, $g = New-BannerBitmap 55 58
$sw = 40
$sh = [int]($sw * ($logo.Height / $logo.Width))
$g.DrawImage($logo, [int](($bmp.Width - $sw) / 2), [int](($bmp.Height - $sh) / 2), $sw, $sh)
$g.Dispose()
$bmp.Save($smallPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
$bmp.Dispose()
Write-Host "Wrote $smallPath"

$logo.Dispose()
Write-Host "Done."
