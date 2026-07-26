//=============================================================================
// GraphicsDevice.h
//   DirectX 12 の「土台」を作るクラス。
//
//   ■ 用語
//     ・DXGI ファクトリ (IDXGIFactory)
//         GPU（アダプタ）を列挙したり、スワップチェーンを作ったりする窓口。
//         「DirectX の受付カウンター」のような存在。
//
//     ・アダプタ (IDXGIAdapter)
//         1 個の GPU を表すオブジェクト。PC には
//         「CPU 内蔵 GPU」と「外付けの高性能 GPU」が両方載っていることがあり、
//         どちらを使うかをここで選びます。
//
//     ・デバイス (ID3D12Device)
//         選んだ GPU を操作するための本体。
//         「〜を作る」系の関数（CreateCommandQueue, CreateCommittedResource,
//         CreateGraphicsPipelineState …）はほぼ全てこのデバイスが持っています。
//         DirectX 12 のあらゆる処理はデバイスから始まる、と覚えてください。
//
//     ・デバッグレイヤー (ID3D12Debug)
//         DirectX 12 は性能最優先の API のため、既定では「間違った使い方」を
//         していても黙って壊れた絵を出す（あるいは即クラッシュする）だけです。
//         デバッグレイヤーを有効にすると、API の使い方の誤りを検査して
//         詳細なエラーメッセージを出力してくれます。
//         DirectX 12 学習において最も重要な機能と言っても過言ではありません。
//         ※ 動作は非常に遅くなるため、Debug ビルドでのみ有効にします。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{
class GraphicsDevice
{
public:
    GraphicsDevice() = default;
    ~GraphicsDevice();

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    //-------------------------------------------------------------------------
    // 初期化
    //   処理順序（この順番でなければならない）:
    //     1. デバッグレイヤーを有効化   ← デバイス生成より前でないと効かない
    //     2. DXGI ファクトリを生成
    //     3. 使用する GPU（アダプタ）を選択
    //     4. D3D12 デバイスを生成
    //     5. 情報キューを設定（エラー時にデバッガを止める）
    //-------------------------------------------------------------------------
    void Initialize();

    // --- 参照用アクセサ ------------------------------------------------------
    ID3D12Device*  Device()  const noexcept { return m_device.Get(); }
    IDXGIFactory6* Factory() const noexcept { return m_factory.Get(); }

private:
    // 1. デバッグレイヤーの有効化（Debug ビルドのみ実際に動く）
    void EnableDebugLayer();

    // 2. DXGI ファクトリの生成
    void CreateFactory();

    // 3. 描画に使う GPU を選ぶ
    void SelectAdapter();

    // 4. D3D12 デバイスの生成
    void CreateDevice();

    // 5. デバッグ用の情報キュー設定（Debug ビルドのみ）
    void ConfigureInfoQueue();

private:
    ComPtr<IDXGIFactory6> m_factory;      // DXGI の受付カウンター
    ComPtr<IDXGIAdapter1> m_adapter;      // 選択した GPU
    ComPtr<ID3D12Device>  m_device;       // D3D12 本体

    // デバッグレイヤーが有効化できたか（Release ビルドでは常に false）
    bool m_debugLayerEnabled = false;
};

} // namespace dx12
