<#
================================================================================
  check-diagrams.ps1
    資料の図が「読める色」になっているかを機械的に確認するスクリプト。

    目視では気付きにくい 2 つの規約違反を検出する。

      1. Mermaid の style で fill だけ指定し color を書き忘れている
         → GitHub の暗色モードで、淡い背景に白文字が乗って読めなくなる
      2. SVG の文字に黒以外の色が付いている
         → 色は線・矢印・塗りで示し、文字は黒に統一する

    使い方:
      .\tools\check-diagrams.ps1

    違反があれば一覧を表示し、終了コード 1 を返す。
================================================================================
#>
[CmdletBinding()]
param(
    # 走査の起点。既定はリポジトリのルート。
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

# 図の文字に使ってよい色（本文と補足）。
$AllowedTextFill = @('#1f2328', '#3d444d')

$violations = @()

# --- 1. Mermaid: fill があるのに color が無い style 行 -------------------------
$markdown = Get-ChildItem -Path $Root -Filter *.md -Recurse -File |
    Where-Object { $_.FullName -notmatch '\\(build|node_modules|\.git)\\' }

foreach ($file in $markdown) {
    $lines = [IO.File]::ReadAllLines($file.FullName, [Text.UTF8Encoding]::new($false))
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^\s*style\s+\S+\s+.*fill:#' -and $line -notmatch 'color:') {
            $violations += [pscustomobject]@{
                Kind    = 'Mermaid'
                Where   = "{0}:{1}" -f $file.FullName.Substring($Root.Length + 1), ($i + 1)
                Detail  = "style に color: が無い -> $($line.Trim())"
            }
        }
    }
}

# --- 2. SVG: 黒以外の色が付いた文字 ------------------------------------------
$svgFiles = Get-ChildItem -Path (Join-Path $Root 'docs\assets') -Filter *.svg -File

foreach ($file in $svgFiles) {
    $text = [IO.File]::ReadAllText($file.FullName, [Text.UTF8Encoding]::new($false))

    # CSS クラスの fill を集める（.cls { fill: #xxxxxx; ... }）
    $classFill = @{}
    foreach ($m in [regex]::Matches($text, '\.([\w-]+)\s*\{([^}]*)\}')) {
        if ($m.Groups[2].Value -match 'fill:\s*(#[0-9a-fA-F]{3,6})') {
            $classFill[$m.Groups[1].Value] = $Matches[1].ToLower()
        }
    }

    foreach ($m in [regex]::Matches($text, '<(?:text|tspan)\b[^>]*>')) {
        $tag = $m.Value

        # 属性が優先。無ければ class から引く。
        $fill = $null
        if ($tag -match 'fill="(#[0-9a-fA-F]{3,6})"') {
            $fill = $Matches[1].ToLower()
        }
        elseif ($tag -match 'class="([^"]*)"') {
            foreach ($name in ($Matches[1] -split '\s+')) {
                if ($classFill.ContainsKey($name)) { $fill = $classFill[$name] }
            }
        }

        if ($fill -and ($AllowedTextFill -notcontains $fill)) {
            $line = ($text.Substring(0, $m.Index) -split "`n").Count
            $violations += [pscustomobject]@{
                Kind    = 'SVG'
                Where   = "docs\assets\{0}:{1}" -f $file.Name, $line
                Detail  = "文字色 $fill は許可されていない（黒 $($AllowedTextFill -join ' / ') のみ）"
            }
        }
    }
}

# --- 結果 --------------------------------------------------------------------
Write-Output ("Markdown {0} 件 / SVG {1} 件を確認しました。" -f $markdown.Count, $svgFiles.Count)

if ($violations.Count -eq 0) {
    Write-Output '図の配色に問題はありません。'
    exit 0
}

Write-Output ''
Write-Output ("規約違反 {0} 件:" -f $violations.Count)
$violations | Format-Table -AutoSize | Out-String -Width 200 | Write-Output
exit 1
