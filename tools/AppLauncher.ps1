<#
================================================================================
  AppLauncher.ps1
    資料撮影用にアプリを起動・操作・撮影するための共通部品。

    撮影スクリプト（capture-shader-lab.ps1 / capture-gif.ps1）から
    ドットソースで読み込んで使う。

      . "$PSScriptRoot\AppLauncher.ps1"
      $app = Start-DemoApp -Configuration Release
      Save-DemoShot $app 'out.png'
      Send-DemoKey  $app 'L'
      Stop-DemoApp  $app

    ねらい:
      ・作業中のディスプレイを塞がない。既定でサブディスプレイへ出す。
      ・フォーカスを奪わない。撮影のあいだ手元の作業を続けられる。

    仕組みの要点:
      ・PrintWindow と PostMessage は、対象が前面になくても効く。
        そのため撮影も操作もフォーカス無しで完結する。
      ・SetWindowPos に SWP_NOACTIVATE を付けると、位置だけ変えて
        アクティブにしない。付け忘れると移動のたびに前面へ来る。
      ・起動時の一瞬だけはアプリ側が前面に出る。これを消したい場合は
        環境変数 DX12DEV_NO_ACTIVATE=1 を立てる（Window.cpp が見る）。
================================================================================
#>

Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;

public class DemoWindow
{
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
    [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(
        IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);

    // 位置だけ変える／大きさは変えない／アクティブにしない
    public const uint SWP_NOSIZE = 0x0001, SWP_NOZORDER = 0x0004, SWP_NOACTIVATE = 0x0010;

    public const uint WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101;

    public static void Shot(IntPtr hwnd, string path, int width, int height)
    {
        using (var bmp = new Bitmap(width, height))
        using (var g = Graphics.FromImage(bmp))
        {
            IntPtr dc = g.GetHdc();
            PrintWindow(hwnd, dc, 2);   // 2 = PW_RENDERFULLCONTENT
            g.ReleaseHdc(dc);
            bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
        }
    }

    public static void Key(IntPtr hwnd, int virtualKey)
    {
        PostMessage(hwnd, WM_KEYDOWN, (IntPtr)virtualKey, (IntPtr)0);
        System.Threading.Thread.Sleep(40);
        PostMessage(hwnd, WM_KEYUP, (IntPtr)virtualKey, (IntPtr)0);
    }
}
"@ -ReferencedAssemblies System.Drawing


<#
.SYNOPSIS
  撮影用にアプリを起動し、ウィンドウをサブディスプレイへ寄せる。
.PARAMETER Configuration
  ビルド構成（Debug / Release）。
.PARAMETER Monitor
  出す先。Secondary はサブディスプレイ（無ければプライマリ）、Primary は主画面、
  Current は動かさない。
.PARAMETER LogPath
  標準出力の記録先。省略すると記録しない。
.OUTPUTS
  Process / Hwnd / Width / Height を持つオブジェクト。
#>
function Start-DemoApp
{
    [CmdletBinding()]
    param(
        [ValidateSet('Debug', 'Release')]
        [string]$Configuration = 'Release',

        [ValidateSet('Secondary', 'Primary', 'Current')]
        [string]$Monitor = 'Secondary',

        [string]$LogPath = '',

        [int]$Width = 1280,
        [int]$Height = 720,

        [int]$TimeoutMs = 15000
    )

    $root = Split-Path -Parent $PSScriptRoot
    $exe  = Join-Path $root ("build\x64\{0}\DirectX12Dev.exe" -f $Configuration)

    if (-not (Test-Path $exe)) {
        throw "実行ファイルがありません: $exe（先に .\tools\build.ps1 を実行してください）"
    }

    Get-Process DirectX12Dev -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 400

    # ★ 起動前の前面ウィンドウを覚えておき、最後に戻す。
    $previousForeground = [DemoWindow]::GetForegroundWindow()

    # アプリ側にも「前面に出るな」と伝える（Window.cpp が読む）。
    $env:DX12DEV_NO_ACTIVATE = '1'

    if ([string]::IsNullOrEmpty($LogPath)) {
        $proc = Start-Process -FilePath $exe -PassThru
    } else {
        New-Item -ItemType Directory -Force (Split-Path -Parent $LogPath) | Out-Null
        $proc = Start-Process -FilePath $exe -PassThru -RedirectStandardOutput $LogPath
    }

    # ★ 固定時間の Start-Sleep ではなく、ハンドルが取れるまで待つ。
    #   こうすると、出てから動かすまでの時間が最短になる。
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $hwnd  = [IntPtr]::Zero

    while ($watch.ElapsedMilliseconds -lt $TimeoutMs) {
        $proc.Refresh()
        if ($proc.HasExited) {
            throw ("アプリが起動直後に終了しました（終了コード {0}）。" -f $proc.ExitCode)
        }
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) {
            $hwnd = $proc.MainWindowHandle
            break
        }
        Start-Sleep -Milliseconds 50
    }

    if ($hwnd -eq [IntPtr]::Zero) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        throw 'ウィンドウを取得できませんでした。'
    }

    if ($Monitor -ne 'Current') {
        $screens = [System.Windows.Forms.Screen]::AllScreens
        $target  = $null

        if ($Monitor -eq 'Secondary') {
            $target = $screens | Where-Object { -not $_.Primary } | Select-Object -First 1
        }
        if ($null -eq $target) {
            $target = $screens | Where-Object { $_.Primary } | Select-Object -First 1
        }

        $area = $target.WorkingArea
        $x = $area.X + [int](($area.Width  - $Width)  / 2)
        $y = $area.Y + [int](($area.Height - $Height) / 2)

        $flags = [DemoWindow]::SWP_NOSIZE -bor [DemoWindow]::SWP_NOZORDER `
                 -bor [DemoWindow]::SWP_NOACTIVATE

        [void][DemoWindow]::SetWindowPos($hwnd, [IntPtr]::Zero, $x, $y, 0, 0, $flags)
    }

    # 奪ってしまったフォーカスを返す。
    if ($previousForeground -ne [IntPtr]::Zero) {
        [void][DemoWindow]::SetForegroundWindow($previousForeground)
    }

    # PrintWindow は起動直後に不安定なので、捨てショットで温める。
    $warm = Join-Path $env:TEMP 'dx12dev-warm.png'
    for ($i = 0; $i -lt 3; $i++) {
        [DemoWindow]::Shot($hwnd, $warm, $Width, $Height)
        Start-Sleep -Milliseconds 250
    }
    Remove-Item $warm -Force -ErrorAction SilentlyContinue

    return [pscustomobject]@{
        Process = $proc
        Hwnd    = $hwnd
        Width   = $Width
        Height  = $Height
    }
}


<#
.SYNOPSIS
  ウィンドウの中身を PNG として保存する。
#>
function Save-DemoShot
{
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]$App,
        [Parameter(Mandatory = $true)][string]$Path
    )

    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    [DemoWindow]::Shot($App.Hwnd, $Path, $App.Width, $App.Height)
}


<#
.SYNOPSIS
  キーを 1 つ送る。英数字 1 文字か、仮想キーコード（整数）を渡す。
#>
function Send-DemoKey
{
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]$App,
        [Parameter(Mandatory = $true)]$Key,
        [int]$SettleMs = 0
    )

    if ($Key -is [string]) {
        $virtualKey = [int][char]($Key.ToUpper()[0])
    } else {
        $virtualKey = [int]$Key
    }

    [DemoWindow]::Key($App.Hwnd, $virtualKey)

    if ($SettleMs -gt 0) { Start-Sleep -Milliseconds $SettleMs }
}


<#
.SYNOPSIS
  アプリを終了する。
#>
function Stop-DemoApp
{
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)]$App)

    Stop-Process -Id $App.Process.Id -Force -ErrorAction SilentlyContinue
    Remove-Item Env:\DX12DEV_NO_ACTIVATE -ErrorAction SilentlyContinue
}
