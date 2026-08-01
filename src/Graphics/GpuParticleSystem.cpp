//=============================================================================
// GpuParticleSystem.cpp
//   GpuParticleSystem の実装。
//=============================================================================
#include "GpuParticleSystem.h"

#include "DescriptorHeap.h"
#include "ShaderCompiler.h"

#include <format>

namespace dx12
{
namespace
{
using namespace DirectX;

/// <summary>更新シェーダーの場所。</summary>
constexpr const wchar_t* kUpdateShaderPath = L"shaders/ParticleUpdate.hlsl";

/// <summary>描画シェーダーの場所。</summary>
constexpr const wchar_t* kDrawShaderPath = L"shaders/ParticleDraw.hlsl";

/// <summary>板 1 枚ぶんの頂点数（三角形 2 枚）。</summary>
constexpr uint32_t kVerticesPerQuad = 6;

/// <summary>重力（毎秒毎秒）。</summary>
constexpr float kGravity = -4.6f;

/// <summary>跳ね返る床の高さ。</summary>
constexpr float kFloorLevel = 0.02f;

/// <summary>生まれた瞬間の速さ。</summary>
constexpr float kInitialSpeed = 2.35f;


/// <summary>更新シェーダーへ渡す定数。`ParticleUpdate.hlsl` と一致させること。</summary>
struct ParticleUpdateConstants
{
    /// <summary>x = 前フレームからの秒、y = 経過秒、z = 総数、w = 未使用。</summary>
    XMFLOAT4 timing;

    /// <summary>xyz = 湧き出し口、w = 初速。</summary>
    XMFLOAT4 emitter;

    /// <summary>xyz = 重力、w = 床の高さ。</summary>
    XMFLOAT4 gravity;
};


/// <summary>描画シェーダーへ渡す定数。`ParticleDraw.hlsl` と一致させること。</summary>
struct ParticleDrawConstants
{
    /// <summary>ビュー行列 × 射影行列。</summary>
    XMFLOAT4X4 viewProjection;

    /// <summary>カメラの右方向。w は未使用。</summary>
    XMFLOAT4 cameraRight;

    /// <summary>カメラの上方向。w は未使用。</summary>
    XMFLOAT4 cameraUp;

    /// <summary>x = 射影行列の _33、y = 同 _43、z = 消し始める距離、w = 有効フラグ。</summary>
    XMFLOAT4 depthParams;
};


/// <summary>
/// ルートシグネチャをシリアライズして生成します。
/// </summary>
/// <param name="device">生成に使う D3D12 デバイス。</param>
/// <param name="desc">元になる説明。</param>
/// <param name="result">生成先。</param>
/// <exception cref="HrException">生成に失敗した場合。</exception>
void CreateRootSignature(ID3D12Device* device,
                         const D3D12_ROOT_SIGNATURE_DESC& desc,
                         ComPtr<ID3D12RootSignature>& result)
{
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> error;

    const HRESULT hr = ::D3D12SerializeRootSignature(
        &desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error);

    if (FAILED(hr))
    {
        if (error != nullptr)
        {
            ::OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
        }
        DX_CHECK(hr);
    }

    DX_CHECK(device->CreateRootSignature(0,
                                         serialized->GetBufferPointer(),
                                         serialized->GetBufferSize(),
                                         IID_PPV_ARGS(&result)));
}
} // namespace


/// <summary>
/// 構造化バッファ・UAV / SRV・2 つの PSO・定数バッファを生成します。
/// </summary>
void GpuParticleSystem::Initialize(ID3D12Device* device,
                                   DescriptorHeap& descriptorHeap,
                                   DXGI_FORMAT renderTargetFormat,
                                   DXGI_FORMAT depthStencilFormat,
                                   uint32_t frameCount)
{
    // --- (1) 構造化バッファ ---------------------------------------------------
    //   GPU が読み書きするだけなので DEFAULT ヒープに置きます。
    //   CPU からは一度も触りません。
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask     = 1;
    heapProperties.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment        = 0;
    bufferDesc.Width            = sizeof(GpuParticle) * kMaxParticles;
    bufferDesc.Height           = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels        = 1;
    bufferDesc.Format           = DXGI_FORMAT_UNKNOWN;   // バッファは形式を持たない
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // ★ これが無いと UAV を作れません。
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // ★ 生成直後の中身は 0 で埋められます。
    //   寿命が 0 なので、最初の更新で全部が湧き出し口から生まれ直します。
    //   初期化用の転送を書かずに済むのは、この保証があるからです。
    DX_CHECK(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_particleBuffer)));

    m_particleBuffer->SetName(L"GPU パーティクル");

    // --- (2) UAV と SRV -------------------------------------------------------
    //   同じバッファを「書く用」と「読む用」の 2 通りで見ます。
    const uint32_t uavIndex = descriptorHeap.Allocate();
    const uint32_t srvIndex = descriptorHeap.Allocate();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format                      = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement         = 0;
    uavDesc.Buffer.NumElements          = kMaxParticles;
    uavDesc.Buffer.StructureByteStride  = sizeof(GpuParticle);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_NONE;

    device->CreateUnorderedAccessView(m_particleBuffer.Get(), nullptr, &uavDesc,
                                      descriptorHeap.CpuHandle(uavIndex));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                     = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement        = 0;
    srvDesc.Buffer.NumElements         = kMaxParticles;
    srvDesc.Buffer.StructureByteStride = sizeof(GpuParticle);
    srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;

    device->CreateShaderResourceView(m_particleBuffer.Get(), &srvDesc,
                                     descriptorHeap.CpuHandle(srvIndex));

    m_unorderedAccessView = descriptorHeap.GpuHandle(uavIndex);
    m_shaderResourceView  = descriptorHeap.GpuHandle(srvIndex);

    // --- (3) 更新（コンピュート）側 -------------------------------------------
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors                    = 1;
    uavRange.BaseShaderRegister                = 0;   // u0
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER computeParameters[2] = {};

    computeParameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    computeParameters[0].Descriptor.ShaderRegister = 0;

    computeParameters[1].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    computeParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    computeParameters[1].DescriptorTable.pDescriptorRanges   = &uavRange;

    // ★ コンピュート用のルートシグネチャに DENY_*_ROOT_ACCESS は付けません。
    //   グラフィックスの段階が存在しないので、意味を持ちません。
    D3D12_ROOT_SIGNATURE_DESC computeRootDesc = {};
    computeRootDesc.NumParameters = _countof(computeParameters);
    computeRootDesc.pParameters   = computeParameters;
    computeRootDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    CreateRootSignature(device, computeRootDesc, m_computeRootSignature);

    shader::Bytecode computeShader = shader::Compile(
        ResolveAssetPath(kUpdateShaderPath), L"CSMain",
        shader::kComputeShaderTarget);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = m_computeRootSignature.Get();
    computePsoDesc.CS             = { computeShader->GetBufferPointer(),
                                      computeShader->GetBufferSize() };

    DX_CHECK(device->CreateComputePipelineState(
        &computePsoDesc, IID_PPV_ARGS(&m_computePipelineState)));

    // --- (4) 描画側 -----------------------------------------------------------
    //   構造化バッファと深度は、別々のテーブルにします。
    //   1 つにまとめると、ヒープ上で隣り合っていなければならなくなります。
    D3D12_DESCRIPTOR_RANGE particleRange = {};
    particleRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    particleRange.NumDescriptors                    = 1;
    particleRange.BaseShaderRegister                = 0;   // t0
    particleRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE depthRange = particleRange;
    depthRange.BaseShaderRegister = 1;                     // t1

    D3D12_ROOT_PARAMETER graphicsParameters[3] = {};

    graphicsParameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    graphicsParameters[0].Descriptor.ShaderRegister = 0;
    graphicsParameters[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    graphicsParameters[1].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    graphicsParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    graphicsParameters[1].DescriptorTable.pDescriptorRanges   = &particleRange;

    // ★ 頂点シェーダーが読むので VERTEX。PIXEL にすると PSO 生成が黙って失敗する。
    graphicsParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    graphicsParameters[2].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    graphicsParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    graphicsParameters[2].DescriptorTable.pDescriptorRanges   = &depthRange;
    graphicsParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC graphicsRootDesc = {};
    graphicsRootDesc.NumParameters = _countof(graphicsParameters);
    graphicsRootDesc.pParameters   = graphicsParameters;
    graphicsRootDesc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    CreateRootSignature(device, graphicsRootDesc, m_graphicsRootSignature);

    const std::wstring drawPath = ResolveAssetPath(kDrawShaderPath);

    shader::Bytecode vertexShader =
        shader::Compile(drawPath, L"VSMain", shader::kVertexShaderTarget);
    shader::Bytecode pixelShader =
        shader::Compile(drawPath, L"PSMain", shader::kPixelShaderTarget);

    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode        = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode        = D3D12_CULL_MODE_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable    = TRUE;
    depthStencilDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.StencilEnable  = FALSE;

    // 加算合成。火の粉なので、重なるほど明るくなってよい。
    D3D12_BLEND_DESC blendDesc = {};
    D3D12_RENDER_TARGET_BLEND_DESC& target = blendDesc.RenderTarget[0];

    target.BlendEnable          = TRUE;
    target.SrcBlend             = D3D12_BLEND_ONE;
    target.DestBlend            = D3D12_BLEND_ONE;
    target.BlendOp              = D3D12_BLEND_OP_ADD;
    target.SrcBlendAlpha        = D3D12_BLEND_ONE;
    target.DestBlendAlpha       = D3D12_BLEND_ONE;
    target.BlendOpAlpha         = D3D12_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = m_graphicsRootSignature.Get();
    psoDesc.VS                    = { vertexShader->GetBufferPointer(),
                                      vertexShader->GetBufferSize() };
    psoDesc.PS                    = { pixelShader->GetBufferPointer(),
                                      pixelShader->GetBufferSize() };
    psoDesc.RasterizerState       = rasterizerDesc;
    psoDesc.DepthStencilState     = depthStencilDesc;
    psoDesc.BlendState            = blendDesc;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = renderTargetFormat;
    psoDesc.DSVFormat             = depthStencilFormat;
    psoDesc.SampleDesc.Count      = 1;

    DX_CHECK(device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_graphicsPipelineState)));

    // --- (5) 定数バッファ -----------------------------------------------------
    m_updateConstants.Initialize(device, sizeof(ParticleUpdateConstants), frameCount);
    m_drawConstants.Initialize(device, sizeof(ParticleDrawConstants), frameCount);

    Log(std::format(L"GPU パーティクルを構築しました（最大 {} 個）。", kMaxParticles));
}


/// <summary>
/// このフレームぶんの定数を書き込みます。
/// </summary>
void GpuParticleSystem::Update(uint32_t frameIndex,
                               float deltaTime,
                               float time,
                               const DirectX::XMFLOAT3& emitter,
                               const DirectX::XMMATRIX& viewProjection,
                               const DirectX::XMFLOAT3& cameraRight,
                               const DirectX::XMFLOAT3& cameraUp,
                               const DirectX::XMMATRIX& projection,
                               float softFadeDistance,
                               uint32_t count)
{
    ParticleUpdateConstants updateConstants = {};

    // ★ 極端に大きい deltaTime を渡さない。ウィンドウを動かした直後などに
    //   1 フレームで数秒進むと、パーティクルが一斉に飛び散る。
    const float clampedDelta = (deltaTime > 0.05f) ? 0.05f : deltaTime;

    updateConstants.timing  = { clampedDelta, time, static_cast<float>(count), 0.0f };
    updateConstants.emitter = { emitter.x, emitter.y, emitter.z, kInitialSpeed };
    updateConstants.gravity = { 0.0f, kGravity, 0.0f, kFloorLevel };

    m_updateConstants.Update(frameIndex, &updateConstants, sizeof(updateConstants));

    ParticleDrawConstants drawConstants = {};

    XMStoreFloat4x4(&drawConstants.viewProjection, XMMatrixTranspose(viewProjection));

    drawConstants.cameraRight = { cameraRight.x, cameraRight.y, cameraRight.z, 0.0f };
    drawConstants.cameraUp    = { cameraUp.x, cameraUp.y, cameraUp.z, 0.0f };

    XMFLOAT4X4 projectionMatrix;
    XMStoreFloat4x4(&projectionMatrix, projection);

    drawConstants.depthParams = {
        projectionMatrix._33,
        projectionMatrix._43,
        (softFadeDistance > 0.0f) ? softFadeDistance : 1.0f,
        (softFadeDistance > 0.0f) ? 1.0f : 0.0f
    };

    m_drawConstants.Update(frameIndex, &drawConstants, sizeof(drawConstants));
}


/// <summary>
/// パーティクルを 1 フレームぶん進める命令を記録します。
/// </summary>
void GpuParticleSystem::RecordUpdate(ID3D12GraphicsCommandList* commandList,
                                     uint32_t frameIndex,
                                     uint32_t count)
{
    if (count == 0)
    {
        return;
    }

    // ★ グラフィックスとは別系統。Set"Compute"RootSignature を使う。
    //   Set"Graphics"… と書いても警告なく通り、値だけが届かない。
    commandList->SetComputeRootSignature(m_computeRootSignature.Get());
    commandList->SetPipelineState(m_computePipelineState.Get());

    commandList->SetComputeRootConstantBufferView(
        0, m_updateConstants.GpuAddress(frameIndex));

    commandList->SetComputeRootDescriptorTable(1, m_unorderedAccessView);

    // 端数を切り上げてグループ数を決める。シェーダー側で範囲外を弾いている。
    const uint32_t groupCount = (count + kThreadGroupSize - 1) / kThreadGroupSize;

    commandList->Dispatch(groupCount, 1, 1);

    // --- 書き終わりを待たせる -------------------------------------------------
    //   ★ Dispatch と、この後の描画は自動では順番が守られない。
    //     UAV バリアで「書き終わってから読め」と明示する。
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_particleBuffer.Get();

    commandList->ResourceBarrier(1, &uavBarrier);

    // 頂点シェーダーから読める状態へ移す。
    D3D12_RESOURCE_BARRIER transition = {};
    transition.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition.pResource   = m_particleBuffer.Get();
    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.StateAfter =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    transition.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(1, &transition);
}


/// <summary>
/// パーティクルを描く命令を記録します。
/// </summary>
void GpuParticleSystem::RecordDraw(ID3D12GraphicsCommandList* commandList,
                                   uint32_t frameIndex,
                                   D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth,
                                   uint32_t count)
{
    if (count == 0)
    {
        return;
    }

    commandList->SetGraphicsRootSignature(m_graphicsRootSignature.Get());
    commandList->SetPipelineState(m_graphicsPipelineState.Get());

    commandList->SetGraphicsRootConstantBufferView(
        0, m_drawConstants.GpuAddress(frameIndex));

    commandList->SetGraphicsRootDescriptorTable(1, m_shaderResourceView);
    commandList->SetGraphicsRootDescriptorTable(2, sceneDepth);

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 0, nullptr);
    commandList->IASetIndexBuffer(nullptr);

    // ★ 何万個でも命令は 1 つ。CPU 側の仕事は数の大小に関係しない。
    commandList->DrawInstanced(kVerticesPerQuad, count, 0, 0);

    // 次の更新に備えて書き込み可へ戻す。
    D3D12_RESOURCE_BARRIER transition = {};
    transition.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition.pResource   = m_particleBuffer.Get();
    transition.Transition.StateBefore =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    transition.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(1, &transition);
}

} // namespace dx12
