# CLAUDE.md — このリポジトリで作業するときのルール

DirectX 12 の**学習**を目的としたプロジェクトです。
「動くコード」より「**読んで分かるコード**」を優先します。

## コメント

- **宣言には Doxygen 形式のドキュメントコメント（`///`）を付ける。**
  `@brief` は必須。引数・戻り値・例外があれば `@param` / `@returns` / `@exception` も必須。
  背景や理由は `@brief` の後に空行を置いて本文として書く（Markdown が使える）。
  - **XML 形式（`<summary>` 等）は使わない。** VSCode の C/C++ 拡張は
    Doxygen にしか対応しておらず、XML はツールチップで生のタグが見えてしまう。
- 関数の内部、ファイル冒頭、マクロ、`.hlsl` は `//` で書く（Doxygen の対象外のため）。
- 詳しい解説はヘッダ 1 か所に置き、`.cpp` の定義側は 1 行の `@brief` だけにする。
- 粒度は**初学者向けに厚く**。「何をしているか」だけでなく「なぜそうするか」を書く。
- 1 行は表示幅 96 桁以内（全角は 2 桁）。行頭に `。、）」` を置かない。

詳細は [docs/conventions/07_コーディング規約.md](docs/conventions/07_コーディング規約.md) を参照。

## 実装方針

- 外部ライブラリ（DirectXTK12 / `d3dx12.h`）を使わず、生の DirectX 12 API を直接呼ぶ。
  ヘルパーが隠している処理こそが学習対象のため。
- 分かりやすさのために単純化した箇所は、**理由と改善方法をコメントと `docs/` に明記する**。
- 性能改善は「効くはず」で終わらせず、`FrameTimer` で**測ってから**判断する。
  効果が出なかった場合もその結果を資料に残す。

## ビルドと検証

```powershell
.\tools\build.ps1                      # Debug x64
.\tools\build.ps1 -Configuration Release
```

コードを変更したら **Debug と Release の両方をビルドし、実行して確認する**まで完了としない。

## Git

- git-flow に従う。`feature/*` で作業し、`develop` へ `--no-ff` でマージ後、**ブランチを必ず削除**する。
- 区切りが付いたら `release/x.y.z` を切って `main` へマージし、タグを打つ。
- コミットメッセージは Conventional Commits（`feat:` / `fix:` / `perf:` / `docs:` / `chore:` …）。
  概要行で「何を」、本文で「なぜ」を書く。
- 詳細は [docs/conventions/06_Git運用ルール.md](docs/conventions/06_Git運用ルール.md) を参照。

## 資料

コードを変更したら、対応する `docs/` も同じコミットで更新する。
このリポジトリでは資料もコードと同じ成果物として扱う。

### 配置

| フォルダ | 内容 |
|---------|------|
| `docs/setup/` | 環境構築と実行手順 |
| `docs/concepts/` | DirectX 12 そのものの概念 |
| `docs/architecture/` | 本プロジェクトの設計 |
| `docs/troubleshooting/` | 症状別の原因逆引き |
| `docs/conventions/` | 開発ルール |
| `docs/assets/` | 図（SVG） |

### 図

- **アスキーアートで図を描かない。**
- 処理の流れ・依存関係・状態遷移・Git 履歴 → **Mermaid**
  （日本語ラベルは `["..."]` とダブルクォートで囲む）
- メモリ配置・座標系・タイムラインなど位置関係が意味を持つ図 → **SVG**
  （`docs/assets/` に置き、背景 `#f6f8fa`・`viewBox`・`aria-label` を付ける）
- ディレクトリ構成やコンソール出力は、そのままで読めるのでコードブロックでよい。

詳細は [docs/conventions/07_コーディング規約.md](docs/conventions/07_コーディング規約.md#5-ドキュメントの図) を参照。
