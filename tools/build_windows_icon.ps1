param(
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [string]$PreviewPath = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$output = [System.IO.Path]::GetFullPath($OutputPath)
$outputDir = [System.IO.Path]::GetDirectoryName($output)
[System.IO.Directory]::CreateDirectory($outputDir) | Out-Null
$keepPreview = -not [string]::IsNullOrWhiteSpace($PreviewPath)
$png = if ($keepPreview) {
    [System.IO.Path]::GetFullPath($PreviewPath)
} else {
    [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), [guid]::NewGuid().ToString() + '.png')
}

$bitmap = [System.Drawing.Bitmap]::new(512, 512, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$background = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 11, 17, 21))
$baselinePen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 29, 48, 56), 24)
$chartPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 0, 229, 139), 28)
$endpointBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 0, 229, 139))
$rounded = [System.Drawing.Drawing2D.GraphicsPath]::new()
$points = [System.Drawing.Point[]]@(
    [System.Drawing.Point]::new(96, 337),
    [System.Drawing.Point]::new(173, 246),
    [System.Drawing.Point]::new(235, 289),
    [System.Drawing.Point]::new(330, 159),
    [System.Drawing.Point]::new(415, 213)
)

try {
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $rounded.AddArc(0, 0, 224, 224, 180, 90)
    $rounded.AddArc(288, 0, 224, 224, 270, 90)
    $rounded.AddArc(288, 288, 224, 224, 0, 90)
    $rounded.AddArc(0, 288, 224, 224, 90, 90)
    $rounded.CloseFigure()
    $graphics.FillPath($background, $rounded)

    $baselinePen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $baselinePen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $graphics.DrawLine($baselinePen, 86, 342, 426, 342)

    $chartPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $chartPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $chartPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $graphics.DrawLines($chartPen, $points)
    $graphics.FillEllipse($endpointBrush, 391, 189, 48, 48)

    $bitmap.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    $image = [System.IO.File]::ReadAllBytes($png)
    $stream = [System.IO.File]::Create($output)
    try {
        $writer = [System.IO.BinaryWriter]::new($stream)
        $writer.Write([UInt16]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]1)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]$image.Length)
        $writer.Write([UInt32]22)
        $writer.Write($image)
        $writer.Dispose()
    } finally {
        $stream.Dispose()
    }
    Write-Host "Windows icon created: $output"
} finally {
    $rounded.Dispose()
    $endpointBrush.Dispose()
    $chartPen.Dispose()
    $baselinePen.Dispose()
    $background.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
    if (-not $keepPreview -and (Test-Path -LiteralPath $png)) {
        Remove-Item -LiteralPath $png
    }
}
