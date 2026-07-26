//=============================================================================
// GraphicsDevice.h
//   DirectX 12 の「土台」（デバッグレイヤー・DXGI・アダプタ・デバイス）。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// @brief DirectX 12 の土台（DXGI ファクトリ、GPU の選択、D3D12 デバイス）を作るクラス。
///
/// **DXGI ファクトリ (IDXGIFactory)**
///
/// GPU（アダプタ）を列挙したり、スワップチェーンを作ったりする窓口。「DirectX の受付カウンター」の
/// ような存在です。
///
/// **アダプタ (IDXGIAdapter)**
///
/// 1 個の GPU を表すオブジェクト。PC には「CPU 内蔵 GPU」と「外付けの高性能 GPU」が両方載っているこ
/// とがあり、どちらを使うかをここで選びます。
///
/// **デバイス (ID3D12Device)**
///
/// 選んだ GPU を操作するための本体。「〜を作る」系の関数（`CreateCommandQueue`、`CreateCommittedRes
/// ource`、`CreateGraphicsPipelineState` …）はほぼ全てこのデバイスが持っています。DirectX 12 のあら
/// ゆる処理はデバイスから始まる、と覚えてください。
///
/// **デバッグレイヤー (ID3D12Debug)**
///
/// DirectX 12 は性能最優先の API のため、既定では「間違った使い方」をしていても黙って壊れた絵を出す
/// （あるいは即クラッシュする）だけです。デバッグレイヤーを有効にすると、API の使い方の誤りを検査し
/// て詳細なエラーメッセージを出力してくれます。DirectX 12 学習において最も重要な機能と言っても過言
/// ではありません。動作は非常に遅くなるため、Debug ビルドでのみ有効にします。
class GraphicsDevice
{
public:
    /// @brief 既定のコンストラクタ。まだ何も生成されません。
    GraphicsDevice() = default;

    /// @brief デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。
    ~GraphicsDevice();

    /// @brief コピー構築は禁止です（GPU リソースの二重解放を防ぐため）。
    GraphicsDevice(const GraphicsDevice&) = delete;

    /// @brief コピー代入は禁止です。
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    /// @brief DirectX 12 の土台を初期化します。
    /// @exception HrException DXGI ファクトリまたはデバイスの生成に失敗した場合。
    ///
    /// 処理順序（この順番でなければならない）:
    ///
    /// 1. デバッグレイヤーを有効化 … デバイス生成より前でないと効かない
    /// 2. DXGI ファクトリを生成
    /// 3. 使用する GPU（アダプタ）を選択
    /// 4. D3D12 デバイスを生成
    /// 5. 情報キューを設定（エラー時にデバッガを止める）
    void Initialize();

    /// @brief 生成済みの D3D12 デバイスを取得します。
    /// @returns デバイスの生ポインタ（所有権は移動しません）。
    ID3D12Device* Device() const noexcept { return m_device.Get(); }

    /// @brief 生成済みの DXGI ファクトリを取得します。
    /// @returns ファクトリの生ポインタ（所有権は移動しません）。
    IDXGIFactory6* Factory() const noexcept { return m_factory.Get(); }

private:
    /// @brief デバッグレイヤーを有効化します（Debug ビルドのみ）。
    ///
    /// **なぜデバイス生成より前でなければならないのか**
    ///
    /// デバッグレイヤーは「デバイス生成時に、検証機能付きの実装に差し替える」という仕組みで働きます。デ
    /// バイスを作った後に有効化しても、そのデバイスは通常版のままなので何も検証されません。
    ///
    /// Windows の「オプション機能」に *Graphics Tools* が入っていないと有効化に失敗します。その場合でも
    /// アプリは動作するため、例外は投げず警告ログに留めます。
    void EnableDebugLayer();

    /// @brief DXGI ファクトリを生成します。
    /// @exception HrException 生成に失敗した場合。
    void CreateFactory();

    /// @brief 描画に使う GPU（アダプタ）を選択します。
    /// @exception HrException アダプタの列挙に失敗した場合。
    ///
    /// 高性能な GPU から順に試し、D3D12 デバイスを作れる最初の 1 台を採用します。実 GPU が 1 つも使えな
    /// い場合は WARP（CPU によるソフトウェア実装）へ切り替えます。
    void SelectAdapter();

    /// @brief 選択したアダプタから D3D12 デバイスを生成します。
    /// @exception HrException 生成に失敗した場合。
    void CreateDevice();

    /// @brief デバッグメッセージの扱いを設定します（Debug ビルドのみ）。
    ///
    /// 深刻度が ERROR 以上のメッセージが出た瞬間にデバッガを停止させます。設定しないと、エラーは出力ウ
    /// ィンドウに流れるだけで実行が続行され、「原因の場所」ではなく「はるか後の別の場所」でクラッシュし
    /// ます。
    void ConfigureInfoQueue();

private:
    /// @brief DXGI の受付カウンター（アダプタ列挙・スワップチェーン生成に使う）。
    ComPtr<IDXGIFactory6> m_factory;

    /// @brief 選択した GPU。
    ComPtr<IDXGIAdapter1> m_adapter;

    /// @brief D3D12 本体。あらゆる生成処理の起点。
    ComPtr<ID3D12Device> m_device;

    /// @brief デバッグレイヤーを有効化できたか（Release ビルドでは常に false）。
    bool m_debugLayerEnabled = false;
};

} // namespace dx12
