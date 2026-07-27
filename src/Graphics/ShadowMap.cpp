//=============================================================================
// ShadowMap.cpp
//   ShadowMap の実装。
//=============================================================================
#include "ShadowMap.h"

#include "DescriptorHeap.h"

#include <cmath>   // std::abs
#include <format>

using namespace DirectX;

namespace dx12
{

/// <summary>
/// シャドウマップ本体・DSV・SRV を生成します。
/// </summary>
void ShadowMap::Initialize(ID3D12Device* device, DescriptorHeap& descriptorHeap, uint32_t size)
{
    m_size = size;

    // (1) DSV 用ディスクリプタヒープ
    //   画面用の深度バッファとは別のリソースなので、DSV も別に必要。
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask       = 0;

    DX_CHECK(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    // (2) リソース本体
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask     = 1;
    heapProperties.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment          = 0;
    resourceDesc.Width              = size;
    resourceDesc.Height             = size;
    resourceDesc.DepthOrArraySize   = 1;
    resourceDesc.MipLevels          = 1;

    // ★ TYPELESS で作るのがポイント。ビューごとに別の形式で解釈する。
    resourceDesc.Format             = kResourceFormat;

    resourceDesc.SampleDesc.Count   = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    // 最適化されたクリア値。DSV と同じ形式で書く必要がある。
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format               = kDepthStencilViewFormat;
    clearValue.DepthStencil.Depth   = kClearDepth;
    clearValue.DepthStencil.Stencil = 0;

    // 初期状態はテクスチャとして読める状態にしておく。
    // 毎フレーム BeginRender が「テクスチャ → 深度バッファ」のバリアから始まるため。
    DX_CHECK(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&m_shadowMap)));

    // (3) 深度ステンシルビュー（書き込み用の見方）
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format             = kDepthStencilViewFormat;
    dsvDesc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags              = D3D12_DSV_FLAG_NONE;
    dsvDesc.Texture2D.MipSlice = 0;

    device->CreateDepthStencilView(
        m_shadowMap.Get(), &dsvDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

    // (4) シェーダーリソースビュー（読み込み用の見方）
    //   同じリソースを R32_FLOAT の 1 チャンネルテクスチャとして見る。
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = kShaderResourceViewFormat;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;

    const uint32_t descriptorIndex = descriptorHeap.Allocate();

    device->CreateShaderResourceView(
        m_shadowMap.Get(), &srvDesc, descriptorHeap.CpuHandle(descriptorIndex));

    m_shaderResourceView = descriptorHeap.GpuHandle(descriptorIndex);

    // (5) ビューポートとシザー矩形
    //   画面ではなくシャドウマップ全体を描画範囲にする。
    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;
    m_viewport.Width    = static_cast<float>(size);
    m_viewport.Height   = static_cast<float>(size);
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;

    m_scissorRect.left   = 0;
    m_scissorRect.top    = 0;
    m_scissorRect.right  = static_cast<LONG>(size);
    m_scissorRect.bottom = static_cast<LONG>(size);

    Log(std::format(L"シャドウマップを生成しました ({} x {}, R32_TYPELESS)", size, size));
}


/// <summary>
/// 光源をカメラに見立てた変換行列を計算します。
/// </summary>
void ShadowMap::SetLight(const XMFLOAT3& lightDirection,
                         const XMFLOAT3& sceneCenter,
                         float sceneRadius)
{
    const XMVECTOR direction = XMVector3Normalize(XMLoadFloat3(&lightDirection));
    const XMVECTOR center    = XMLoadFloat3(&sceneCenter);

    // 平行光源に位置はないので、範囲の外まで下がった所へ仮の視点を置く。
    // 光の進む向きと逆に、半径の 2 倍だけ戻す。
    const XMVECTOR eye = XMVectorSubtract(center, XMVectorScale(direction, sceneRadius * 2.0f));

    // 上方向が光の向きと平行だと LookAt が破綻するので、その場合だけ別の軸を使う。
    const float verticalDot = std::abs(XMVectorGetY(direction));
    const XMVECTOR up = (verticalDot > 0.99f) ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)
                                              : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const XMMATRIX view = XMMatrixLookAtLH(eye, center, up);

    // ★ 平行光源なので正射影を使う。透視投影にすると、
    //   光源に近い物ほど影が大きくなってしまう。
    const float extent = sceneRadius * 2.0f;

    const XMMATRIX projection =
        XMMatrixOrthographicLH(extent, extent, 0.1f, sceneRadius * 4.0f);

    XMStoreFloat4x4(&m_lightViewProjection, view * projection);
}


/// <summary>
/// 光源から見たビュー行列 × 射影行列を返します。
/// </summary>
XMMATRIX ShadowMap::LightViewProjection() const
{
    return XMLoadFloat4x4(&m_lightViewProjection);
}


/// <summary>
/// シャドウマップへの描き込みを開始します。
/// </summary>
void ShadowMap::BeginRender(ID3D12GraphicsCommandList* commandList) const
{
    // (1) バリア : テクスチャ → 深度バッファ
    //   前フレームの最後はピクセルシェーダーが読む状態で終わっている。
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = m_shadowMap.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    commandList->ResourceBarrier(1, &barrier);

    // (2) 描画範囲はシャドウマップ全体。画面の解像度とは無関係。
    commandList->RSSetViewports(1, &m_viewport);
    commandList->RSSetScissorRects(1, &m_scissorRect);

    // (3) 描画先の設定
    //   ★ 色は要らないのでレンダーターゲットは 0 枚。深度だけを描く。
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);

    // (4) 一番奥の値で埋める
    commandList->ClearDepthStencilView(
        dsvHandle, D3D12_CLEAR_FLAG_DEPTH, kClearDepth, 0, 0, nullptr);
}


/// <summary>
/// シャドウマップへの描き込みを終え、テクスチャとして読める状態に戻します。
/// </summary>
void ShadowMap::EndRender(ID3D12GraphicsCommandList* commandList) const
{
    // バリア : 深度バッファ → テクスチャ
    //   これを忘れると、書き込み中のリソースを読むことになり
    //   デバッグレイヤーがエラーを出す。
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = m_shadowMap.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    commandList->ResourceBarrier(1, &barrier);
}

} // namespace dx12
