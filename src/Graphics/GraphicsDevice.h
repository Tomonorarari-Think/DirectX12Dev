//=============================================================================
// GraphicsDevice.h
//   DirectX 12 の「土台」（デバッグレイヤー・DXGI・アダプタ・デバイス）。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// <summary>
/// DirectX 12 の土台（DXGI ファクトリ、GPU の選択、D3D12 デバイス）を作るクラス。
/// </summary>
class GraphicsDevice
{
public:
    /// <summary>
    /// 既定のコンストラクタ。まだ何も生成されません。
    /// </summary>
    GraphicsDevice() = default;

    /// <summary>
    /// デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。
    /// </summary>
    ~GraphicsDevice();

    /// <summary>
    /// コピー構築は禁止です（GPU リソースの二重解放を防ぐため）。
    /// </summary>
    GraphicsDevice(const GraphicsDevice&) = delete;

    /// <summary>
    /// コピー代入は禁止です。
    /// </summary>
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    /// <summary>
    /// DirectX 12 の土台を初期化します。
    /// </summary>
    /// <exception cref="HrException">
    /// DXGI ファクトリまたはデバイスの生成に失敗した場合。
    /// </exception>
    void Initialize();

    /// <summary>
    /// 生成済みの D3D12 デバイスを取得します。
    /// </summary>
    /// <returns>デバイスの生ポインタ（所有権は移動しません）。</returns>
    ID3D12Device* Device() const noexcept { return m_device.Get(); }

    /// <summary>
    /// 生成済みの DXGI ファクトリを取得します。
    /// </summary>
    /// <returns>ファクトリの生ポインタ（所有権は移動しません）。</returns>
    IDXGIFactory6* Factory() const noexcept { return m_factory.Get(); }

private:
    /// <summary>
    /// デバッグレイヤーを有効化します（Debug ビルドのみ）。
    /// </summary>
    void EnableDebugLayer();

    /// <summary>
    /// DXGI ファクトリを生成します。
    /// </summary>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void CreateFactory();

    /// <summary>
    /// 描画に使う GPU（アダプタ）を選択します。
    /// </summary>
    /// <exception cref="HrException">アダプタの列挙に失敗した場合。</exception>
    void SelectAdapter();

    /// <summary>
    /// 選択したアダプタから D3D12 デバイスを生成します。
    /// </summary>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void CreateDevice();

    /// <summary>
    /// この GPU が対応しているシェーダーモデルの最大値をログに出します。
    /// </summary>
    /// <remarks>
    /// DXC が出す DXIL はシェーダーモデル 6 以降です。GPU が対応していなければ
    /// PSO の生成が失敗するので、起動時に分かるようにしています。
    /// </remarks>
    void LogShaderModel();

    /// <summary>
    /// リソースの結び付けの段階（Tier）をログに出します。
    /// </summary>
    /// <remarks>
    /// Tier 3 だと、シェーダー可視ヒープに置けるディスクリプタの数が
    /// 実質メモリ次第になります。ビンドレスの前提です。
    /// </remarks>
    void LogResourceBindingTier();

    /// <summary>
    /// デバッグメッセージの扱いを設定します（Debug ビルドのみ）。
    /// </summary>
    void ConfigureInfoQueue();

private:
    /// <summary>
    /// DXGI の受付カウンター（アダプタ列挙・スワップチェーン生成に使う）。
    /// </summary>
    ComPtr<IDXGIFactory6> m_factory;

    /// <summary>
    /// 選択した GPU。
    /// </summary>
    ComPtr<IDXGIAdapter1> m_adapter;

    /// <summary>
    /// D3D12 本体。あらゆる生成処理の起点。
    /// </summary>
    ComPtr<ID3D12Device> m_device;

    /// <summary>
    /// デバッグレイヤーを有効化できたか（Release ビルドでは常に false）。
    /// </summary>
    bool m_debugLayerEnabled = false;
};

} // namespace dx12
