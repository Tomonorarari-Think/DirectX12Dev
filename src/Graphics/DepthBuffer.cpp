//=============================================================================
// DepthBuffer.cpp
//   DepthBuffer の実装。
//=============================================================================
#include "DepthBuffer.h"

#include <format>

namespace dx12
{

/// <summary>
/// DSV ディスクリプタヒープと深度バッファ本体を生成します。
/// </summary>
void DepthBuffer::Initialize(ID3D12Device* device, uint32_t width, uint32_t height)
{
    // (1) DSV 用ディスクリプタヒープの生成
    //   ディスクリプタヒープは「種類ごと」に用意する決まりです。
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;

    // DSV は「描画先」の指定であってシェーダーが読むものではないため NONE。
    dsvHeapDesc.Flags    = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;

    DX_CHECK(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    // (2) 深度バッファ本体と DSV
    CreateResourceAndView(device, width, height);

    Log(std::format(L"深度バッファを生成しました ({} x {}, D32_FLOAT)", width, height));
}


/// <summary>
/// 深度バッファのリソースを作り、DSV をヒープに書き込みます。
/// </summary>
void DepthBuffer::CreateResourceAndView(ID3D12Device* device, uint32_t width, uint32_t height)
{
    m_width  = width;
    m_height = height;

    // ヒープの種類 : DEFAULT
    //   深度バッファは CPU から読み書きしません。GPU が書いて GPU が読むだけです。
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask     = 1;
    heapProperties.VisibleNodeMask      = 1;

    // リソースの形状 : 2D テクスチャ
    //   バッファ（ただのバイト列）ではなく、画面と同じ縦横を持つ 2D テクスチャです。
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
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    // 最適化されたクリア値
    //   「このリソースは、ほぼ必ずこの値でクリアされます」と事前に宣言します。
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format               = kFormat;
    clearValue.DepthStencil.Depth   = kClearDepth;
    clearValue.DepthStencil.Stencil = 0;

    // 初期状態 : DEPTH_WRITE
    //   深度バッファは常に「書き込み可能な深度バッファ」として使い続けるため、
    //   この状態のまま変更しません。つまりリソースバリアが不要です。
    DX_CHECK(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_depthBuffer)));

    // 深度ステンシルビュー (DSV) の作成
    //   RTV と同じく「このリソースを深度バッファとして見る」ための説明書です。
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


/// <summary>
/// ウィンドウサイズ変更に追従して深度バッファを作り直します。
/// </summary>
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
    m_depthBuffer.Reset();

    CreateResourceAndView(device, width, height);

    Log(std::format(L"深度バッファをリサイズしました ({} x {})", width, height));
}

} // namespace dx12
