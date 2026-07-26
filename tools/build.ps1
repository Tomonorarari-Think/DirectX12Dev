<#
================================================================================
  build.ps1
    MSBuild を使って本プロジェクトをビルドするスクリプト。

    ■ なぜスクリプトを挟むのか
      MSBuild.exe の場所は Visual Studio のバージョンやエディション
      （Community / Professional / BuildTools）によって変わります。
      tasks.json にパスを直接書くと、環境が変わるたびに壊れてしまいます。

      そこで Visual Studio に付属する「vswhere.exe」を使い、
      インストール済みの Visual Studio を検索して MSBuild の場所を
      自動的に特定します。これにより、環境に依存しない設定になります。

    ■ 使い方
      PowerShell から:
        .\tools\build.ps1                      … Debug / x64 をビルド
        .\tools\build.ps1 -Configuration Release
        .\tools\build.ps1 -Target Clean        … 中間ファイルを削除
      VSCode から:
        Ctrl+Shift+B （tasks.json がこのスクリプトを呼び出します）
================================================================================
#>

[CmdletBinding()]
param(
    # ビルド構成。Debug はデバッグ情報付き・最適化なし、Release は最適化あり。
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    # ターゲット CPU。DirectX 12 の学習では x64 を使います。
    [ValidateSet('x64', 'Win32')]
    [string]$Platform = 'x64',

    # MSBuild のターゲット。Build / Rebuild / Clean。
    [ValidateSet('Build', 'Rebuild', 'Clean')]
    [string]$Target = 'Build'
)

# エラーが出たら即座に停止する（失敗に気付かず進むのを防ぐ）
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# (1) vswhere.exe を探す
#     Visual Studio 2017 以降であれば、必ずこの固定パスに存在します。
# ---------------------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path $vswhere)) {
    Write-Error ("vswhere.exe が見つかりません。`n" +
                 "Visual Studio 2017 以降、または Build Tools for Visual Studio を" +
                 "インストールしてください。")
}

# ---------------------------------------------------------------------------
# (2) MSBuild.exe の場所を問い合わせる
#     -latest              : 最新バージョンの Visual Studio を選ぶ
#     -requires ...VCTools : C++ ビルドツールが入っているものに限定する
#     -find                : インストールフォルダからの相対パスで検索する
# ---------------------------------------------------------------------------
$msbuild = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1

if (-not $msbuild) {
    Write-Error ("MSBuild.exe が見つかりません。`n" +
                 "Visual Studio Installer で「C++ によるデスクトップ開発」" +
                 "ワークロードをインストールしてください。")
}

# ---------------------------------------------------------------------------
# (3) ビルド実行
#     $PSScriptRoot はこのスクリプトが置かれているフォルダ (tools/) を指す。
#     その 1 つ上がプロジェクトルート。
# ---------------------------------------------------------------------------
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'DirectX12Dev.vcxproj'

Write-Host "MSBuild       : $msbuild"
Write-Host "Project       : $projectFile"
Write-Host "Configuration : $Configuration | $Platform | $Target"
Write-Host ''

& $msbuild $projectFile `
    /t:$Target `
    /p:Configuration=$Configuration `
    /p:Platform=$Platform `
    /nologo `
    /verbosity:minimal

# $LASTEXITCODE には直前の外部プログラムの終了コードが入る。
# 0 以外はビルド失敗なので、その値でこのスクリプトも終了させる。
# （VSCode のタスクが「失敗」を正しく認識できるようにするため）
if ($LASTEXITCODE -ne 0) {
    Write-Host ''
    Write-Host "ビルドに失敗しました (exit code: $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ''
Write-Host "ビルドに成功しました: $projectRoot\build\$Platform\$Configuration\DirectX12Dev.exe" -ForegroundColor Green
