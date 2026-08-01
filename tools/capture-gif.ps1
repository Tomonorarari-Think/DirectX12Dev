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
      ・起動・撮影・キー送りは AppLauncher.ps1 に集約してある。
        既定ではサブディスプレイに出し、作業中のフォーカスも奪わない。
      ・撮影の 1 枚あたりに掛かった実測時間を GIF の再生間隔に使う。
        こうすると再生速度が実時間と一致する（既定でおよそ 10 秒）。
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

    # 撮る枚数。既定は 1 枚あたり実測 90 ms 前後 × 110 枚 ＝ およそ 10 秒。
    [int]$Frames = 110,

    # 1 枚あたりの追加の待ち時間（ミリ秒）。
    #   実際の間隔は「撮影に掛かった時間 ＋ この値」になる。
    [int]$IntervalMs = 55,

    # 習作モードで撮るときの番号（1 始まり）。0 なら 3D シーンを撮る。
    [int]$LabIndex = 0,

    # 出力する GIF の横幅。
    [int]$Width = 440,

    # 使う色数（最大 256）。減らすとファイルが小さくなる。
    [ValidateRange(16, 256)]
    [int]$Colors = 128,

    # 撮り始めるまでの待ち時間（ミリ秒）。表現が展開しきるのを待つ。
    [int]$WarmupMs = 1200,

    # 撮影前に押しておくキー（英字 1 文字ずつ）。例: -PressKeys F,V
    [string[]]$PressKeys = @(),

    # ウィンドウを出すディスプレイ。
    [ValidateSet('Secondary', 'Primary', 'Current')]
    [string]$Monitor = 'Secondary'
)

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\AppLauncher.ps1"

$root = Split-Path -Parent $PSScriptRoot
$temp = Join-Path $env:TEMP 'dx12dev-gif'

if (-not [IO.Path]::IsPathRooted($Out)) {
    $Out = Join-Path $root $Out
}

New-Item -ItemType Directory -Force $temp | Out-Null
New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null
Get-ChildItem $temp -Filter '*.png' -ErrorAction SilentlyContinue | Remove-Item -Force

$app = Start-DemoApp -Configuration $Configuration -Monitor $Monitor

if ($LabIndex -gt 0) {
    Send-DemoKey $app 'L' -SettleMs 500   # 習作モードへ
    Send-DemoKey $app '1' -SettleMs 400   # 先頭へ

    # ★ 送りが速すぎるとキーが取りこぼされ、1 つ手前の習作を撮ってしまう。
    #   capture-shader-lab.ps1 と同じ 350 ms まで落とすと取りこぼさない。
    for ($i = 1; $i -lt $LabIndex; $i++) {
        Send-DemoKey $app 0x27 -SettleMs 350   # →
    }
    Start-Sleep -Milliseconds 400
}

foreach ($key in $PressKeys) {
    Send-DemoKey $app $key -SettleMs 300
}

Start-Sleep -Milliseconds $WarmupMs

# 撮影に掛かった実時間を測る。GIF の再生間隔にこの実測値を使うと、
# 再生速度が実際の動きと一致する。
$watch = [Diagnostics.Stopwatch]::StartNew()

for ($i = 0; $i -lt $Frames; $i++) {
    Save-DemoShot $app (Join-Path $temp ("f{0:D3}.png" -f $i))
    Start-Sleep -Milliseconds $IntervalMs
}

$watch.Stop()
$delayMs = [int][Math]::Round($watch.Elapsed.TotalMilliseconds / $Frames)

Stop-DemoApp $app
Write-Output ("{0} 枚を撮影しました（実測 {1:F1} 秒 / 1 枚 {2} ms）。" -f `
              $Frames, $watch.Elapsed.TotalSeconds, $delayMs)

# --- GIF への変換は Python に任せる ------------------------------------------
$script = Join-Path $temp 'togif.py'

$python = @"
from PIL import Image
import glob
import os

TEMP  = r'$temp'
OUT   = r'$Out'
WIDTH  = $Width
DELAY  = $delayMs
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
print('%s (%d 枚, %d ms 間隔, 再生 %.1f 秒, %.0f KB)'
      % (OUT, len(frames), DELAY, len(frames) * DELAY / 1000.0, size_kb))
"@

[IO.File]::WriteAllText($script, $python, [Text.UTF8Encoding]::new($false))

python $script
