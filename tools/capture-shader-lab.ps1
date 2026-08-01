<#
================================================================================
  capture-shader-lab.ps1
    シェーダー習作の画像を、資料用に一括で撮り直すスクリプト。

    アプリを起動 → 習作モードへ入る → 1 本ずつ切り替えながら撮影 →
    余白を落として縮小 → 一覧シートを作る、までを自動で行う。

    使い方:
      .\tools\capture-shader-lab.ps1
      .\tools\capture-shader-lab.ps1 -Configuration Release

    必要なもの:
      ・Python 3 と Pillow（画像の切り出しと縮小に使う）
        pip install pillow

    出力先:
      docs/shader-lab/images/*.png  … 習作ごとの画像
      docs/shader-lab/images/00_index.png … 一覧シート

    注意:
      起動・撮影・キー送りは AppLauncher.ps1 に集約してある。
      既定ではサブディスプレイに出し、作業中のフォーカスも奪わない。
================================================================================
#>
[CmdletBinding()]
param(
    # ビルド構成。
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    # 待ち時間（ミリ秒）。動く習作が展開しきるまで待つ。
    [int]$SettleMs = 900,

    # ウィンドウを出すディスプレイ。
    [ValidateSet('Secondary', 'Primary', 'Current')]
    [string]$Monitor = 'Secondary'
)

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\AppLauncher.ps1"

$root = Split-Path -Parent $PSScriptRoot
$temp = Join-Path $env:TEMP 'dx12dev-labshot'
$dest = Join-Path $root 'docs\shader-lab\images'

New-Item -ItemType Directory -Force $temp | Out-Null
New-Item -ItemType Directory -Force $dest | Out-Null

# --- 習作の一覧。ShaderLabPipeline.cpp の kShaderFiles と同じ順序にすること ---
$names = @(
    '01_uv', '02_shapes', '03_tiling', '04_noise', '05_fbm', '06_voronoi',
    '07_domainwarp', '08_palette', '09_plasma', '10_water', '11_raymarch',
    '12_mandelbrot', '13_truchet', '14_effects', '15_hash', '16_kaleidoscope',
    '17_fire', '18_starfield', '19_metaball', '20_bezier', '21_glitch',
    '22_mandelbulb', '23_lensflare', '24_thinfilm', '25_lightning',
    '26_woodmarble', '27_caustics', '28_dissolve', '29_shockwave',
    '30_portal', '31_shield', '32_explosion', '33_trail'
)

$app = Start-DemoApp -Configuration $Configuration -Monitor $Monitor

Send-DemoKey $app 'L' -SettleMs 500          # 習作モードへ
Send-DemoKey $app '1' -SettleMs 500          # 先頭へ

for ($i = 1; $i -le $names.Count; $i++) {
    Start-Sleep -Milliseconds $SettleMs
    Save-DemoShot $app (Join-Path $temp ("raw{0:D2}.png" -f $i))
    Write-Output ("撮影 {0:D2} / {1}" -f $i, $names.Count)

    if ($i -lt $names.Count) {
        # ★ 送りが速すぎるとキーを取りこぼし、1 つ手前の習作を撮ってしまう。
        Send-DemoKey $app 0x27 -SettleMs 350   # →
    }
}

Stop-DemoApp $app

# --- 切り出し・縮小・一覧シートの作成は Python に任せる -----------------------
$script = Join-Path $temp 'process.py'
$nameList = ($names | ForEach-Object { "'$_'" }) -join ', '

$python = @"
from PIL import Image, ImageDraw
import os

TEMP = r'$temp'
DEST = r'$dest'
NAMES = [$nameList]

# タイトルバーぶんを落とし、資料用に横 960 へ縮小する
for i, name in enumerate(NAMES, 1):
    src = os.path.join(TEMP, 'raw%02d.png' % i)
    im = Image.open(src).convert('RGB').crop((0, 30, 1280, 720))
    im = im.resize((960, int(im.height * 960 / im.width)), Image.LANCZOS)
    im.save(os.path.join(DEST, name + '.png'))

# 一覧シート
cols, tw, th, gap, pad = 4, 300, 162, 6, 6
rows = (len(NAMES) + cols - 1) // cols
sheet = Image.new('RGB',
                  (pad * 2 + cols * tw + (cols - 1) * gap,
                   pad * 2 + rows * th + (rows - 1) * gap),
                  (246, 248, 250))
draw = ImageDraw.Draw(sheet)

for i, name in enumerate(NAMES):
    im = Image.open(os.path.join(DEST, name + '.png')).resize((tw, th), Image.LANCZOS)
    x = pad + (i % cols) * (tw + gap)
    y = pad + (i // cols) * (th + gap)
    sheet.paste(im, (x, y))
    draw.rectangle([x, y, x + tw - 1, y + th - 1], outline=(110, 119, 129))
    draw.rectangle([x, y, x + 30, y + 18], fill=(31, 35, 40))
    draw.text((x + 7, y + 4), name[:2], fill=(255, 255, 255))

sheet.save(os.path.join(DEST, '00_index.png'))
print('%d 枚を書き出しました。' % (len(NAMES) + 1))
"@

[IO.File]::WriteAllText($script, $python, [Text.UTF8Encoding]::new($false))

python $script

Write-Output ("出力先: {0}" -f $dest)
