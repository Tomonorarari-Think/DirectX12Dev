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
      PrintWindow は起動直後に不安定なため、捨てショットで温めてから撮る。
      これを省くと、最初の数枚が保存されないか、縮小された絵になる。
================================================================================
#>
[CmdletBinding()]
param(
    # ビルド構成。
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    # 待ち時間（ミリ秒）。動く習作が展開しきるまで待つ。
    [int]$SettleMs = 900
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root ("build\x64\{0}\DirectX12Dev.exe" -f $Configuration)
$temp = Join-Path $env:TEMP 'dx12dev-labshot'
$dest = Join-Path $root 'docs\shader-lab\images'

if (-not (Test-Path $exe)) {
    throw "実行ファイルがありません: $exe（先に .\tools\build.ps1 を実行してください）"
}

New-Item -ItemType Directory -Force $temp | Out-Null
New-Item -ItemType Directory -Force $dest | Out-Null

# --- 習作の一覧。ShaderLabPipeline.cpp の kShaderFiles と同じ順序にすること ---
$names = @(
    '01_uv', '02_shapes', '03_tiling', '04_noise', '05_fbm', '06_voronoi',
    '07_domainwarp', '08_palette', '09_plasma', '10_water', '11_raymarch',
    '12_mandelbrot', '13_truchet', '14_effects', '15_hash', '16_kaleidoscope',
    '17_fire', '18_starfield', '19_metaball', '20_bezier', '21_glitch',
    '22_mandelbulb', '23_lensflare', '24_thinfilm', '25_lightning',
    '26_woodmarble', '27_caustics'
)

Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class LabCapture {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
    [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    public const uint WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101;
    public static void Shot(IntPtr hwnd, string path) {
        using (var bmp = new Bitmap(1280, 720))
        using (var g = Graphics.FromImage(bmp)) {
            IntPtr dc = g.GetHdc();
            PrintWindow(hwnd, dc, 2);
            g.ReleaseHdc(dc);
            bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
        }
    }
    public static void Key(IntPtr h, int vk) {
        PostMessage(h, WM_KEYDOWN, (IntPtr)vk, (IntPtr)0);
        System.Threading.Thread.Sleep(40);
        PostMessage(h, WM_KEYUP, (IntPtr)vk, (IntPtr)0);
    }
}
"@ -ReferencedAssemblies System.Drawing

Get-Process DirectX12Dev -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

$proc = Start-Process -FilePath $exe -PassThru
Start-Sleep -Milliseconds 2500
$hwnd = $proc.MainWindowHandle

if ($hwnd -eq [IntPtr]::Zero) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    throw 'ウィンドウを取得できませんでした。アプリが起動できているか確認してください。'
}

# PrintWindow を温める（捨てショット）
for ($i = 0; $i -lt 3; $i++) {
    [LabCapture]::Shot($hwnd, (Join-Path $temp 'warm.png'))
    Start-Sleep -Milliseconds 250
}

[LabCapture]::Key($hwnd, 0x4C)   # L : 習作モードへ
Start-Sleep -Milliseconds 500
[LabCapture]::Key($hwnd, 0x31)   # 1 : 先頭へ
Start-Sleep -Milliseconds 500

for ($i = 1; $i -le $names.Count; $i++) {
    Start-Sleep -Milliseconds $SettleMs
    [LabCapture]::Shot($hwnd, (Join-Path $temp ("raw{0:D2}.png" -f $i)))
    Write-Output ("撮影 {0:D2} / {1}" -f $i, $names.Count)

    if ($i -lt $names.Count) {
        [LabCapture]::Key($hwnd, 0x27)   # →
        Start-Sleep -Milliseconds 350
    }
}

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue

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
