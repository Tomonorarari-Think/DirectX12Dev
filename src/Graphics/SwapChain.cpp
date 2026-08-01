//=============================================================================
// SwapChain.cpp
//   SwapChain の実装。
//=============================================================================
#include "SwapChain.h"

#include <format>

namespace dx12
{
/// <summary>
/// スワップチェーンと RTV ディスクリプタヒープを生成します。
/// </summary>
void SwapChain::Initialize(IDXGIFactory6* factory,
                           ID3D12Device* device,
                           ID3D12CommandQueue* commandQueue,
                           HWND hwnd,
                           uint32_t width,
                           uint32_t height)
{
    m_width  = width;
    m_height = height;

    // (1) スワップチェーンの設定
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width  = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = kBackBufferFormat;

    // Stereo : 立体視（左右の目で別の絵）を使うか。今回は不要。
    swapChainDesc.Stereo = FALSE;

    // SampleDesc : マルチサンプルアンチエイリアス (MSAA) の設定。
    swapChainDesc.SampleDesc.Count   = 1;
    swapChainDesc.SampleDesc.Quality = 0;

    // BufferUsage : このバッファを何に使うか。描画先なのでレンダーターゲット出力。
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    swapChainDesc.BufferCount = kBackBufferCount;

    // Scaling : ウィンドウとバッファのサイズが食い違ったときの引き伸ばし方。
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;

    // SwapEffect : バッファの入れ替え方式。DirectX 12 では最重要の設定のひとつ。
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags     = 0;

    // (2) スワップチェーンの生成
    //   ★ 第 1 引数がデバイスではなく「コマンドキュー」であることに注目。
    ComPtr<IDXGISwapChain1> swapChain1;
    DX_CHECK(factory->CreateSwapChainForHwnd(
        commandQueue,
        hwnd,
        &swapChainDesc,
        nullptr,      // フルスクリーン設定（nullptr = ウィンドウモード）
        nullptr,      // 出力先モニタの制限（nullptr = 制限なし）
        &swapChain1));

    // (3) 新しい版のインターフェースへ変換
    //   CreateSwapChainForHwnd が返すのは IDXGISwapChain1 ですが、
    //   GetCurrentBackBufferIndex() を持つのは IDXGISwapChain3 以降です。
    DX_CHECK(swapChain1.As(&m_swapChain));

    // (4) Alt + Enter による自動フルスクリーン切り替えを無効化する
    //   DXGI は既定で Alt+Enter を横取りし、勝手にフルスクリーンにします。
    DX_CHECK(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

    // (5) RTV 用ディスクリプタヒープの生成
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = kBackBufferCount;

    // Flags : SHADER_VISIBLE を付けると「シェーダーから参照できるヒープ」になる。
    rtvHeapDesc.Flags    = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;

    DX_CHECK(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

    // ディスクリプタ 1 個のサイズを取得しておく（GPU ごとに異なる）
    m_rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // (6) バックバッファを取得して RTV を作る
    CreateRenderTargetViews(device);

    Log(std::format(L"スワップチェーンを生成しました ({} x {}, バッファ {} 枚)",
                    m_width, m_height, kBackBufferCount));
}


/// <summary>
/// バックバッファを取得し、それぞれの RTV をヒープに書き込みます。
/// </summary>
void SwapChain::CreateRenderTargetViews(ID3D12Device* device)
{
    // ディスクリプタヒープの先頭位置を取得する
    //   D3D12_CPU_DESCRIPTOR_HANDLE は実質的に「メモリアドレスを包んだ構造体」で、
    //   .ptr メンバに SIZE_T のアドレスが入っています。
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < kBackBufferCount; ++i)
    {
        // スワップチェーンが内部で確保しているバッファへの参照を取得する。
        DX_CHECK(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])));

        // RTV を作成して、ヒープの i 番目に書き込む。
        //   ★ nullptr（リソースと同じ形式）ではなく、明示的に sRGB の形式を渡す。
        //     こうすると「書き込むときだけ sRGB へ変換する」見方になる。
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format        = kRenderTargetViewFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        device->CreateRenderTargetView(m_backBuffers[i].Get(), &rtvDesc, rtvHandle);

        // 次のディスクリプタの位置へ進める
        rtvHandle.ptr += m_rtvDescriptorSize;
    }

    // 現在のバックバッファ番号を DXGI から取得して同期しておく
    m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}


/// <summary>
/// 現在のバックバッファに対応する RTV ディスクリプタの位置を返します。
/// </summary>
D3D12_CPU_DESCRIPTOR_HANDLE SwapChain::CurrentRenderTargetView() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    // 先頭 + サイズ × 現在の番号
    handle.ptr += static_cast<SIZE_T>(m_currentBackBufferIndex) * m_rtvDescriptorSize;

    return handle;
}


/// <summary>
/// 画面に表示し、バックバッファを入れ替えます。
/// </summary>
void SwapChain::Present(bool enableVSync)
{
    // 第 1 引数 SyncInterval : 垂直同期の待ちフレーム数
    //   1 … モニタのリフレッシュ 1 回ぶん待つ（60Hz なら最大 60fps）
    const UINT syncInterval = enableVSync ? 1 : 0;

    DX_CHECK(m_swapChain->Present(syncInterval, 0));

    // 表示が終わったので、次に描くべきバッファ番号を更新する。
    m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}


/// <summary>
/// ウィンドウサイズ変更に追従してバックバッファを作り直します。
/// </summary>
void SwapChain::Resize(ID3D12Device* device, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return; // 最小化時など。0 サイズでは作り直せない。
    }

    if (width == m_width && height == m_height)
    {
        return; // 変化なし
    }

    // ★ 重要 : ResizeBuffers を呼ぶ前に、バックバッファへの参照を
    //          「すべて」手放さなければなりません。
    for (auto& backBuffer : m_backBuffers)
    {
        backBuffer.Reset();
    }

    // バッファを新しいサイズで作り直す
    //   0 を渡した引数は「現在の設定を維持する」という意味になります。
    DX_CHECK(m_swapChain->ResizeBuffers(
        kBackBufferCount,   // バッファ枚数（0 なら現状維持）
        width,
        height,
        kBackBufferFormat,  // 形式（DXGI_FORMAT_UNKNOWN なら現状維持）
        0));                // フラグ

    m_width  = width;
    m_height = height;

    // 新しいバッファに対して RTV を作り直す
    CreateRenderTargetViews(device);

    Log(std::format(L"スワップチェーンをリサイズしました ({} x {})", m_width, m_height));
}

} // namespace dx12
