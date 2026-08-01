//=============================================================================
// GraphicsDevice.cpp
//   GraphicsDevice の実装。DirectX 12 の土台づくり。
//=============================================================================
#include "GraphicsDevice.h"

// d3d12sdklayers.h : ID3D12Debug / ID3D12InfoQueue といったデバッグ機能の宣言。
#include <d3d12sdklayers.h>

#include <format>

namespace dx12
{
namespace
{
/// <summary>
/// デバイス生成時に要求する最低限の機能レベル。
/// </summary>
constexpr D3D_FEATURE_LEVEL kMinimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
} // namespace


/// <summary>
/// デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。
/// </summary>
GraphicsDevice::~GraphicsDevice() = default;


/// <summary>
/// DirectX 12 の土台を初期化します。
/// </summary>
void GraphicsDevice::Initialize()
{
    EnableDebugLayer();   // ← 必ずデバイス生成より前に呼ぶこと
    CreateFactory();
    SelectAdapter();
    CreateDevice();
    ConfigureInfoQueue();

    Log(L"DirectX 12 デバイスの初期化が完了しました。");
}


/// <summary>
/// デバッグレイヤーを有効化します（Debug ビルドのみ）。
/// </summary>
void GraphicsDevice::EnableDebugLayer()
{
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;

    // ここは DX_CHECK を使わない。失敗しても続行したいため、自前で判定する。
    const HRESULT hr = ::D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
    if (FAILED(hr))
    {
        LogError(L"デバッグレイヤーを有効化できませんでした。");
        LogError(L"  Windows の「オプション機能」から Graphics Tools を"
                 L"インストールすると利用できます。");
        return;
    }

    debugController->EnableDebugLayer();
    m_debugLayerEnabled = true;

    Log(L"デバッグレイヤーを有効化しました（Debug ビルド）。");
#else
    // Release ビルドでは何もしない。
#endif
}


/// <summary>
/// DXGI ファクトリを生成します。
/// </summary>
void GraphicsDevice::CreateFactory()
{
    UINT factoryFlags = 0;

#if defined(_DEBUG)
    if (m_debugLayerEnabled)
    {
        // DXGI 側にもデバッグ機能があり、スワップチェーン関連の誤用を検出する。
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    //  CreateDXGIFactory2 の "2" はバージョン番号ではなく関数の世代。
    DX_CHECK(::CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)));

    Log(L"DXGI ファクトリを生成しました。");
}


/// <summary>
/// 描画に使う GPU（アダプタ）を選択します。
/// </summary>
void GraphicsDevice::SelectAdapter()
{
    for (UINT index = 0;; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;

        // DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : 高性能な GPU から順に返す
        const HRESULT hr = m_factory->EnumAdapterByGpuPreference(
            index,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter));

        if (hr == DXGI_ERROR_NOT_FOUND)
        {
            break; // すべて列挙し終えた
        }
        DX_CHECK(hr);

        DXGI_ADAPTER_DESC1 desc = {};
        DX_CHECK(adapter->GetDesc1(&desc));

        // ソフトウェアレンダラ（WARP）は非常に遅いので、まずは実 GPU を優先する
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            continue;
        }

        // 実際にデバイスを作れるか「試し打ち」する
        //   第 3・第 4 引数に「型 ID と nullptr」を渡すと、
        //   デバイスを実際には作らずに「作れるかどうか」だけを検査できます。
        if (SUCCEEDED(::D3D12CreateDevice(adapter.Get(), kMinimumFeatureLevel,
                                          __uuidof(ID3D12Device), nullptr)))
        {
            m_adapter = adapter;

            // VRAM 量はバイト単位なので MB に直して表示する
            const uint64_t videoMemoryMB = desc.DedicatedVideoMemory / (1024ull * 1024ull);
            Log(std::format(L"使用する GPU : {} (VRAM {} MB)",
                            desc.Description, videoMemoryMB));
            return;
        }
    }

    // 実 GPU が 1 つも使えなかった場合のフォールバック : WARP
    //   WARP (Windows Advanced Rasterization Platform) は CPU による
    LogError(L"D3D12 対応 GPU が見つかりませんでした。WARP（CPU 実装）に切り替えます。");

    ComPtr<IDXGIAdapter> warpAdapter;
    DX_CHECK(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

    // IDXGIAdapter → IDXGIAdapter1 へインターフェースを問い合わせる（QueryInterface）
    DX_CHECK(warpAdapter.As(&m_adapter));
}


/// <summary>
/// 選択したアダプタから D3D12 デバイスを生成します。
/// </summary>
void GraphicsDevice::CreateDevice()
{
    DX_CHECK(::D3D12CreateDevice(
        m_adapter.Get(),        // どの GPU を使うか
        kMinimumFeatureLevel,   // 最低限必要な機能レベル
        IID_PPV_ARGS(&m_device)));

    Log(L"D3D12 デバイスを生成しました。");

    LogShaderModel();
}


/// <summary>
/// この GPU が対応しているシェーダーモデルの最大値をログに出します。
/// </summary>
void GraphicsDevice::LogShaderModel()
{
    // ★ 高いほうから順に聞いていく。
    //   対応していない値を渡すと E_INVALIDARG が返るので、下げながら試す。
    //   ランタイムが知らない新しい値でも同じく失敗するため、この形になる。
    constexpr D3D_SHADER_MODEL kCandidates[] = {
        D3D_SHADER_MODEL_6_7,
        D3D_SHADER_MODEL_6_6,
        D3D_SHADER_MODEL_6_5,
        D3D_SHADER_MODEL_6_4,
        D3D_SHADER_MODEL_6_3,
        D3D_SHADER_MODEL_6_2,
        D3D_SHADER_MODEL_6_1,
        D3D_SHADER_MODEL_6_0,
    };

    for (const D3D_SHADER_MODEL candidate : kCandidates)
    {
        D3D12_FEATURE_DATA_SHADER_MODEL data = {};
        data.HighestShaderModel = candidate;

        const HRESULT hr = m_device->CheckFeatureSupport(
            D3D12_FEATURE_SHADER_MODEL, &data, sizeof(data));

        if (SUCCEEDED(hr))
        {
            // 返ってくるのは「実際に使える最大値」で、渡した値とは限らない。
            const uint32_t major = (data.HighestShaderModel >> 4) & 0xF;
            const uint32_t minor = data.HighestShaderModel & 0xF;

            Log(std::format(L"対応シェーダーモデル : 最大 {}.{}", major, minor));

            // ★ ビンドレス（ResourceDescriptorHeap）は 6.6 以降。
            //   足りないと、PSO の生成ではなくシェーダーのコンパイルで落ちる。
            if (data.HighestShaderModel < D3D_SHADER_MODEL_6_6)
            {
                LogError(L"シェーダーモデル 6.6 に対応していません。"
                         L"材質のビンドレス参照が動きません。");
            }

            LogResourceBindingTier();
            return;
        }
    }

    LogError(L"シェーダーモデル 6 に対応していません。DXC のシェーダーは動きません。");
}


/// <summary>
/// リソースの結び付けの段階（Tier）をログに出します。
/// </summary>
void GraphicsDevice::LogResourceBindingTier()
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};

    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
                                             &options, sizeof(options))))
    {
        return;
    }

    // Tier 3 だと、ヒープに置けるディスクリプタの数が実質メモリ次第になる。
    // ビンドレスで何千枚ものテクスチャを扱うには、この段階が要る。
    const uint32_t tier = static_cast<uint32_t>(options.ResourceBindingTier);

    Log(std::format(L"リソース結び付けの段階 : Tier {}", tier));
}


/// <summary>
/// デバッグメッセージの扱いを設定します（Debug ビルドのみ）。
/// </summary>
void GraphicsDevice::ConfigureInfoQueue()
{
#if defined(_DEBUG)
    if (!m_debugLayerEnabled)
    {
        return;
    }

    ComPtr<ID3D12InfoQueue> infoQueue;

    // As() は QueryInterface のラッパー。
    if (FAILED(m_device.As(&infoQueue)))
    {
        return;
    }

    // CORRUPTION : メモリ破壊レベルの致命的問題
    // ERROR      : API の誤用（ほぼ確実にバグ）
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);

    // WARNING は「動くが推奨されない」レベル。
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

    Log(L"デバッグ情報キューを設定しました（ERROR 以上でブレークします）。");
#endif
}

} // namespace dx12
