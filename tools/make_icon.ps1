<#
  Baut app.ico aus assets\logo.png (das Zeichen ohne den Schriftzug darunter).

  Die Datei wird von app.rc eingebettet und ist damit Datei-Symbol, Fenstersymbol und das
  Symbol in der Taskleiste. Groessen: 16 (klassisch), 24/32/48/64 (Skalierung), 256 (Explorer).

  Aufruf:  tools\make_icon.ps1                 - nimmt die gespeicherten Randmassen
           tools\make_icon.ps1 -AutoTrim       - sucht den Rand des Inhalts selbst
  Nach dem Austausch von assets\logo.png once neu bauen lassen und build.cmd erneut laufen.
#>
param(
  [string]$Source = '',
  [int[]]$Mark = @(198, 38, 478, 274),
  [int[]]$Sizes = @(16, 24, 32, 48, 64, 256),
  [switch]$AutoTrim
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
if (-not $Source) { $Source = Join-Path $root 'assets\logo.png' }
if (-not (Test-Path $Source)) { throw "Logo fehlt: $Source" }
$img = [System.Drawing.Bitmap]::new($Source)

[int]$mx = $Mark[0]; [int]$my = $Mark[1]; [int]$mx1 = $Mark[2]; [int]$my1 = $Mark[3]
if ($AutoTrim) {
  # Undurchsichtige Pixel suchen und die groesste zusammenhaengende Bande benutzen:
  # der Schriftzug unter dem Zeichen ist eine eigene, flache Bande.
  $rect = [System.Drawing.Rectangle]::new(0, 0, $img.Width, $img.Height)
  $b32 = $img.Clone($rect, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $d = $b32.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $stride = $d.Stride
  $bytes = New-Object byte[] ($stride * $b32.Height)
  [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $bytes, 0, $bytes.Length)
  $b32.UnlockBits($d)
  $rows = New-Object int[] $img.Height
  $x0 = $img.Width; $x1 = -1
  for ($y = 0; $y -lt $img.Height; $y++) {
    for ($x = 0; $x -lt $img.Width; $x++) {
      if ($bytes[$y * $stride + $x * 4 + 3] -gt 16) { $rows[$y]++; if ($x -lt $x0) { $x0 = $x }; if ($x -gt $x1) { $x1 = $x } }
    }
  }
  $best = @(0, -1); $start = -1
  for ($y = 0; $y -le $img.Height; $y++) {
    $hit = ($y -lt $img.Height) -and ($rows[$y] -gt 0)
    if ($hit -and $start -lt 0) { $start = $y }
    if (-not $hit -and $start -ge 0) {
      if (($y - 1 - $start) -gt ($best[1] - $best[0])) { $best = @($start, $y - 1) }
      $start = -1
    }
  }
  $my = $best[0]; $my1 = $best[1]; $mx = $x0; $mx1 = $x1
}

# Das Zeichen in ein quadratisches Feld mit etwas Luft setzen - so wirkt es in der
# Taskleiste nicht abgeschnitten und laesst sich sauber herunterskalieren.
$mw = $mx1 - $mx + 1; $mh = $my1 - $my + 1
$side = [math]::Max($mw, $mh)
$pad = [int]([math]::Ceiling($side * 0.08))
$box = $side + 2 * $pad
$square = New-Object System.Drawing.Bitmap $box, $box
$g = [System.Drawing.Graphics]::FromImage($square)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
$g.Clear([System.Drawing.Color]::Transparent)
$offX = $pad + [int](($side - $mw) / 2)
$offY = $pad + [int](($side - $mh) / 2)
$src = [System.Drawing.Rectangle]::new($mx, $my, $mw, $mh)
$dst = [System.Drawing.Rectangle]::new($offX, $offY, $mw, $mh)
$g.DrawImage($img, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
$img.Dispose()

function BmpBytes([System.Drawing.Bitmap]$bmp) {
  $n = $bmp.Width
  $rect = [System.Drawing.Rectangle]::new(0, 0, $n, $n)
  $d = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $stride = $d.Stride
  $px = New-Object byte[] ($stride * $n)
  [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $px, 0, $px.Length)
  $bmp.UnlockBits($d)
  $body = New-Object 'System.Collections.Generic.List[byte]'
  for ($y = $n - 1; $y -ge 0; $y--) {                       # Bilddaten von unten nach oben
    for ($k = 0; $k -lt ($n * 4); $k++) { $body.Add($px[$y * $stride + $k]) }
  }
  $maskRow = ([int][math]::Floor(($n + 7) / 8) + 3) -band -4
  $body.AddRange([byte[]](, 0) * ($maskRow * $n))            # Maske: alles durchsichtig ueber Alpha
  $hdr = New-Object byte[] 40
  [void]([BitConverter]::GetBytes([int32]40)).CopyTo($hdr, 0)
  [void]([BitConverter]::GetBytes([int32]$n)).CopyTo($hdr, 4)
  [void]([BitConverter]::GetBytes([int32]($n * 2)).CopyTo($hdr, 8))
  [void]([BitConverter]::GetBytes([int16]1)).CopyTo($hdr, 12)
  [void]([BitConverter]::GetBytes([int16]32)).CopyTo($hdr, 14)
  [void]([BitConverter]::GetBytes([int32]$body.Count)).CopyTo($hdr, 20)
  return , (@($hdr) + $body)
}

function PngBytes([System.Drawing.Bitmap]$bmp) {
  $ms = New-Object System.IO.MemoryStream
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
  $b = $ms.ToArray(); $ms.Dispose()
  return , $b
}

$images = @()
foreach ($n in $Sizes) {
  $thumb = New-Object System.Drawing.Bitmap $n, $n
  $tg = [System.Drawing.Graphics]::FromImage($thumb)
  $tg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $tg.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
  $tg.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
  $tg.Clear([System.Drawing.Color]::Transparent)
  $tg.DrawImage($square, 0, 0, $n, $n)
  $tg.Dispose()
  if ($n -ge 256) { $data = PngBytes $thumb } else { $data = BmpBytes $thumb }
  $images += ,$data
  $thumb.Dispose()
}
$square.Dispose()

$out = New-Object 'System.Collections.Generic.List[byte]'
$out.AddRange([byte[]]([BitConverter]::GetBytes([int16]0) + [BitConverter]::GetBytes([int16]1) +
                       [BitConverter]::GetBytes([int16]$Sizes.Count)))
$offset = 6 + 16 * $Sizes.Count
for ($i = 0; $i -lt $Sizes.Count; $i++) {
  $n = $Sizes[$i]; $len = $images[$i].Count
  $out.Add([byte]($n -band 0xFF)); $out.Add([byte]($n -band 0xFF))
  $out.Add([byte]0); $out.Add([byte]0)
  $isPng = ($n -ge 256)
  $out.AddRange([byte[]]([BitConverter]::GetBytes([int16]1) +
                         [BitConverter]::GetBytes([int16]$(if ($isPng) { 0 } else { 32 }))))
  $out.AddRange([byte[]][BitConverter]::GetBytes([int32]$len))
  $out.AddRange([byte[]][BitConverter]::GetBytes([int32]$offset))
  $offset += $len
}
foreach ($img in $images) { $out.AddRange([byte[]]$img) }

$target = Join-Path $root 'app.ico'
[System.IO.File]::WriteAllBytes($target, [byte[]]$out)

# Nachlesen: die Datei muss als Symbol mit allen Groessen offenbar sein.
$check = New-Object System.Drawing.Icon $target
Write-Output ("app.ico geschrieben: " + $target + "  (" + $out.Count + " Byte)")
Write-Output ("  eingebundene Groessen: " + ($Sizes -join ', ') + "   (gelesen: " + $check.Width + "x" + $check.Height + ")")
$check.Dispose()
