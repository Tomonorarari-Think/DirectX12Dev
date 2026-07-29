//=============================================================================
// RenderTexture.cpp
//   RenderTexture の実装。
//=============================================================================
#include "RenderTexture.h"

#include "DescriptorHeap.h"

#include <format>

namespace dx12
{

/// <summary>
/// 描画先とテクスチャを兼ねる 1 枚を生成します。
/// </summary>
void RenderTexture::Initialize(ID3D12Device* device,
                               DescriptorHeap& descriptorHeap,
                               uint32_t width,
                               uint32_t height,
                               const std::wstring& debugName,
                               const float clearColor[4])
{
    m_width  = width;
    m_height = height;

    for (int i = 0; i < 4; ++i)
    {
        m_clearColor[i] = clearColor[i];
    }

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask     = 1;
    heapProperties.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width              = width;
    textureDesc.Height             = height;
    textureDesc.DepthOrArraySize   = 1;
    textureDesc.MipLevels          = 1;
    textureDesc.Format             = kFormat;
    textureDesc.SampleDesc.Count   = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    // ★ 「描画先にもする」と宣言しておく。あとから付け足すことはできない。
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    // 最適化クリア値。ここで宣言した値と、実際に ClearRenderTargetView へ
    // 渡す値が食い違うと、デバッグレイヤーが警告を出す（性能も落ちる）。
    D3D12_CLEAR_VALUE optimizedClearValue = {};
    optimizedClearValue.Format = kFormat;
    for (int i = 0; i < 4; ++i)
    {
        optimizedClearValue.Color[i] = clearColor[i];
    }

    DX_CHECK(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &optimizedClearValue,
        IID_PPV_ARGS(&m_texture)));

    m_texture->SetName(debugName.c_str());

    // --- RTV ---
    //   ★ RTV は「シェーダーから見えないヒープ」に置く決まり。
    //     SRV のヒープには入れられないので、1 個だけの専用ヒープを持つ。
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    DX_CHECK(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format             = kFormat;
    rtvDesc.ViewDimension      = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    device->CreateRenderTargetView(m_texture.Get(), &rtvDesc,
                                   m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // --- SRV ---
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                        = kFormat;
    srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip     = 0;
    srvDesc.Texture2D.MipLevels           = 1;
    srvDesc.Texture2D.PlaneSlice          = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    const uint32_t descriptorIndex = descriptorHeap.Allocate();
    device->CreateShaderResourceView(m_texture.Get(), &srvDesc,
                                     descriptorHeap.CpuHandle(descriptorIndex));

    m_srvGpuHandle = descriptorHeap.GpuHandle(descriptorIndex);

    Log(std::format(L"描画先テクスチャを生成しました（{} : {} x {}, R16G16B16A16_FLOAT）",
                    debugName, width, height));
}


/// <summary>
/// 描画先として使う準備をします。
/// </summary>
void RenderTexture::BeginRender(ID3D12GraphicsCommandList* commandList,
                                const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencilView)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = m_texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

    commandList->ResourceBarrier(1, &barrier);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, depthStencilView);
    commandList->ClearRenderTargetView(rtvHandle, m_clearColor, 0, nullptr);
}


/// <summary>
/// 描画を終え、テクスチャとして読める状態へ戻します。
/// </summary>
void RenderTexture::EndRender(ID3D12GraphicsCommandList* commandList)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = m_texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    commandList->ResourceBarrier(1, &barrier);
}

} // namespace dx12
