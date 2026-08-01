<#
================================================================================
  check-docs.ps1
    コードと資料の検査をまとめて走らせるスクリプト。

    このリポジトリでは資料もコードと同じ成果物として扱うため、
    ビルドと同じ重さで機械検査する。CI（.github/workflows/ci.yml）も
    このスクリプトを呼ぶ。

    使い方:
      .\tools\check-docs.ps1
      .\tools\check-docs.ps1 -SkipMermaid    # Node を入れていないとき

    必要なもの:
      ・Python 3（リンク・コントラスト・行幅・混入の検査）
      ・Node.js（Mermaid の構文検証。無ければ自動で飛ばす）
        初回のみ: cd tools\checks\mermaid; npm install

    検査の内容:
      1. 図の配色      … Mermaid の style に color 指定があるか、SVG の文字色
      2. 相対リンク    … Markdown のリンク切れ
      3. コントラスト  … SVG の文字と背景が 7:1 以上か
      4. 行幅          … コメントを含む 1 行が表示幅 96 桁以内か
      5. 文字の混入    … キリル文字などが紛れていないか
      6. 文字コード    … .ps1 が UTF-8 BOM 付きか
      7. Mermaid 構文  … mermaid.parse が通るか
================================================================================
#>
[CmdletBinding()]
param(
    # Mermaid の構文検証を飛ばす。
    [switch]$SkipMermaid
)

$ErrorActionPreference = 'Continue'

$root = Split-Path -Parent $PSScriptRoot
$checks = Join-Path $PSScriptRoot 'checks'

$failed = @()

function Invoke-Check
{
    param([string]$Name, [scriptblock]$Body)

    Write-Output ''
    Write-Output ('=== {0} ' -f $Name).PadRight(72, '=')

    & $Body

    if ($LASTEXITCODE -ne 0) {
        $script:failed += $Name
        Write-Output ('--> {0} : 失敗' -f $Name)
    }
}

# 1. 図の配色（Mermaid の style と SVG の文字色）
Invoke-Check '図の配色' {
    & (Join-Path $PSScriptRoot 'check-diagrams.ps1')
    # check-diagrams.ps1 は throw で失敗するため、ここまで来たら成功。
    $global:LASTEXITCODE = 0
}

# 2〜5. Python の検査
Invoke-Check '相対リンク'   { python (Join-Path $checks 'check-links.py') $root }
Invoke-Check 'コントラスト' { python (Join-Path $checks 'check-contrast.py') `
                                     (Join-Path $root 'docs\assets') }
Invoke-Check '行幅 96 桁'   { python (Join-Path $checks 'check-width.py') $root }
Invoke-Check '文字の混入'   { python (Join-Path $checks 'check-stray-script.py') $root }
Invoke-Check '文字コード'   { python (Join-Path $checks 'check-encoding.py') $root }

# 7. Mermaid の構文検証（Node が要る）
if ($SkipMermaid) {
    Write-Output ''
    Write-Output 'Mermaid の構文検証は -SkipMermaid のため飛ばしました。'
} elseif ($null -eq (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Output ''
    Write-Output 'Node.js が見つからないため Mermaid の構文検証を飛ばしました。'
} elseif (-not (Test-Path (Join-Path $checks 'mermaid\node_modules'))) {
    Write-Output ''
    Write-Output 'mermaid の依存が未取得のため飛ばしました。'
    Write-Output '  cd tools\checks\mermaid; npm install'
} else {
    Invoke-Check 'Mermaid 構文' {
        node (Join-Path $checks 'mermaid\validate.mjs') $root
    }
}

Write-Output ''
Write-Output ''.PadRight(72, '=')

if ($failed.Count -eq 0) {
    Write-Output 'すべての検査を通過しました。'
    exit 0
}

Write-Output ('失敗した検査: {0}' -f ($failed -join ', '))
exit 1
