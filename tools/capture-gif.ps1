<#
================================================================================
  capture-gif.ps1
    動きのある表現を GIF アニメーションとして撮るスクリプト。

    静止画では「時間で何が起きているか」が伝わらない表現（ディゾルブ、
    パーティクル、揺らぎなど）の資料用。

    使い方:
      # 3D シーンをそのまま撮る
      .\tools\capture-gif.ps1 -Out docs\assets\dissolve.gif

      # 習作モードの 17 番（炎）を撮る
      .\tools\capture-gif.ps1 -LabIndex 17 -Out docs\shader-lab\images\17_fire.gif

    必要なもの:
      Python 3 と Pillow（pip install pillow）

    注意:
      ・GIF は 256 色しか使えない。グラデーションは縞が出るので、
        減色したうえでディザ（誤差拡散）を掛けている。
      ・色数を減らすとファイルが小さくなる。空のような広いグラデーションが
        あると、色数を削ったときに縞が目立ちやすい。
      ・PrintWindow は起動直後に不安定なため、捨てショットで温めてから撮る。
================================================================================
#>
[CmdletBinding()]
param(
    # 出力先（リポジトリのルートからの相対、または絶対パス）。
    [Parameter(Mandatory = $true)]
    [string]$Out,

    # ビルド構成。
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    # 撮る枚数。
    [int]$Frames = 48,

    # 1 枚あたりの待ち時間（ミリ秒）。GIF の再生間隔にもこの値を使う。
    [int]$IntervalMs = 55,

    # 習作モードで撮るときの番号（1 始まり）。0 なら 3D シーンを撮る。
    [int]$LabIndex = 0,

    # 出力する GIF の横幅。
    [int]$Width = 480,

    # 使う色数（最大 256）。減らすとファイルが小さくなる。
    [ValidateRange(16, 256)]
    [int]$Colors = 160,

    # 撮り始めるまでの待ち時間（ミリ秒）。表現が展開しきるのを待つ。
    [int]$WarmupMs = 1200
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root ("build\x64\{0}\DirectX12Dev.exe" -f $Configuration)
$temp = Join-Path $env:TEMP 'dx12dev-gif'

if (-not (Test-Path $exe)) {
    throw "実行ファイルがありません: $exe（先に .\tools\build.ps1 を実行してください）"
}

if (-not [IO.Path]::IsPathRooted($Out)) {
    $Out = Join-Path $root $Out
}

New-Item -ItemType Directory -Force $temp | Out-Null
New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null
Get-ChildItem $temp -Filter '*.png' -ErrorAction SilentlyContinue | Remove-Item -Force

Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class GifCapture {
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
    throw 'ウィンドウを取得できませんでした。'
}

# PrintWindow を温める（捨てショット）
for ($i = 0; $i -lt 3; $i++) {
    [GifCapture]::Shot($hwnd, (Join-Path $temp 'warm.png'))
    Start-Sleep -Milliseconds 250
}
Remove-Item (Join-Path $temp 'warm.png') -Force -ErrorAction SilentlyContinue

if ($LabIndex -gt 0) {
    [GifCapture]::Key($hwnd, 0x4C)   # L : 習作モードへ
    Start-Sleep -Milliseconds 500
    [GifCapture]::Key($hwnd, 0x31)   # 1 : 先頭へ
    Start-Sleep -Milliseconds 400

    for ($i = 1; $i -lt $LabIndex; $i++) {
        [GifCapture]::Key($hwnd, 0x27)   # →
        Start-Sleep -Milliseconds 160
    }
    Start-Sleep -Milliseconds 400
}

Start-Sleep -Milliseconds $WarmupMs

for ($i = 0; $i -lt $Frames; $i++) {
    [GifCapture]::Shot($hwnd, (Join-Path $temp ("f{0:D3}.png" -f $i)))
    Start-Sleep -Milliseconds $IntervalMs
}

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Write-Output ("{0} 枚を撮影しました。" -f $Frames)

# --- GIF への変換は Python に任せる ------------------------------------------
$script = Join-Path $temp 'togif.py'

$python = @"
from PIL import Image
import glob
import os

TEMP  = r'$temp'
OUT   = r'$Out'
WIDTH  = $Width
DELAY  = $IntervalMs
COLORS = $Colors

paths = sorted(glob.glob(os.path.join(TEMP, 'f*.png')))
frames = []

for path in paths:
    im = Image.open(path).convert('RGB')

    # タイトルバーを落とす
    im = im.crop((0, 30, 1280, 720))
    im = im.resize((WIDTH, int(im.height * WIDTH / im.width)), Image.LANCZOS)

    # GIF は 256 色まで。中央値分割で減色し、ディザで縞を散らす。
    frames.append(im.convert('P', palette=Image.ADAPTIVE, colors=COLORS,
                             dither=Image.FLOYDSTEINBERG))

frames[0].save(OUT, save_all=True, append_images=frames[1:],
               duration=DELAY, loop=0, optimize=True)

size_kb = os.path.getsize(OUT) / 1024.0
print('%s (%d 枚, %.0f KB)' % (OUT, len(frames), size_kb))
"@

[IO.File]::WriteAllText($script, $python, [Text.UTF8Encoding]::new($false))

python $script
