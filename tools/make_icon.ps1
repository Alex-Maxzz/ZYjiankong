# Generate TaskbarStudio application icon
# Design: dark rounded background + blue rising data line + bottom taskbar bar
Add-Type -AssemblyName System.Drawing

$out = Join-Path $PWD "src\app.ico"
$sizes = @(256, 48, 32, 16)
$pngList = @()

foreach ($sz in $sizes) {
    $size = [int]$sz
    $bmp = [System.Drawing.Bitmap]::new($size, $size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode    = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode  = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.InterpolationMode= [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.Clear([System.Drawing.Color]::Transparent)

    # Rounded rectangle background
    $radius = [int]([math]::Max(2, [int]($size * 0.18)))
    $rectW = $size - 1
    $rectH = $size - 1
    $rect = [System.Drawing.Rectangle]::new(0, 0, $rectW, $rectH)
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $d = $radius * 2
    $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
    $path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
    $path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
    $path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    $bgColor = [System.Drawing.Color]::FromArgb(255, 26, 28, 36)
    $bgBrush = [System.Drawing.SolidBrush]::new($bgColor)
    $g.FillPath($bgBrush, $path)

    # Border
    if ($size -ge 32) {
        $borderW = [math]::Max(1.0, $size * 0.025)
        $borderPen = [System.Drawing.Pen]::new(
            [System.Drawing.Color]::FromArgb(255, 74, 144, 226), [float]$borderW)
        $g.DrawPath($borderPen, $path)
    }

    # Blue rising line
    $accent = [System.Drawing.Color]::FromArgb(255, 74, 144, 226)
    $lineW = [float][math]::Max(1.5, $size * 0.07)
    $pen = [System.Drawing.Pen]::new($accent, $lineW)
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round

    $p1 = [System.Drawing.PointF]::new([float]($size * 0.18), [float]($size * 0.58))
    $p2 = [System.Drawing.PointF]::new([float]($size * 0.40), [float]($size * 0.45))
    $p3 = [System.Drawing.PointF]::new([float]($size * 0.60), [float]($size * 0.50))
    $p4 = [System.Drawing.PointF]::new([float]($size * 0.82), [float]($size * 0.28))
    $pts = [System.Drawing.PointF[]]@($p1, $p2, $p3, $p4)
    $g.DrawLines($pen, $pts)

    # Data points
    if ($size -ge 32) {
        $dotR = [float][math]::Max(1.5, $size * 0.045)
        $dotBrush = [System.Drawing.SolidBrush]::new($accent)
        foreach ($p in $pts) {
            $g.FillEllipse($dotBrush, $p.X - $dotR, $p.Y - $dotR, $dotR * 2, $dotR * 2)
        }
    }

    # Bottom taskbar bar
    $barH = [float][math]::Max(1.0, $size * 0.08)
    $barY = [float]($size * 0.78)
    $barX = [float]($size * 0.18)
    $barW = [float]($size * 0.64)
    $barBrush = [System.Drawing.SolidBrush]::new(
        [System.Drawing.Color]::FromArgb(255, 100, 170, 255))
    $barRect = [System.Drawing.RectangleF]::new($barX, $barY, $barW, $barH)
    $g.FillRectangle($barBrush, $barRect)

    $g.Dispose()

    $pngMs = [System.IO.MemoryStream]::new()
    $bmp.Save($pngMs, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngList += ,$pngMs.ToArray()
    $bmp.Dispose()
}

# Build ICO file
$ms = [System.IO.MemoryStream]::new()
$bw = [System.IO.BinaryWriter]::new($ms)

# ICONDIR
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$sizes.Count)

# Data offset
$dataOffset = 6 + 16 * $sizes.Count

# ICONDIRENTRY
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = [int]$sizes[$i]
    $data = $pngList[$i]
    if ($s -ge 256) {
        $bw.Write([Byte]0)
        $bw.Write([Byte]0)
    } else {
        $bw.Write([Byte]$s)
        $bw.Write([Byte]$s)
    }
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$data.Length)
    $bw.Write([UInt32]$dataOffset)
    $dataOffset = $dataOffset + $data.Length
}

# Write PNG data
foreach ($data in $pngList) {
    $bw.Write($data)
}

# Save to file
$bytes = $ms.ToArray()
[System.IO.File]::WriteAllBytes($out, $bytes)

$bw.Dispose()
$ms.Dispose()

$info = Get-Item -LiteralPath $out
Write-Output ("Generated: " + $info.FullName)
Write-Output ("Size: " + $info.Length + " bytes")
