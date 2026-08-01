//=============================================================================
// ShadowMap.cpp
//   ShadowMap の実装。
//=============================================================================
#include "ShadowMap.h"

#include "DescriptorHeap.h"

#include <algorithm>   // std::min, std::max
#include <cassert>
#include <cmath>       // std::abs, std::pow, std::ceil
#include <format>

using namespace DirectX;

namespace dx12
{
namespace
{
/// <summary>
/// 段の切れ目を決めるときの、対数分割の混ぜ具合。
/// </summary>
/// <remarks>
/// 0 で等間隔、1 で完全な対数分割。手前を細かくしたいので対数寄りにしますが、
/// 1 にすると奥の段が広くなりすぎます。
/// </remarks>
constexpr float kSplitBlend = 0.75f;
} // namespace

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
    // ★ 段ごとに 1 個ずつ要る。書き込む先の「切れ端」を指すため。
    dsvHeapDesc.NumDescriptors = kCascadeCount;
    dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask       = 0;

    DX_CHECK(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    m_dsvDescriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

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
    // ★ 1 枚のテクスチャ配列にする。段の数だけ「層」を持つ。
    resourceDesc.DepthOrArraySize   = static_cast<UINT16>(kCascadeCount);
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
    //   ★ 段ごとに「配列の何層目を描くか」を指す DSV を 1 個ずつ作る。
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format        = kDepthStencilViewFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    dsvDesc.Flags         = D3D12_DSV_FLAG_NONE;

    dsvDesc.Texture2DArray.MipSlice        = 0;
    dsvDesc.Texture2DArray.ArraySize       = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade)
    {
        dsvDesc.Texture2DArray.FirstArraySlice = cascade;

        device->CreateDepthStencilView(m_shadowMap.Get(), &dsvDesc, dsvHandle);

        dsvHandle.ptr += m_dsvDescriptorSize;
    }

    // (4) シェーダーリソースビュー（読み込み用の見方）
    //   同じリソースを R32_FLOAT の 1 チャンネルテクスチャとして見る。
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = kShaderResourceViewFormat;
    //   ★ 読むほうは配列まるごと 1 個の SRV。シェーダーは層番号で選ぶ。
    srvDesc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Shader4ComponentMapping        = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2DArray.MipLevels       = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize       = kCascadeCount;

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
/// カメラの視錐台を段に切り、段ごとの光源行列を計算します。
/// </summary>
void ShadowMap::SetLight(const XMFLOAT3& lightDirection,
                         const Camera& camera,
                         float shadowDistance)
{
    const XMVECTOR direction = XMVector3Normalize(XMLoadFloat3(&lightDirection));

    const float nearPlane = camera.NearPlane();
    const float farPlane  = std::min(shadowDistance, camera.FarPlane());

    // --- (1) 段の切れ目を決める ---------------------------------------------
    //   ★ 等間隔だと、手前がすかすかで奥が足りない。
    //     対数分割は逆に手前へ寄りすぎる。両者を混ぜるのが定石。
    for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade)
    {
        const float ratio = static_cast<float>(cascade + 1)
                          / static_cast<float>(kCascadeCount);

        const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, ratio);
        const float uniform     = nearPlane + (farPlane - nearPlane) * ratio;

        m_cascadeSplits[cascade] =
            uniform + kSplitBlend * (logarithmic - uniform);
    }

    // --- (2) カメラの向きを求める -------------------------------------------
    const XMVECTOR eye     = XMLoadFloat3(&camera.Position());
    const XMVECTOR target  = XMLoadFloat3(&camera.Target());
    const XMVECTOR worldUp = XMLoadFloat3(&camera.Up());

    const XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, eye));
    const XMVECTOR right   = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    const XMVECTOR up      = XMVector3Cross(forward, right);

    const float tanHalfFovY = std::tan(camera.FieldOfView() * 0.5f);
    const float tanHalfFovX = tanHalfFovY * camera.AspectRatio();

    // 光源から見る向きが真上に近いと LookAt が破綻するので、その場合だけ別の軸。
    const float verticalDot = std::abs(XMVectorGetY(direction));
    const XMVECTOR lightUp = (verticalDot > 0.99f) ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)
                                                   : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    float sliceNear = nearPlane;

    for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade)
    {
        const float sliceFar = m_cascadeSplits[cascade];

        // --- (3) この段の視錐台を包む球を求める ------------------------------
        //   ★ 箱ではなく球で包むのが要点。球は回しても大きさが変わらないので、
        //     カメラを回しただけで影の解像度が変わる（ちらつく）ことがない。
        const XMVECTOR nearCenter = XMVectorAdd(eye, XMVectorScale(forward, sliceNear));
        const XMVECTOR farCenter  = XMVectorAdd(eye, XMVectorScale(forward, sliceFar));

        // 錐台の 8 隅のうち、最も遠い隅までの距離が半径になる。
        const XMVECTOR farCorner = XMVectorAdd(
            farCenter,
            XMVectorAdd(XMVectorScale(right, sliceFar * tanHalfFovX),
                        XMVectorScale(up,    sliceFar * tanHalfFovY)));

        const XMVECTOR nearCorner = XMVectorAdd(
            nearCenter,
            XMVectorAdd(XMVectorScale(right, sliceNear * tanHalfFovX),
                        XMVectorScale(up,    sliceNear * tanHalfFovY)));

        // 球の中心は、2 つの隅を結ぶ線分の中点として求める。
        const XMVECTOR center = XMVectorScale(XMVectorAdd(nearCorner, farCorner), 0.5f);

        float radius = XMVectorGetX(XMVector3Length(XMVectorSubtract(farCorner, center)));
        radius = std::max(radius,
                          XMVectorGetX(XMVector3Length(
                              XMVectorSubtract(nearCorner, center))));

        // 端数で毎フレーム半径が揺れないよう、少しだけ切り上げて安定させる。
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // --- (4) 光源から見た行列を組む --------------------------------------
        const XMVECTOR lightEye =
            XMVectorSubtract(center, XMVectorScale(direction, radius * 2.0f));

        XMMATRIX view = XMMatrixLookAtLH(lightEye, center, lightUp);

        XMMATRIX projection =
            XMMatrixOrthographicLH(radius * 2.0f, radius * 2.0f, 0.1f, radius * 4.0f);

        // --- (5) テクセルの格子に合わせる ------------------------------------
        //   ★ これをしないと、カメラが少し動くだけで影の縁が沸き立つ。
        //     光源空間で「1 テクセルぶんに満たないずれ」を丸めて消す。
        XMMATRIX viewProjection = view * projection;

        const float texelsPerUnit = static_cast<float>(m_size) / (radius * 2.0f);

        XMVECTOR shadowOrigin = XMVector3TransformCoord(XMVectorZero(), viewProjection);
        shadowOrigin = XMVectorScale(shadowOrigin, static_cast<float>(m_size) * 0.5f);

        const XMVECTOR rounded = XMVectorRound(shadowOrigin);
        XMVECTOR offset = XMVectorSubtract(rounded, shadowOrigin);
        offset = XMVectorScale(offset, 2.0f / static_cast<float>(m_size));

        projection.r[3] = XMVectorAdd(
            projection.r[3],
            XMVectorSet(XMVectorGetX(offset), XMVectorGetY(offset), 0.0f, 0.0f));

        XMStoreFloat4x4(&m_lightViewProjection[cascade], view * projection);

        // 段の受け持ち範囲と細かさを 1 度だけ記録する。
        //   ★ 「段に分けると手前が細かくなる」は、この数字で確かめられる。
        if (!m_logged)
        {
            Log(std::format(L"  段 {} : 距離 {:.2f} 〜 {:.2f} / 半径 {:.2f} / "
                            L"1 ワールド単位あたり {:.0f} テクセル",
                            cascade, sliceNear, sliceFar, radius,
                            static_cast<float>(m_size) / (radius * 2.0f)));
        }

        // 次の段は、この段の終わりから始める。
        sliceNear = sliceFar;

        // 静的解析よけ。texelsPerUnit は説明のために求めているだけ。
        (void)texelsPerUnit;
    }

    m_logged = true;
}


/// <summary>
/// 指定した段の、光源から見たビュー行列 × 射影行列を返します。
/// </summary>
XMMATRIX ShadowMap::LightViewProjection(uint32_t cascadeIndex) const
{
    assert(cascadeIndex < kCascadeCount);
    return XMLoadFloat4x4(&m_lightViewProjection[cascadeIndex]);
}


/// <summary>
/// 段の切れ目（カメラからの距離）を返します。
/// </summary>
float ShadowMap::CascadeSplit(uint32_t cascadeIndex) const
{
    assert(cascadeIndex < kCascadeCount);
    return m_cascadeSplits[cascadeIndex];
}


/// <summary>
/// シャドウマップへの描き込みを開始します。
/// </summary>
void ShadowMap::BeginShadowPass(ID3D12GraphicsCommandList* commandList) const
{
    // バリア : テクスチャ → 深度バッファ
    //   ★ 段ごとではなく、影のパス全体で 1 回だけ。
    //     配列まるごと（ALL_SUBRESOURCES）を一度に移す。
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = m_shadowMap.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    commandList->ResourceBarrier(1, &barrier);
}


/// <summary>
/// 指定した段への描き込みを開始します。
/// </summary>
void ShadowMap::BeginRender(ID3D12GraphicsCommandList* commandList,
                            uint32_t cascadeIndex) const
{
    assert(cascadeIndex < kCascadeCount);

    // 描画範囲はシャドウマップ全体。画面の解像度とは無関係。
    commandList->RSSetViewports(1, &m_viewport);
    commandList->RSSetScissorRects(1, &m_scissorRect);

    // 描画先の設定
    //   ★ 色は要らないのでレンダーターゲットは 0 枚。深度だけを描く。
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    dsvHandle.ptr += static_cast<SIZE_T>(cascadeIndex) * m_dsvDescriptorSize;

    commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);

    // 一番奥の値で埋める
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
