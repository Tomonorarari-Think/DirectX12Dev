//=============================================================================
// DepthBuffer.cpp
//   DepthBuffer の実装。
//=============================================================================
#include "DepthBuffer.h"

#include <format>

namespace dx12
{

/// @brief DSV ディスクリプタヒープと深度バッファ本体を生成します。
void DepthBuffer::Initialize(ID3D12Device* device, uint32_t width, uint32_t height)
{
    //-------------------------------------------------------------------------
    // (1) DSV 用ディスクリプタヒープの生成
    //
    //   ディスクリプタヒープは「種類ごと」に用意する決まりです。
    //   RTV 用のヒープ（SwapChain が持っている）に DSV を混ぜて置くことはできません。
    //   今回置くのは深度バッファ 1 枚ぶんの DSV だけなので、要素数は 1。
    //-------------------------------------------------------------------------
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;

    // DSV は「描画先」の指定であってシェーダーが読むものではないため NONE。
    // （深度を後からテクスチャとして読みたい場合は SRV を別途作ります）
    dsvHeapDesc.Flags    = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;

    DX_CHECK(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    // (2) 深度バッファ本体と DSV
    CreateResourceAndView(device, width, height);

    Log(std::format(L"深度バッファを生成しました ({} x {}, D32_FLOAT)", width, height));
}


/// @brief 深度バッファのリソースを作り、DSV をヒープに書き込みます。
void DepthBuffer::CreateResourceAndView(ID3D12Device* device, uint32_t width, uint32_t height)
{
    m_width  = width;
    m_height = height;

    //-------------------------------------------------------------------------
    // ヒープの種類 : DEFAULT
    //
    //   深度バッファは CPU から読み書きしません。GPU が書いて GPU が読むだけです。
    //   そのため GPU 専用の最速メモリ（DEFAULT ヒープ）に置きます。
    //   頂点バッファや定数バッファで使った UPLOAD ヒープとは目的が違います。
    //-------------------------------------------------------------------------
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask     = 1;
    heapProperties.VisibleNodeMask      = 1;

    //-------------------------------------------------------------------------
    // リソースの形状 : 2D テクスチャ
    //
    //   バッファ（ただのバイト列）ではなく、画面と同じ縦横を持つ 2D テクスチャです。
    //   ★ ALLOW_DEPTH_STENCIL フラグを立てるのを忘れないこと。
    //     これが無いと「深度バッファとしては使えないリソース」になり、
    //     DSV の作成時にエラーになります。
    //-------------------------------------------------------------------------
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment          = 0;
    resourceDesc.Width              = width;
    resourceDesc.Height             = height;
    resourceDesc.DepthOrArraySize   = 1;
    resourceDesc.MipLevels          = 1;
    resourceDesc.Format             = kFormat;
    resourceDesc.SampleDesc.Count   = 1;   // MSAA 無し（バックバッファと合わせる）
    resourceDesc.SampleDesc.Quality = 0;

    // テクスチャのメモリ配置は GPU に最適な形へ任せる（UNKNOWN）。
    // バッファのときに ROW_MAJOR を指定したのとは対照的です。
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    //-------------------------------------------------------------------------
    // 最適化されたクリア値
    //
    //   「このリソースは、ほぼ必ずこの値でクリアされます」と事前に宣言します。
    //   GPU はその値に特化した高速なクリア処理（高速クリア）を使えるようになります。
    //
    //   ★ ここで宣言した値と、実際に ClearDepthStencilView へ渡す値が
    //     食い違うと、デバッグレイヤーが警告を出し、最適化も効きません。
    //     必ず同じ値（本プロジェクトでは kClearDepth）を使ってください。
    //-------------------------------------------------------------------------
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format               = kFormat;
    clearValue.DepthStencil.Depth   = kClearDepth;
    clearValue.DepthStencil.Stencil = 0;

    //-------------------------------------------------------------------------
    // 初期状態 : DEPTH_WRITE
    //
    //   深度バッファは常に「書き込み可能な深度バッファ」として使い続けるため、
    //   この状態のまま変更しません。つまりリソースバリアが不要です。
    //   （深度をテクスチャとして読みたくなったら、そのときだけ
    //     DEPTH_READ / PIXEL_SHADER_RESOURCE へ遷移させます）
    //-------------------------------------------------------------------------
    DX_CHECK(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_depthBuffer)));

    //-------------------------------------------------------------------------
    // 深度ステンシルビュー (DSV) の作成
    //
    //   RTV と同じく「このリソースを深度バッファとして見る」ための説明書です。
    //   RTV では第 2 引数に nullptr を渡して既定の設定に任せましたが、
    //   ここでは明示しています（学習のため。nullptr でも同じ結果になります）。
    //-------------------------------------------------------------------------
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format        = kFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    // Flags に READ_ONLY_DEPTH を指定すると「深度テストはするが書き込まない」
    // ビューになります。今回は書き込むので NONE。
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

    dsvDesc.Texture2D.MipSlice = 0;

    device->CreateDepthStencilView(
        m_depthBuffer.Get(),
        &dsvDesc,
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}


/// @brief ウィンドウサイズ変更に追従して深度バッファを作り直します。
void DepthBuffer::Resize(ID3D12Device* device, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return; // 最小化時など
    }

    if (width == m_width && height == m_height)
    {
        return; // 変化なし
    }

    // 古いリソースを手放してから作り直す。
    // 呼び出し側で GPU の完了を待っているため、ここで解放しても安全。
    m_depthBuffer.Reset();

    CreateResourceAndView(device, width, height);

    Log(std::format(L"深度バッファをリサイズしました ({} x {})", width, height));
}

} // namespace dx12
