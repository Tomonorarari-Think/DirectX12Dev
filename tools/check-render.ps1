<#
================================================================================
  check-render.ps1
    3D シーンの見た目が壊れていないかを機械で確かめる。

    シェーダーの設定を変えると、落ちも警告も出ないまま絵だけが変わることが
    ある。実際に -Zpr（行優先の行列）を足して、カメラも物体も違う場所へ
    行った状態で公開してしまった（docs/tutorial/31_DXCとシェーダーモデル6.md）。

    そのとき「習作 01 番を 1 ピクセルずつ比べたので絵は変わっていない」と
    結論していた。習作は行列を 1 つも使わないので、比較が意味を持たなかった。

    ここでは 3D シーンそのものを撮り、基準画像と比べる。

    使い方:
      .\tools\check-render.ps1              # 基準と比べる
      .\tools\check-render.ps1 -UpdateBaseline   # 基準を撮り直す

    仕組み:
      ・パーティクルや回転があるので、1 ピクセル単位では毎回変わる。
      ・64 x 36 まで縮めてから比べる。粒の位置は消え、
        「どこに何があるか」だけが残る。
      ・行列が転置されるような壊れ方は、この粗さでもはっきり出る。
================================================================================
#>
[CmdletBinding()]
param(
    # 基準画像を撮り直す。
    [switch]$UpdateBaseline,

    # ビルド構成。
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    # 許容する平均差（0〜255）。これを超えたら失敗にする。
    [double]$Tolerance = 6.0
)

$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot
$exe       = Join-Path $root ("build\x64\{0}\DirectX12Dev.exe" -f $Configuration)
$baseline  = Join-Path $root 'tools\reference\scene.png'
$temp      = Join-Path $env:TEMP 'dx12dev-render-check'

if (-not (Test-Path $exe)) {
    throw "実行ファイルがありません: $exe（先に .\tools\build.ps1 を実行してください）"
}

New-Item -ItemType Directory -Force $temp | Out-Null
New-Item -ItemType Directory -Force (Split-Path -Parent $baseline) | Out-Null

Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class RenderCheck {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
    [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
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
        PostMessage(h, 0x0100, (IntPtr)vk, (IntPtr)0);
        System.Threading.Thread.Sleep(40);
        PostMessage(h, 0x0101, (IntPtr)vk, (IntPtr)0);
    }
}
"@ -ReferencedAssemblies System.Drawing

Get-Process DirectX12Dev -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

$proc = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru
Start-Sleep -Milliseconds 2500

$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    throw 'ウィンドウを取得できませんでした（起動直後に落ちた可能性があります）。'
}

# PrintWindow は起動直後に不安定なので、捨てショットで温める。
for ($i = 0; $i -lt 3; $i++) {
    [RenderCheck]::Shot($hwnd, (Join-Path $temp 'warm.png'))
    Start-Sleep -Milliseconds 250
}
Remove-Item (Join-Path $temp 'warm.png') -Force -ErrorAction SilentlyContinue

# パーティクルが出そろうのを待ってから撮る。
Start-Sleep -Milliseconds 1500

$shot = Join-Path $temp 'current.png'
[RenderCheck]::Shot($hwnd, $shot)

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

$compare = Join-Path $temp 'compare.py'

$python = @"
from PIL import Image, ImageChops, ImageStat
import os, sys

SHOT     = r'$shot'
BASELINE = r'$baseline'
UPDATE   = $(if ($UpdateBaseline) { 'True' } else { 'False' })
TOL      = $Tolerance

# タイトルバーを落として縮める。
#   粒の位置は消え、「どこに何があるか」だけが残る粗さにする。
def load(path):
    im = Image.open(path).convert('RGB').crop((0, 30, 1280, 720))
    return im.resize((64, 36), Image.BOX)

current = load(SHOT)

if UPDATE or not os.path.exists(BASELINE):
    current.save(BASELINE)
    print('基準画像を保存しました: ' + BASELINE)
    sys.exit(0)

base = Image.open(BASELINE).convert('RGB')

if base.size != current.size:
    print('基準画像の大きさが違います。-UpdateBaseline で撮り直してください。')
    sys.exit(1)

diff = ImageChops.difference(base, current)
stat = ImageStat.Stat(diff)

mean = sum(stat.mean) / 3.0
peak = max(e[1] for e in diff.getextrema())

print('平均差 %.2f / 最大差 %d（許容 %.1f）' % (mean, peak, TOL))

if mean > TOL:
    print('見た目が基準から離れています。意図した変更なら -UpdateBaseline を実行してください。')
    sys.exit(1)

print('見た目は基準どおりです。')
"@

[IO.File]::WriteAllText($compare, $python, [Text.UTF8Encoding]::new($false))

python $compare
exit $LASTEXITCODE
