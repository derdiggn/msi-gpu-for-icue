# SPDX-License-Identifier: GPL-2.0-or-later
#
# Renders the device pictures iCUE shows for each card style into
# .\images\<style>\ : original artwork drawn with System.Drawing after the
# general shape of each card family (no manufacturer photos, logos or text).
#
# Each card is drawn from the fan side, tilted slightly so the top edge, where
# the lighting sits, is visible. The light bar rectangle of each
# device_view.png must match kArt in src/device_description.cpp: the script
# prints it and warns on a mismatch.
#
# PowerShell variables are case-insensitive, so the card sizes are $len, $hgt
# and $dep, never single letters.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot -Parent
$outDir = Join-Path $root 'images'

# --- drawing helpers ------------------------------------------------------------

function New-Color([int]$a, [int]$r, [int]$g, [int]$b) { [Drawing.Color]::FromArgb($a, $r, $g, $b) }
function New-Hex([string]$hex, [int]$a = 255) {
    $v = [Convert]::ToInt32($hex.TrimStart('#'), 16)
    New-Color $a (($v -shr 16) -band 255) (($v -shr 8) -band 255) ($v -band 255)
}
function New-Brush([Drawing.Color]$c) { New-Object Drawing.SolidBrush $c }
function New-Pen([Drawing.Color]$c, [float]$width) {
    $p = New-Object Drawing.Pen $c, $width
    $p.LineJoin = 'Round'
    $p
}

# Linear gradient between two points; mirrored beyond them, so a shape larger
# than the gradient shows no hard seams.
function New-Gradient([float]$x0, [float]$y0, [float]$x1, [float]$y1, [Drawing.Color]$c0, [Drawing.Color]$c1) {
    $ex = ($x1 - $x0) * 0.02
    $ey = ($y1 - $y0) * 0.02 + 0.01
    $br = New-Object Drawing.Drawing2D.LinearGradientBrush ([Drawing.PointF]::new($x0 - $ex, $y0 - $ey)), ([Drawing.PointF]::new($x1 + $ex, $y1 + $ey)), $c0, $c1
    $br.WrapMode = 'TileFlipXY'
    $br
}
function New-VGradient([float]$y0, [float]$y1, [Drawing.Color]$c0, [Drawing.Color]$c1) { New-Gradient 0 $y0 0 $y1 $c0 $c1 }

function New-PointList([float[]]$xy) {
    [Drawing.PointF[]]$(for ($i = 0; $i -lt $xy.Count; $i += 2) { [Drawing.PointF]::new($xy[$i], $xy[$i + 1]) })
}
function New-Path([float[]]$xy) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath
    $p.AddPolygon((New-PointList $xy))
    $p
}
function New-Circle([float]$cx, [float]$cy, [float]$r) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath
    $p.AddEllipse($cx - $r, $cy - $r, 2 * $r, 2 * $r)
    $p
}
function New-RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath
    $d = [Math]::Min(2 * $r, [Math]::Min($w, $h))
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    $p
}

# Fills a path clipped to $clip (null = no clip).
function Add-Fill([Drawing.Graphics]$g, $path, $brush, $clip) {
    if ($clip) { $g.SetClip($clip, 'Replace') }
    $g.FillPath($brush, $path)
    if ($clip) { $g.ResetClip() }
}

# Fine streaks for brushed metal with the given slope, drawn into the current
# clip; seeded so the output is reproducible.
function Add-Brushing([Drawing.Graphics]$g, [float]$x0, [float]$y0, [float]$x1, [float]$y1, [int]$seed, [float]$slope) {
    $rnd = [System.Random]::new($seed)
    $light = New-Pen (New-Color 22 255 255 255) 0.18
    $dark = New-Pen (New-Color 16 0 0 0) 0.18
    for ($i = 0; $i -lt 1800; $i++) {
        $x = $x0 - 60 + $rnd.NextDouble() * ($x1 - $x0 + 60)
        $y = $y0 + $rnd.NextDouble() * ($y1 - $y0)
        $n = 30 + $rnd.NextDouble() * 150
        $pen = if ($i % 2) { $light } else { $dark }
        $g.DrawLine($pen, [float]$x, [float]$y, [float]($x + $n), [float]($y + $n * $slope))
    }
}

# Carbon-fibre weave inside a path.
function Add-Carbon([Drawing.Graphics]$g, $path, $edge) {
    $g.FillPath((New-Object Drawing.Drawing2D.HatchBrush 'Weave', (New-Hex '#2e3034'), (New-Hex '#17181b')), $path)
    $g.DrawPath((New-Pen $edge 0.6), $path)
}

# Side faces of the front polygon seen in the oblique view: every edge whose
# outward normal faces the viewer offset (dx, -dy) gets a quad.
function Add-Extrusion([Drawing.Graphics]$g, [Drawing.PointF[]]$pts, [float]$dx, [float]$dy, $top, $side, [bool]$fins) {
    $n = $pts.Count
    for ($i = 0; $i -lt $n; $i++) {
        $p = $pts[$i]
        $q = $pts[($i + 1) % $n]
        $nx = $q.Y - $p.Y
        $ny = - ($q.X - $p.X)
        if ($nx * $dx - $ny * $dy -le 0) { continue }
        $quad = New-Path @($p.X, $p.Y, $q.X, $q.Y, ($q.X + $dx), ($q.Y - $dy), ($p.X + $dx), ($p.Y - $dy))
        $isTop = $ny -lt 0 -and [Math]::Abs($ny) -ge [Math]::Abs($nx)
        $c = if ($isTop) { $top } else { $side }
        # Shade across the face only, so the gradient runs parallel to the edge.
        $shade = if ($isTop) { New-Gradient 0 $p.Y 0 ($p.Y - $dy) $c[0] $c[1] } else { New-Gradient $p.X 0 ($p.X + $dx) 0 $c[0] $c[1] }
        $g.FillPath($shade, $quad)
        if (-not $isTop -and $fins) {
            # Heatsink fins seen through the open end of the shroud.
            $g.SetClip($quad, 'Replace')
            $fin = New-Pen (New-Hex '#3a3c41') 0.45
            for ($t = 0.08; $t -lt 1; $t += 0.07) {
                $g.DrawLine($fin, ($p.X + $dx * $t), ($p.Y - $dy * $t), ($q.X + $dx * $t), ($q.Y - $dy * $t))
            }
            $g.ResetClip()
        }
        $g.DrawPath((New-Pen (New-Color 120 0 0 0) 0.4), $quad)
    }
}

# A STORMFORCE-like fan: heatsink behind, seven swept blades, metal hub.
function Add-Fan([Drawing.Graphics]$g, [float]$cx, [float]$cy, [float]$r) {
    $well = New-Circle $cx $cy $r
    $g.FillPath((New-Brush (New-Hex '#0c0d0f')), $well)
    $g.SetClip($well, 'Replace')
    $finPen = New-Pen (New-Hex '#26282c') ($r * 0.012)
    for ($x = $cx - $r; $x -lt $cx + $r; $x += $r * 0.045) { $g.DrawLine($finPen, $x, ($cy - $r), $x, ($cy + $r)) }
    $pipe = New-Brush (New-Color 110 150 150 155)
    foreach ($k in -0.55, -0.18, 0.2, 0.56) { $g.FillRectangle($pipe, ($cx - $r), ($cy + $r * $k), (2 * $r), ($r * 0.07)) }
    $g.ResetClip()

    $rh = $r * 0.23
    $rt = $r * 0.95
    $n = 7
    $fill = New-VGradient ($cy - $r) ($cy + $r) (New-Hex '#45484e') (New-Hex '#131417')
    $edge = New-Pen (New-Hex '#5c5f66' 190) ($r * 0.012)
    $shine = New-Pen (New-Hex '#7d8188' 150) ($r * 0.02)
    for ($b = 0; $b -lt $n; $b++) {
        $a0 = 2 * [Math]::PI * $b / $n
        $lead = @()
        $trail = @()
        for ($i = 0; $i -le 16; $i++) {
            $t = $i / 16
            $rad = $rh + ($rt - $rh) * $t
            $mid = $a0 + 1.05 * $t * $t                      # strongly swept
            $half = 0.25 + 0.13 * [Math]::Sin($t * 2.2)      # widest past the middle
            $lead += [Drawing.PointF]::new($cx + $rad * [Math]::Cos($mid + $half), $cy + $rad * [Math]::Sin($mid + $half))
            $trail += [Drawing.PointF]::new($cx + $rad * [Math]::Cos($mid - $half), $cy + $rad * [Math]::Sin($mid - $half))
        }
        $back = $trail.Clone()
        [array]::Reverse($back)
        $p = New-Object Drawing.Drawing2D.GraphicsPath
        $p.AddLines([Drawing.PointF[]]($lead + $back))
        $p.CloseFigure()
        $g.FillPath($fill, $p)
        $g.DrawPath($edge, $p)
        $g.DrawLines($shine, [Drawing.PointF[]]$lead[4..16])
    }

    # Frame ring and hub.
    $g.DrawPath((New-Pen (New-Hex '#0a0a0c') ($r * 0.07)), (New-Circle $cx $cy ($r * 1.0)))
    $g.DrawPath((New-Pen (New-Hex '#55585e') ($r * 0.014)), (New-Circle $cx $cy ($r * 1.045)))
    $hub = New-Circle $cx $cy $rh
    $pg = New-Object Drawing.Drawing2D.PathGradientBrush $hub
    $pg.CenterPoint = [Drawing.PointF]::new($cx - $rh * 0.3, $cy - $rh * 0.35)
    $pg.CenterColor = New-Hex '#c4c7cc'
    $pg.SurroundColors = [Drawing.Color[]]@((New-Hex '#5b5f66'))
    $g.FillPath($pg, $hub)
    $g.DrawPath((New-Pen (New-Hex '#3c3f44') ($r * 0.02)), $hub)
    $g.DrawPath((New-Pen (New-Hex '#1c1d20') ($r * 0.03)), (New-Circle $cx $cy ($rh * 1.08)))
    $g.DrawPath((New-Pen (New-Color 60 0 0 0) ($r * 0.008)), (New-Circle $cx $cy ($rh * 0.66)))
}

function Add-Octagon([Drawing.Graphics]$g, [float]$cx, [float]$cy, [float]$r) {
    $xy = foreach ($i in 0..7) {
        $a = [Math]::PI / 8 + $i * [Math]::PI / 4
        ($cx + $r * [Math]::Cos($a)), ($cy + $r * [Math]::Sin($a))
    }
    $oct = New-Path ([float[]]$xy)
    $g.FillPath((New-Gradient ($cx - $r) ($cy - $r) ($cx + $r) ($cy + $r) (New-Hex '#2a2c30') (New-Hex '#0e0f11')), $oct)
    $g.DrawPath((New-Pen (New-Hex '#4b4e54') 0.8), $oct)
}

# Two braided hoses leaving the far end of a liquid-cooled card. They fade
# out towards the bottom of the picture instead of ending in mid-air.
function Add-Hose([Drawing.Graphics]$g, [float]$x, [float[]]$ys, [float]$bottom) {
    $braid = New-Object Drawing.Pen (New-Object Drawing.Drawing2D.HatchBrush 'DiagonalCross', (New-Hex '#2e3035'), (New-Hex '#121315')), 9.5
    $shine = New-Pen (New-Hex '#5a5d63' 110) 1.6
    $fadeTop = $bottom - 26
    $col = New-Hex '#18191c'
    $fade = New-Object Drawing.Drawing2D.LinearGradientBrush ([Drawing.PointF]::new(0, $fadeTop - 0.5)), ([Drawing.PointF]::new(0, $bottom + 0.5)), $col, (New-Color 0 $col.R $col.G $col.B)
    $tail = New-Object Drawing.Pen $fade, 9.5
    $i = 0
    foreach ($y in $ys) {
        $turn = $x + 34 - $i * 13
        $p = New-Object Drawing.Drawing2D.GraphicsPath
        $p.AddLine($x, $y, ($turn - 14), $y)
        $p.AddBezier(($turn - 14), $y, $turn, $y, $turn, $y, $turn, ($y + 14))
        $p.AddLine($turn, ($y + 14), $turn, $fadeTop)
        $g.DrawPath($braid, $p)
        $m = New-Object Drawing.Drawing2D.Matrix
        $m.Translate(-2.6, -2.6)
        $hl = $p.Clone()
        $hl.Transform($m)
        $g.DrawPath($shine, $hl)
        $g.DrawLine($tail, $turn, $fadeTop, $turn, $bottom)
        $i++
    }
    foreach ($y in $ys) {
        $f = New-RoundRect ($x - 3) ($y - 6.5) 9 13 2
        $g.FillPath((New-VGradient ($y - 6.5) ($y + 6.5) (New-Hex '#3a3c41') (New-Hex '#0f1012')), $f)
        $g.DrawPath((New-Pen (New-Hex '#5a5d63') 0.5), $f)
    }
}

# --- card styles (all sizes in millimetres) -------------------------------------

# Enum = the CardStyle value in src/card_style.h (for the printout).
$styles = @(
    @{ Key = 'gaming_trio';   Enum = 'GamingTrio';   Len = 338; Hgt = 140; Dep = 50 }
    @{ Key = 'vanguard';      Enum = 'Vanguard';     Len = 357; Hgt = 150; Dep = 70 }
    @{ Key = 'suprim';        Enum = 'Suprim';       Len = 359; Hgt = 150; Dep = 76 }
    @{ Key = 'suprim_liquid'; Enum = 'SuprimLiquid'; Len = 280; Hgt = 143; Dep = 44 }
    @{ Key = 'gaming';        Enum = 'Gaming';       Len = 247; Hgt = 128; Dep = 45 }
)

$x0 = 5           # the bracket takes x 0..4
$barHeight = 7

# Front outline (convex, clockwise) and the top edge [xa, xb] the light sits on.
function Get-Outline($st) {
    $len = $st.Len
    $hgt = $st.Hgt
    switch ($st.Key) {
        'gaming_trio'   { @{ Pts = @($x0, 0, ($len - 18), 0, $len, 16, $len, $hgt, 34, $hgt, $x0, ($hgt - 29)); Xa = $x0; Xb = $len - 18 } }
        'gaming'        { @{ Pts = @($x0, 0, ($len - 16), 0, $len, 14, $len, $hgt, 30, $hgt, $x0, ($hgt - 25)); Xa = $x0; Xb = $len - 16 } }
        'vanguard'      { @{ Pts = @($x0, 0, ($len - 24), 0, $len, 14, $len, ($hgt - 28), ($len - 14), $hgt, 30, $hgt, $x0, ($hgt - 25)); Xa = $x0; Xb = $len - 24 } }
        'suprim'        { @{ Pts = @($x0, 9, 14, 0, ($len - 30), 0, $len, 22, $len, ($hgt - 10), ($len - 10), $hgt, 40, $hgt, $x0, ($hgt - 30)); Xa = 14; Xb = $len - 30 } }
        'suprim_liquid' { @{ Pts = @($x0, 3, 8, 0, ($len - 3), 0, $len, 3, $len, ($hgt - 3), ($len - 3), $hgt, 8, $hgt, $x0, ($hgt - 3)); Xa = 8; Xb = $len - 3 } }
    }
}

# Panels, textures and fans on the front face.
function Add-FrontDetail([Drawing.Graphics]$g, $st, $front) {
    $len = $st.Len
    $hgt = $st.Hgt
    switch ($st.Key) {
        { $_ -in 'gaming_trio', 'gaming' } {
            $fans = if ($_ -eq 'gaming') { 2 } else { 3 }
            # Darker lower-left facet and the smoked translucent band across the top.
            Add-Fill $g (New-Path @($x0, ($hgt * 0.5), ($len * 0.16), $hgt, $x0, $hgt)) (New-Brush (New-Hex '#1d1e21')) $front
            $band = New-Path @(($len * 0.22), 0, $len, 0, $len, ($hgt * 0.40), ($len * 0.50), ($hgt * 0.40), ($len * 0.36), ($hgt * 0.24))
            Add-Fill $g $band (New-VGradient 0 ($hgt * 0.4) (New-Hex '#83858c' 165) (New-Hex '#35363a' 215)) $front
            $g.SetClip($front, 'Replace')
            $g.DrawLines((New-Pen (New-Hex '#9a9ca2' 120) 0.6), (New-PointList @(($len * 0.22), 0, ($len * 0.36), ($hgt * 0.24), ($len * 0.50), ($hgt * 0.40), $len, ($hgt * 0.40))))
            $g.ResetClip()
            $pitch = ($len - $x0) / $fans
            $r = [Math]::Min($pitch * 0.46, $hgt * 0.37)
            for ($i = 0; $i -lt $fans; $i++) { Add-Fan $g ($x0 + $pitch * ($i + 0.5)) ($hgt * 0.53) $r }
        }
        'vanguard' {
            # Lighter matte plates, carbon strips and the ribbed block at the end.
            Add-Fill $g (New-Path @(($len * 0.50), 0, $len, 0, $len, ($hgt * 0.32), ($len * 0.62), ($hgt * 0.32))) (New-VGradient 0 ($hgt * 0.32) (New-Hex '#5d6066') (New-Hex '#45474c')) $front
            Add-Fill $g (New-Path @(0, 0, ($len * 0.20), 0, ($len * 0.12), ($hgt * 0.45), 0, ($hgt * 0.45))) (New-Brush (New-Hex '#505257')) $front
            $g.SetClip($front, 'Replace')
            Add-Carbon $g (New-Path @(($len * 0.30), 0, ($len * 0.38), 0, ($len * 0.27), $hgt, ($len * 0.19), $hgt)) (New-Hex '#5a5d63')
            Add-Carbon $g (New-Path @(($len * 0.24), ($hgt * 0.86), ($len * 0.66), ($hgt * 0.80), ($len * 0.70), $hgt, ($len * 0.23), $hgt)) (New-Hex '#5a5d63')
            $rib = New-Path @(($len * 0.86), ($hgt * 0.62), $len, ($hgt * 0.62), $len, $hgt, ($len * 0.80), $hgt)
            $g.FillPath((New-Brush (New-Hex '#101113')), $rib)
            for ($y = $hgt * 0.65; $y -lt $hgt; $y += 3.2) { $g.DrawLine((New-Pen (New-Hex '#34363b') 1.2), ($len * 0.80), $y, $len, $y) }
            $g.ResetClip()
            $pitch = ($len - $x0) / 3
            $r = [Math]::Min($pitch * 0.475, $hgt * 0.39)
            for ($i = 0; $i -lt 3; $i++) { Add-Fan $g ($x0 + $pitch * ($i + 0.5)) ($hgt * 0.48) $r }
        }
        'suprim' {
            # Brushed silver centre plate, faceted gunmetal ends, black end block.
            $silver = New-Path @(($len * 0.17), 0, ($len * 0.60), 0, ($len * 0.70), ($hgt * 0.60), ($len * 0.66), $hgt, ($len * 0.30), $hgt, ($len * 0.22), ($hgt * 0.45))
            Add-Fill $g $silver (New-Gradient 0 0 ($len * 0.3) $hgt (New-Hex '#e1e3e7') (New-Hex '#8e939a')) $front
            $g.SetClip($front, 'Replace'); $g.SetClip($silver, 'Intersect')
            Add-Brushing $g 0 0 $len $hgt 7 0
            $g.SetClip($front, 'Replace')
            $g.DrawPath((New-Pen (New-Hex '#c79a6a' 200) 0.7), $silver)
            $block = New-Path @(($len * 0.83), 0, $len, 0, $len, $hgt, ($len * 0.88), $hgt, ($len * 0.79), ($hgt * 0.55))
            $g.FillPath((New-Gradient 0 0 0 $hgt (New-Hex '#26282c') (New-Hex '#121315')), $block)
            $g.FillPath((New-Brush (New-Hex '#2f3338')), (New-Path @(0, ($hgt * 0.55), ($len * 0.11), $hgt, 0, $hgt)))
            $g.ResetClip()
            $pitch = ($len - $x0) / 3
            $r = [Math]::Min($pitch * 0.46, $hgt * 0.36)
            for ($i = 0; $i -lt 3; $i++) { Add-Fan $g ($x0 + $pitch * ($i + 0.5)) ($hgt * 0.50) $r }
        }
        'suprim_liquid' {
            # Two-tone brushed silver, black facets around an octagonal fan frame,
            # a thin copper line along the border.
            $lower = New-Path @(0, ($hgt * 0.56), ($len * 0.62), ($hgt * 0.22), ($len * 0.62), $hgt, 0, $hgt)
            Add-Fill $g $lower (New-Gradient 0 ($hgt * 0.3) ($len * 0.2) $hgt (New-Hex '#b4b8be') (New-Hex '#7c8087')) $front
            $g.SetClip($front, 'Replace')
            Add-Brushing $g 0 0 $len $hgt 11 -0.16
            $border = @(($len * 0.36), $hgt, ($len * 0.56), ($hgt * 0.46))
            $black = New-Path @(($len * 0.36), $hgt, ($len * 0.56), ($hgt * 0.46), ($len * 0.74), ($hgt * 0.62), ($len * 0.92), ($hgt * 0.60), $len, ($hgt * 0.48), $len, $hgt)
            $g.FillPath((New-Gradient ($len * 0.5) 0 $len $hgt (New-Hex '#2b2d31') (New-Hex '#0f1012')), $black)
            $g.FillPath((New-Brush (New-Hex '#1b1c1f')), (New-Path @(($len * 0.36), $hgt, ($len * 0.56), ($hgt * 0.46), ($len * 0.64), $hgt)))
            $g.DrawLines((New-Pen (New-Hex '#c98f5c') 0.8), (New-PointList $border))
            $g.ResetClip()
            $cx = $len * 0.735
            $cy = $hgt * 0.48
            $r = $hgt * 0.30
            Add-Octagon $g $cx $cy ($r * 1.2)
            Add-Fan $g $cx $cy $r
        }
    }
}

# Draws one card style fitted into the pixel box; returns the light bar as
# integer pixels @(left, top, right, bottom).
function Write-Card([Drawing.Graphics]$g, $st, [float]$bx, [float]$by, [float]$bw, [float]$bh, [bool]$lit) {
    $len = $st.Len
    $hgt = $st.Hgt
    $liquid = $st.Key -eq 'suprim_liquid'
    $dx = 0.18 * $st.Dep
    $dy = 0.34 * $st.Dep
    $outline = Get-Outline $st
    $pts = New-PointList $outline.Pts

    $minX = -1
    $maxX = $len + $dx + $(if ($liquid) { 42 } else { 0 })
    $minY = -$dy
    $maxY = $hgt + $(if ($liquid) { 16 } else { 8 })
    $s = [Math]::Min($bw / ($maxX - $minX), $bh / ($maxY - $minY))
    $ox = $bx + ($bw - $s * ($maxX - $minX)) / 2 - $s * $minX
    $oy = $by + ($bh - $s * ($maxY - $minY)) / 2 - $s * $minY

    $g.SmoothingMode = 'AntiAlias'
    $g.ResetTransform()
    $g.TranslateTransform($ox, $oy)
    $g.ScaleTransform($s, $s)

    # Light bar on the top face, snapped to whole pixels so the zone rectangles
    # match exactly. The liquid card only lights its name plate near the bracket.
    $barY = -$dy / 2 - $barHeight / 2
    $barL = $outline.Xa + $dx / 2 + 5
    $barR = if ($liquid) { $len * 0.44 } else { $outline.Xb + $dx / 2 - 5 }
    $px = @([Math]::Round($ox + $s * $barL), [Math]::Round($oy + $s * $barY),
            [Math]::Round($ox + $s * $barR), [Math]::Round($oy + $s * ($barY + $barHeight)))
    $bar = [Drawing.RectangleF]::new(($px[0] - $ox) / $s, ($px[1] - $oy) / $s, ($px[2] - $px[0]) / $s, ($px[3] - $px[1]) / $s)

    # PCB edge with the PCIe fingers, and the bracket.
    $g.FillRectangle((New-Brush (New-Hex '#141a16')), $x0, ($hgt - 3), ($len * 0.62), 4.5)
    $gold = New-VGradient ($hgt + 1) ($hgt + 8) (New-Hex '#e3bf62') (New-Hex '#a77f34')
    $g.FillRectangle($gold, ($len * 0.14), ($hgt + 1.5), 11, 6.5)
    $g.FillRectangle($gold, ($len * 0.14 + 13), ($hgt + 1.5), 76, 6.5)
    $g.FillRectangle((New-Gradient 0 0 4 0 (New-Hex '#d4d7db') (New-Hex '#8a8e94')), 0, ($hgt * 0.10), 3.2, ($hgt * 0.92))

    # Body: side faces, then the front.
    $top = switch ($st.Key) {
        'suprim'        { @((New-Hex '#6f757e'), (New-Hex '#4d5259')) }
        'suprim_liquid' { @((New-Hex '#3a3c41'), (New-Hex '#1e1f22')) }
        'vanguard'      { @((New-Hex '#3c3e43'), (New-Hex '#232427')) }
        default         { @((New-Hex '#4a4c51'), (New-Hex '#2a2b2e')) }
    }
    Add-Extrusion $g $pts $dx $dy $top @((New-Hex '#1d1e21'), (New-Hex '#0d0e10')) (-not $liquid)

    $front = New-Path $outline.Pts
    $base = switch ($st.Key) {
        'suprim'        { @((New-Hex '#5f656e'), (New-Hex '#3a3e45')) }
        'suprim_liquid' { @((New-Hex '#dfe2e6'), (New-Hex '#a9adb3')) }
        'vanguard'      { @((New-Hex '#45474c'), (New-Hex '#2a2b2f')) }
        default         { @((New-Hex '#3d3e42'), (New-Hex '#232427')) }
    }
    $g.FillPath((New-VGradient 0 $hgt $base[0] $base[1]), $front)
    Add-FrontDetail $g $st $front
    $g.DrawPath((New-Pen (New-Color 150 0 0 0) 0.6), $front)
    $g.DrawLine((New-Pen (New-Color 70 255 255 255) 0.5), ($outline.Xa + 1), 0.3, ($outline.Xb - 1), 0.3)

    if ($liquid) {
        $far = $len + $dx * 0.5
        Add-Hose $g $far @(($hgt * 0.16 - $dy * 0.5), ($hgt * 0.34 - $dy * 0.5)) ($maxY)
    }

    # Light bar in its slot.
    $slot = New-RoundRect ($bar.X - 1) ($bar.Y - 1) ($bar.Width + 2) ($bar.Height + 2) 1.6
    $g.FillPath((New-Brush (New-Hex '#08080a')), $slot)
    $barPath = New-RoundRect $bar.X $bar.Y $bar.Width $bar.Height 1.2
    if ($lit) {
        $blend = New-Object Drawing.Drawing2D.ColorBlend 3
        $blend.Positions = [float[]]@(0, 0.5, 1)
        for ($k = 6; $k -ge 0; $k--) {
            $a = if ($k -eq 0) { 255 } else { 18 }
            $grow = $k * 1.0
            $rect = [Drawing.RectangleF]::new($bar.X - $grow, $bar.Y - $grow, $bar.Width + 2 * $grow, $bar.Height + 2 * $grow)
            $br = New-Object Drawing.Drawing2D.LinearGradientBrush $rect, (New-Color $a 255 46 92), (New-Color $a 40 140 255), 0.0
            $blend.Colors = [Drawing.Color[]]@((New-Color $a 255 46 92), (New-Color $a 170 64 255), (New-Color $a 40 140 255))
            $br.InterpolationColors = $blend
            $g.FillPath($br, (New-RoundRect $rect.X $rect.Y $rect.Width $rect.Height (1.2 + $grow)))
        }
        $g.FillPath((New-VGradient $bar.Y ($bar.Y + $bar.Height * 0.6) (New-Color 110 255 255 255) (New-Color 0 255 255 255)), $barPath)
    } else {
        $g.FillPath((New-VGradient $bar.Y ($bar.Y + $bar.Height) (New-Hex '#e9ebef') (New-Hex '#b3b7be')), $barPath)
    }
    $g.ResetTransform()
    return [int[]]$px
}

# --- output ---------------------------------------------------------------------

function New-DrawingSurface([int]$w, [int]$h) {
    $bmp = New-Object Drawing.Bitmap $w, $h, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.Clear([Drawing.Color]::Transparent)
    $g.PixelOffsetMode = 'HighQuality'
    @{ Bitmap = $bmp; Graphics = $g }
}

function Save-DrawingSurface($canvas, [string]$path) {
    $canvas.Graphics.Dispose()
    $canvas.Bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
}

# White where the picture is opaque, black elsewhere (alpha copied into RGB).
function Save-Mask([Drawing.Bitmap]$src, [string]$path) {
    $c = New-DrawingSurface $src.Width $src.Height
    $c.Graphics.Clear([Drawing.Color]::Black)
    $m = New-Object Drawing.Imaging.ColorMatrix (, [Single[][]]@(
        [Single[]]@(0, 0, 0, 0, 0), [Single[]]@(0, 0, 0, 0, 0), [Single[]]@(0, 0, 0, 0, 0),
        [Single[]]@(1, 1, 1, 0, 0), [Single[]]@(0, 0, 0, 1, 1)))
    $attr = New-Object Drawing.Imaging.ImageAttributes
    $attr.SetColorMatrix($m)
    $r = [Drawing.Rectangle]::new(0, 0, $src.Width, $src.Height)
    $c.Graphics.DrawImage($src, $r, 0, 0, $src.Width, $src.Height, [Drawing.GraphicsUnit]::Pixel, $attr)
    Save-DrawingSurface $c $path
}

# Bar rectangles the plugin uses, to catch a picture that no longer matches.
$cpp = Get-Content -Raw (Join-Path $root 'src\device_description.cpp')
$known = @{}
foreach ($m in [regex]::Matches($cpp, '\{"(\w+)",\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}')) {
    $known[$m.Groups[1].Value] = '{0} {1} {2} {3}' -f $m.Groups[2].Value, $m.Groups[3].Value, $m.Groups[4].Value, $m.Groups[5].Value
}

foreach ($st in $styles) {
    $dir = Join-Path $outDir $st.Key
    New-Item -ItemType Directory -Force $dir | Out-Null

    # Device view 615x408 (the size iCUE's own GPU plugins use).
    $view = New-DrawingSurface 615 408
    $bar = Write-Card $view.Graphics $st 20 40 575 328 $false
    Save-DrawingSurface $view (Join-Path $dir 'device_view.png')
    Save-Mask $view.Bitmap (Join-Path $dir 'device_view_mask.png')
    $view.Bitmap.Dispose()

    $promo = New-DrawingSurface 496 496
    Write-Card $promo.Graphics $st 12 110 472 276 $true | Out-Null
    Save-DrawingSurface $promo (Join-Path $dir 'promo.png')
    $promo.Bitmap.Dispose()

    $thumb = New-DrawingSurface 180 124
    Write-Card $thumb.Graphics $st 3 8 174 108 $true | Out-Null
    Save-DrawingSurface $thumb (Join-Path $dir 'thumbnail.png')
    $thumb.Bitmap.Dispose()

    $entry = '{{"{0}", {1}, {2}, {3}, {4}}},   // CardStyle::{5}' -f $st.Key, $bar[0], $bar[2], $bar[1], $bar[3], $st.Enum
    Write-Host $entry
    if ($known[$st.Key] -ne ('{0} {1} {2} {3}' -f $bar[0], $bar[2], $bar[1], $bar[3])) {
        Write-Warning "$($st.Key): light bar differs from kArt in src/device_description.cpp"
    }
}
Write-Host "Images written to $outDir"
