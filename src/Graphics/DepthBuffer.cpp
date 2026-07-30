//=============================================================================
// DepthBuffer.cpp
//   DepthBuffer の実装。
//=============================================================================
#include "DepthBuffer.h"

#include "DescriptorHeap.h"

#include <format>

namespace dx12
{
namespace
{
/// <summary>書き込み可能な DSV のヒープ内番号。</summary>
constexpr uint32_t kWritableDsvIndex = 0;

/// <summary>書き込みを禁じた DSV のヒープ内番号。</summary>
constexpr uint32_t kReadOnlyDsvIndex = 1;
} // namespace


/// <summary>
/// DSV ディスクリプタヒープと深度バッファ本体を生成します。
/// </summary>
void DepthBuffer::Initialize(ID3D12Device* device, DescriptorHeap& descriptorHeap,
                             uint32_t width, uint32_t height)
{
    // (1) DSV 用ディスクリプタヒープの生成
    //   ディスクリプタヒープは「種類ごと」に用意する決まりです。
    //   ★ 2 個作ります。同じリソースに対して「書き込む DSV」と
    //     「読むだけの DSV」の 2 通りの見方を用意するためです。
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 2;

    // DSV は「描画先」の指定であってシェーダーが読むものではないため NONE。
    dsvHeapDesc.Flags    = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;

    DX_CHECK(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    m_dsvDescriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    // (2) SRV の置き場所を確保しておく
    //   リサイズでリソースを作り直しても、番号は変わりません。
    m_descriptorHeap          = &descriptorHeap;
    m_shaderResourceViewIndex = descriptorHeap.Allocate();
    m_shaderResourceView      = descriptorHeap.GpuHandle(m_shaderResourceViewIndex);

    // (3) 深度バッファ本体と各ビュー
    CreateResourceAndView(device, width, height);

    Log(std::format(L"深度バッファを生成しました ({} x {}, R32_TYPELESS)",
                    width, height));
}


/// <summary>
/// 書き込みを禁じた深度ステンシルビューの位置を取得します。
/// </summary>
D3D12_CPU_DESCRIPTOR_HANDLE DepthBuffer::ReadOnlyDepthStencilView() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    handle.ptr += static_cast<SIZE_T>(kReadOnlyDsvIndex) * m_dsvDescriptorSize;

    return handle;
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
    // ★ 型を決めない形式で作る。DSV では D32_FLOAT、SRV では R32_FLOAT と
    //   別の解釈を与えるためです。D32_FLOAT で作ると SRV を作れません。
    resourceDesc.Format             = kResourceFormat;
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
    //   ★ ソフトパーティクルを描くあいだだけ
    //     `DEPTH_READ | PIXEL_SHADER_RESOURCE` へ移し、描き終えたら戻します。
    //     それ以外の場面ではこの状態のままです。
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
    dsvDesc.Format             = kFormat;
    dsvDesc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags              = D3D12_DSV_FLAG_NONE;
    dsvDesc.Texture2D.MipSlice = 0;

    const D3D12_CPU_DESCRIPTOR_HANDLE dsvStart =
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_CPU_DESCRIPTOR_HANDLE writable = dsvStart;
    writable.ptr += static_cast<SIZE_T>(kWritableDsvIndex) * m_dsvDescriptorSize;

    device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc, writable);

    // ★ READ_ONLY_DEPTH を立てた 2 個目の DSV。
    //   「深度テストはするが書き込まない」ビューです。
    //   これがあると、同じ深度バッファを深度テストとテクスチャ読みに
    //   同時に使えます（[28 章](../../docs/tutorial/28_ソフトパーティクル.md)）。
    dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;

    D3D12_CPU_DESCRIPTOR_HANDLE readOnly = dsvStart;
    readOnly.ptr += static_cast<SIZE_T>(kReadOnlyDsvIndex) * m_dsvDescriptorSize;

    device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc, readOnly);

    // シェーダーリソースビュー (SRV) の作成
    //   同じリソースを「1 チャンネルの浮動小数テクスチャ」として読みます。
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = kShaderResourceViewFormat;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;

    device->CreateShaderResourceView(
        m_depthBuffer.Get(), &srvDesc,
        m_descriptorHeap->CpuHandle(m_shaderResourceViewIndex));
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
