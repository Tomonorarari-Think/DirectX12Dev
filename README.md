# DirectX 12 Dev — DirectX 12 学習プロジェクト

DirectX 12 を **初期化から画面表示まで一歩ずつ理解する** ための学習用リポジトリです。
「動くサンプル」ではなく「**なぜそう書くのかが分かるサンプル**」を目指し、
ソースコードには初学者向けの詳細なコメント、`docs/` には解説資料を置いています。

## 環境

| 項目 | 内容 |
|------|------|
| OS | Windows 11 |
| IDE | Visual Studio 2026 Community / Visual Studio Code |
| ビルド | MSBuild（`DirectX12Dev.vcxproj`） |
| 言語 | C++20 |
| API | Direct3D 12 |

## 進捗

- [ ] Step 1: Win32 ウィンドウ表示
- [ ] Step 2: DirectX 12 初期化
- [ ] Step 3: 画面クリア
- [ ] Step 4: 三角形の描画

## ドキュメント

`docs/` 以下に学習資料を追加していきます。

## ブランチ運用

git-flow に従います。

- `main` … リリース可能な状態
- `develop` … 開発の統合先
- `feature/*` … 機能単位の作業ブランチ（マージ後に削除）
