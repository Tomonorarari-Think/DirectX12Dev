# DirectX 12 Dev — DirectX 12 学習プロジェクト

DirectX 12 を **初期化から画面表示まで一歩ずつ理解する** ための学習用リポジトリです。
「動くサンプル」ではなく「**なぜそう書くのかが分かるサンプル**」を目指し、
ソースコードには初学者向けの詳細なコメント、`docs/` には解説資料を置いています。

現在の到達点：**市松模様のテクスチャを貼った三角形 3 枚が、前後関係を保ったまま回転する**

![深度テストの有無による見え方の違い](docs/assets/depth-test.svg)

わざと **手前 → 奥の順**（＝深度テストが無ければ破綻する順）で描いています。
それでも正しく見えることが、深度テストが効いている証拠です。
4 秒で 1 回転し、アスペクト比を補正しているので形は歪みません。
テクスチャの色は頂点カラーと掛け合わせている（モジュレート）ため、
三角形ごとに赤・緑・青の色味が残ります。

---

## クイックスタート

### VSCode

1. 拡張機能 **C/C++** (`ms-vscode.cpptools`) をインストール
2. このフォルダを開く
3. `F5` を押す（ビルドは自動で行われます）

### Visual Studio 2026

1. `DirectX12Dev.slnx` を開く
2. 構成を **Debug / x64** にする
3. `F5` を押す

詳細・トラブルシューティングは [docs/setup/01_ビルドと実行方法.md](docs/setup/01_ビルドと実行方法.md) を参照してください。

---

## 環境

| 項目 | 内容 |
|------|------|
| OS | Windows 11 |
| IDE | Visual Studio 2026 Community / Visual Studio Code |
| ビルド | MSBuild（`DirectX12Dev.vcxproj`） |
| 言語 | C++20 |
| API | Direct3D 12 / DXGI 1.6 |
| 依存ライブラリ | なし（Windows SDK のみ。外部ライブラリは意図的に不使用） |

> **なぜ外部ライブラリを使わないのか**
> DirectXTK12 や `d3dx12.h` はコードを短くしてくれますが、
> 隠している処理こそが学習で理解すべき対象です。
> そのため本プロジェクトでは生の DirectX 12 API を直接呼んでいます。

---

## ドキュメント

| # | ドキュメント | 内容 |
|---|-------------|------|
| — | [docs/README.md](docs/README.md) | 資料インデックスと学習方針 |
| 01 | [ビルドと実行方法](docs/setup/01_ビルドと実行方法.md) | VSCode / VS2026 での手順、トラブルシューティング |
| 02 | [DirectX 12 の用語集](docs/concepts/02_DirectX12の用語集.md) | デバイス、ディスクリプタ、バリアなど中核概念 |
| 03 | [初期化と1フレームの流れ](docs/architecture/03_初期化と1フレームの流れ.md) | 処理順序と、その順序である理由 |
| 04 | [クラス設計](docs/architecture/04_クラス設計.md) | クラス構成・責務分担・設計判断の理由 |
| 05 | [つまずきポイント集](docs/troubleshooting/05_つまずきポイント集.md) | 症状別の原因逆引き表 |
| 06 | [Git 運用ルール](docs/conventions/06_Git運用ルール.md) | git-flow とコミット規約 |
| 07 | [コーディング規約](docs/conventions/07_コーディング規約.md) | Doxygen コメント・命名規則・文字コード |

---

## プロジェクト構成

```
DirectX12Dev/
├── src/
│   ├── main.cpp                      エントリポイント・メインループ
│   ├── App/Window.*                  Win32 ウィンドウ（DirectX を知らない）
│   ├── Common/FrameTimer.*           フレーム時間の計測・FPS 表示
│   ├── Common/GraphicsCommon.*       ComPtr / DX_CHECK / ログ / パス解決
│   └── Graphics/
│       ├── GraphicsDevice.*          デバイス・GPU 選択・デバッグレイヤー
│       ├── CommandQueue.*            コマンドキュー・フェンス（同期）
│       ├── ConstantBuffer.*          定数バッファ（フレーム別・256B アライン）
│       ├── DepthBuffer.*             深度バッファ・DSV
│       ├── DescriptorHeap.*          シェーダー可視ディスクリプタヒープ
│       ├── Texture2D.*               テクスチャ・GPU 転送・SRV
│       ├── SwapChain.*               スワップチェーン・RTV
│       ├── TrianglePipeline.*        ルートシグネチャ・PSO・頂点/定数バッファ
│       └── Renderer.*                上記を束ねて 1 フレーム描く
├── shaders/Triangle.hlsl             頂点シェーダー・ピクセルシェーダー
├── tools/build.ps1                   MSBuild 呼び出しスクリプト
├── docs/                             学習資料（setup / concepts / architecture ほか）
└── .vscode/                          VSCode のビルド・デバッグ設定
```

---

## 進捗

- [x] **Step 1**: Win32 ウィンドウ表示
- [x] **Step 2**: DirectX 12 初期化（デバイス・キュー・スワップチェーン）
- [x] **Step 3**: 画面クリア
- [x] **Step 4**: 三角形の描画（頂点バッファ・PSO・シェーダー）
- [x] **Step 5**: フレームバッファリングによる CPU/GPU 並列化 ＋ FPS 計測
- [x] **Step 6**: 定数バッファと座標変換（回転する三角形）
- [x] **Step 7**: 深度バッファ（奥行き判定）
- [x] **Step 8**: テクスチャマッピング（ディスクリプタテーブル・ステージング転送）
- [ ] Step 9: 頂点バッファを DEFAULT ヒープへ転送
- [ ] Step 10: 3D 化（ビュー行列・透視投影）

各ステップで「なぜ現在の実装が単純化されているか」「どう改善するか」は
コード内コメントと [docs/03](docs/architecture/03_初期化と1フレームの流れ.md) に記載しています。

---

## ブランチ運用

git-flow に従います。詳細は [docs/conventions/06_Git運用ルール.md](docs/conventions/06_Git運用ルール.md)。

- `main` … リリース可能な状態
- `develop` … 開発の統合先
- `feature/*` … 機能単位の作業ブランチ（**マージ後に必ず削除**）
