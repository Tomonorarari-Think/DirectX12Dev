//=============================================================================
// VfxPipeline.cpp
//   VfxPipeline の実装。
//=============================================================================
#include "VfxPipeline.h"

#include "ShaderCompiler.h"

#include <cstring>

namespace dx12
{
namespace
{
using namespace DirectX;

/// <summary>シェーダーファイルの場所。</summary>
constexpr const wchar_t* kShaderRelativePath = L"shaders/Vfx.hlsl";

/// <summary>板 1 枚ぶんの頂点数（三角形 2 枚）。</summary>
constexpr uint32_t kVerticesPerQuad = 6;


/// <summary>
/// シェーダーへ渡す定数の並び。`Vfx.hlsl` の cbuffer と一致させること。
/// </summary>
struct VfxConstants
{
    /// <summary>ビュー行列 × 射影行列。</summary>
    XMFLOAT4X4 viewProjection;

    /// <summary>カメラの右方向。w は未使用。</summary>
    XMFLOAT4 cameraRight;

    /// <summary>カメラの上方向。w は未使用。</summary>
    XMFLOAT4 cameraUp;

    /// <summary>板の一覧。</summary>
    VfxParticle particles[VfxPipeline::kMaxParticles];
};


/// <summary>
/// 合成の仕方に応じたブレンド設定を作ります。
/// </summary>
/// <param name="mode">合成の仕方。</param>
/// <returns>PSO に渡すブレンド設定。</returns>
/// <remarks>
/// シェーダーが**乗算済みアルファ**（色にアルファを掛けたもの）を出すので、
/// どちらも `SrcBlend` は `ONE` になります。違うのは `DestBlend` だけです。
///
/// | 合成 | 式 |
/// |------|-----|
/// | アルファ | `結果 = 前景 + 背景 × (1 - α)` … 背景を隠す |
/// | 加算 | `結果 = 前景 + 背景` … 背景に足す。暗くならない |
/// </remarks>
D3D12_BLEND_DESC CreateBlendDesc(BlendMode mode)
{
    D3D12_BLEND_DESC desc = {};
    desc.AlphaToCoverageEnable  = FALSE;
    desc.IndependentBlendEnable = FALSE;

    D3D12_RENDER_TARGET_BLEND_DESC& target = desc.RenderTarget[0];

    target.BlendEnable   = TRUE;
    target.LogicOpEnable = FALSE;

    // 乗算済みアルファなので、前景はそのまま足す。
    target.SrcBlend  = D3D12_BLEND_ONE;
    target.BlendOp   = D3D12_BLEND_OP_ADD;

    target.DestBlend = (mode == BlendMode::Alpha) ? D3D12_BLEND_INV_SRC_ALPHA
                                                  : D3D12_BLEND_ONE;

    // アルファ成分の合成。レンダーターゲットのアルファは使っていないが、
    // 指定を省くとデバッグレイヤーが警告するので揃えておく。
    target.SrcBlendAlpha  = D3D12_BLEND_ONE;
    target.DestBlendAlpha = (mode == BlendMode::Alpha) ? D3D12_BLEND_INV_SRC_ALPHA
                                                       : D3D12_BLEND_ONE;
    target.BlendOpAlpha   = D3D12_BLEND_OP_ADD;

    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    return desc;
}
} // namespace


/// <summary>
/// ルートシグネチャ・PSO（2 種類）・定数バッファを生成します。
/// </summary>
void VfxPipeline::Initialize(ID3D12Device* device,
                             DXGI_FORMAT renderTargetFormat,
                             DXGI_FORMAT depthStencilFormat,
                             uint32_t frameCount)
{
    // --- (1) ルートシグネチャ -------------------------------------------------
    //   使うのは定数バッファ 1 本だけ。テクスチャも頂点バッファも使わない。
    D3D12_ROOT_PARAMETER rootParameter = {};
    rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameter.Descriptor.ShaderRegister = 0;
    rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 1;
    rootSignatureDesc.pParameters   = &rootParameter;
    rootSignatureDesc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> error;

    const HRESULT hr = ::D3D12SerializeRootSignature(
        &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error);

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
                                         IID_PPV_ARGS(&m_rootSignature)));

    // --- (2) PSO --------------------------------------------------------------
    const std::wstring shaderPath = ResolveAssetPath(kShaderRelativePath);

    ComPtr<ID3DBlob> vertexShader = shader::Compile(shaderPath, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> pixelShader  = shader::Compile(shaderPath, "PSMain", "ps_5_0");

    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    // ★ カリングを切る。ビルボードは常にカメラを向くので、
    //   巻き順が視点によって裏返ることがある。
    rasterizerDesc.CullMode        = D3D12_CULL_MODE_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};

    // ★ 深度テストは「する」。手前にある不透明な物に隠れてほしいため。
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthFunc   = D3D12_COMPARISON_FUNC_LESS;

    // ★ 深度は「書かない」。ここが半透明でいちばん大事な設定。
    //   書いてしまうと、あとから描く半透明が「奥にある」と判定されて消える。
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.StencilEnable  = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = m_rootSignature.Get();
    psoDesc.VS                    = { vertexShader->GetBufferPointer(),
                                      vertexShader->GetBufferSize() };
    psoDesc.PS                    = { pixelShader->GetBufferPointer(),
                                      pixelShader->GetBufferSize() };
    psoDesc.RasterizerState       = rasterizerDesc;
    psoDesc.DepthStencilState     = depthStencilDesc;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = renderTargetFormat;
    psoDesc.DSVFormat             = depthStencilFormat;
    psoDesc.SampleDesc.Count      = 1;

    // ★ 違うのはブレンド設定だけ。シェーダーも頂点も共通。
    psoDesc.BlendState = CreateBlendDesc(BlendMode::Alpha);
    DX_CHECK(device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_alphaPipelineState)));

    psoDesc.BlendState = CreateBlendDesc(BlendMode::Additive);
    DX_CHECK(device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_additivePipelineState)));

    // --- (3) 定数バッファ -----------------------------------------------------
    m_constantBuffer.Initialize(device, sizeof(VfxConstants),
                                frameCount * kSlotCount);

    Log(L"半透明（VFX）の描画設定を構築しました。");
}


/// <summary>
/// このフレームぶんの板を書き込みます。
/// </summary>
void VfxPipeline::Update(uint32_t frameIndex,
                         uint32_t slot,
                         const DirectX::XMMATRIX& viewProjection,
                         const DirectX::XMFLOAT3& cameraRight,
                         const DirectX::XMFLOAT3& cameraUp,
                         const std::vector<VfxParticle>& particles)
{
    VfxConstants constants = {};

    XMStoreFloat4x4(&constants.viewProjection, XMMatrixTranspose(viewProjection));

    constants.cameraRight = { cameraRight.x, cameraRight.y, cameraRight.z, 0.0f };
    constants.cameraUp    = { cameraUp.x, cameraUp.y, cameraUp.z, 0.0f };

    const size_t count = (particles.size() < kMaxParticles) ? particles.size()
                                                            : kMaxParticles;

    if (count > 0)
    {
        std::memcpy(constants.particles, particles.data(),
                    count * sizeof(VfxParticle));
    }

    m_constantBuffer.Update(frameIndex * kSlotCount + slot,
                            &constants, sizeof(constants));
}


/// <summary>
/// 板を描く命令を記録します。
/// </summary>
void VfxPipeline::Record(ID3D12GraphicsCommandList* commandList,
                         uint32_t frameIndex,
                         uint32_t slot,
                         BlendMode mode,
                         uint32_t count) const
{
    if (count == 0)
    {
        return;
    }

    commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    commandList->SetPipelineState((mode == BlendMode::Alpha)
                                      ? m_alphaPipelineState.Get()
                                      : m_additivePipelineState.Get());

    commandList->SetGraphicsRootConstantBufferView(
        0, m_constantBuffer.GpuAddress(frameIndex * kSlotCount + slot));

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 0, nullptr);
    commandList->IASetIndexBuffer(nullptr);

    // ★ 1 回の呼び出しで count 枚ぶんを描く（インスタンシング）。
    //   板ごとに DrawInstanced を呼ぶより、命令の数が圧倒的に少なくなる。
    commandList->DrawInstanced(kVerticesPerQuad, count, 0, 0);
}

} // namespace dx12
