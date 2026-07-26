//=============================================================================
// GraphicsDevice.cpp
//   GraphicsDevice の実装。DirectX 12 の土台づくり。
//=============================================================================
#include "GraphicsDevice.h"

// d3d12sdklayers.h : ID3D12Debug / ID3D12InfoQueue といったデバッグ機能の宣言。
//                    d3d12.h からは自動で読み込まれないため明示的に include する。
#include <d3d12sdklayers.h>

#include <format>

namespace dx12
{
namespace
{
/// @brief デバイス生成時に要求する最低限の機能レベル。
///
/// **機能レベルとは**
///
/// GPU が「どの世代の機能まで対応しているか」を段階で表したもの。`D3D_FEATURE_LEVEL_11_0` は
/// DirectX 11 世代（2009 年頃）の機能に相当します。
///
/// **D3D12 なのに 11_0 でよいのか？**
///
/// はい。「DirectX 12 API」と「GPU の機能レベル」は別物です。DirectX 12 は API の設計（CPU と GPU
/// の仕事の分け方）が新しいのであって、必ずしも最新機能を要求しません。11_0 にしておけば対応 GPU の
/// 幅が広がり、三角形を描くには十分です。
constexpr D3D_FEATURE_LEVEL kMinimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
} // namespace


/// @brief デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。
///
/// メンバは宣言と逆順に破棄されるため、device → adapter → factory の順に参照が外れていきます。
GraphicsDevice::~GraphicsDevice() = default;


/// @brief DirectX 12 の土台を初期化します。
void GraphicsDevice::Initialize()
{
    EnableDebugLayer();   // ← 必ずデバイス生成より前に呼ぶこと
    CreateFactory();
    SelectAdapter();
    CreateDevice();
    ConfigureInfoQueue();

    Log(L"DirectX 12 デバイスの初期化が完了しました。");
}


/// @brief デバッグレイヤーを有効化します（Debug ビルドのみ）。
///
/// 有効化に失敗するのは、Windows の「オプション機能」に *Graphics Tools* が入っていない場合です（設
/// 定 > システム > オプション機能 > 機能を追加）。その場合でもアプリは動作するため、例外を投げず警
/// 告ログに留めます。
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
    // デバッグレイヤーは非常に低速なため、製品版では必ず無効にします。
#endif
}


/// @brief DXGI ファクトリを生成します。
void GraphicsDevice::CreateFactory()
{
    UINT factoryFlags = 0;

#if defined(_DEBUG)
    if (m_debugLayerEnabled)
    {
        // DXGI 側にもデバッグ機能があり、スワップチェーン関連の誤用を検出する。
        // デバッグレイヤーが使える環境でのみ指定する（無い環境では生成が失敗する）。
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    //  CreateDXGIFactory2 の "2" はバージョン番号ではなく関数の世代。
    //  フラグを渡せるのがこの版だけなので、これを使います。
    //  IID_PPV_ARGS(&m_factory) は
    //      __uuidof(IDXGIFactory6), reinterpret_cast<void**>(&m_factory)
    //  を安全に一度に書くためのマクロです。
    //  型と GUID がずれる typo を防げるので、COM 生成では常にこれを使います。
    DX_CHECK(::CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)));

    Log(L"DXGI ファクトリを生成しました。");
}


/// @brief 描画に使う GPU（アダプタ）を選択します。
///
/// ノート PC などでは「CPU 内蔵 GPU（省電力・低性能）」と「専用 GPU（高性能）」が両方存在します。何
/// も考えずに 0 番を選ぶと内蔵 GPU が当たってしまうことがあるため、`EnumAdapterByGpuPreference` で
/// 「高性能優先」の順に並べて列挙します。
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

        //---------------------------------------------------------------------
        // 実際にデバイスを作れるか「試し打ち」する
        //   第 3・第 4 引数に「型 ID と nullptr」を渡すと、
        //   デバイスを実際には作らずに「作れるかどうか」だけを検査できます。
        //   これは DirectX でよく使われる作法です。
        //---------------------------------------------------------------------
        if (SUCCEEDED(::D3D12CreateDevice(adapter.Get(), kMinimumFeatureLevel, __uuidof(ID3D12Device), nullptr)))
        {
            m_adapter = adapter;

            // VRAM 量はバイト単位なので MB に直して表示する
            const uint64_t videoMemoryMB = desc.DedicatedVideoMemory / (1024ull * 1024ull);
            Log(std::format(L"使用する GPU : {} (VRAM {} MB)", desc.Description, videoMemoryMB));
            return;
        }
    }

    //-------------------------------------------------------------------------
    // 実 GPU が 1 つも使えなかった場合のフォールバック : WARP
    //   WARP (Windows Advanced Rasterization Platform) は CPU による
    //   ソフトウェア実装の Direct3D です。非常に低速ですが、
    //   GPU が無い仮想マシン等でも動作確認ができます。
    //-------------------------------------------------------------------------
    LogError(L"D3D12 対応 GPU が見つかりませんでした。WARP（CPU 実装）に切り替えます。");

    ComPtr<IDXGIAdapter> warpAdapter;
    DX_CHECK(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

    // IDXGIAdapter → IDXGIAdapter1 へインターフェースを問い合わせる（QueryInterface）
    DX_CHECK(warpAdapter.As(&m_adapter));
}


/// @brief 選択したアダプタから D3D12 デバイスを生成します。
void GraphicsDevice::CreateDevice()
{
    DX_CHECK(::D3D12CreateDevice(
        m_adapter.Get(),        // どの GPU を使うか
        kMinimumFeatureLevel,   // 最低限必要な機能レベル
        IID_PPV_ARGS(&m_device)));

    Log(L"D3D12 デバイスを生成しました。");
}


/// @brief デバッグメッセージの扱いを設定します（Debug ビルドのみ）。
///
/// `ID3D12InfoQueue` は、デバッグレイヤーが検出した問題を溜めておくキューです。`SetBreakOnSeverity`
/// を設定しておくと、深刻度の高いメッセージが出た瞬間にデバッガのブレークポイントが発動します。
///
/// これが極めて重要な理由: 設定しないと、エラーは出力ウィンドウに流れるだけで実行は続行されます。そ
/// の結果「原因の場所」ではなく「はるか後の別の場所」でクラッシュし、原因究明が非常に困難になります。
/// ブレークさせれば、問題を起こした API 呼び出しでその場で止まります。
void GraphicsDevice::ConfigureInfoQueue()
{
#if defined(_DEBUG)
    if (!m_debugLayerEnabled)
    {
        return;
    }

    ComPtr<ID3D12InfoQueue> infoQueue;

    // As() は QueryInterface のラッパー。
    // デバッグレイヤーが無効だと ID3D12InfoQueue は取得できないため、
    // 失敗しても処理を続行できるよう例外にはしない。
    if (FAILED(m_device.As(&infoQueue)))
    {
        return;
    }

    // CORRUPTION : メモリ破壊レベルの致命的問題
    // ERROR      : API の誤用（ほぼ確実にバグ）
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);

    // WARNING は「動くが推奨されない」レベル。
    // 学習中は止まりすぎると進めないので、ブレークはさせずログのみとする。
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

    Log(L"デバッグ情報キューを設定しました（ERROR 以上でブレークします）。");
#endif
}

} // namespace dx12
