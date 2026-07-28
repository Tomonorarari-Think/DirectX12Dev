//=============================================================================
// SkyboxPipeline.cpp
//   SkyboxPipeline の実装。
//=============================================================================
#include "SkyboxPipeline.h"

#include "ShaderCompiler.h"

#include <cmath>
#include "SwapChain.h"
#include "DepthBuffer.h"

namespace dx12
{
namespace
{
using namespace DirectX;

/// <summary>シェーダーファイルの場所（プロジェクトルートからの相対パス）。</summary>
constexpr const wchar_t* kShaderRelativePath = L"shaders/Skybox.hlsl";

/// <summary>定数バッファを結び付けるルートパラメータの番号。</summary>
constexpr uint32_t kConstantsRootParameterIndex = 0;

/// <summary>環境マップを結び付けるルートパラメータの番号。</summary>
constexpr uint32_t kEnvironmentRootParameterIndex = 1;

/// <summary>画面いっぱいの三角形を作るのに要る頂点数。</summary>
/// <remarks>
/// 四角形（2 三角形・6 頂点）でも覆えますが、対角線上のピクセルが
/// 2 度処理されます。3 頂点なら重なりが出ません。
/// </remarks>
constexpr uint32_t kFullScreenTriangleVertexCount = 3;
} // namespace


/// <summary>
/// ルートシグネチャ・PSO・定数バッファを生成します。
/// </summary>
void SkyboxPipeline::Initialize(ID3D12Device* device, uint32_t frameCount)
{
    // --- (1) ルートシグネチャ -------------------------------------------------
    D3D12_ROOT_PARAMETER rootParameters[2] = {};

    // 0 番 : 定数バッファ（逆行列と環境光の強さ）
    rootParameters[kConstantsRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kConstantsRootParameterIndex].Descriptor.ShaderRegister = 0;
    rootParameters[kConstantsRootParameterIndex].Descriptor.RegisterSpace  = 0;
    rootParameters[kConstantsRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;

    // 1 番 : 環境マップ（ディスクリプタテーブル）
    D3D12_DESCRIPTOR_RANGE environmentRange = {};
    environmentRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    environmentRange.NumDescriptors     = 1;
    environmentRange.BaseShaderRegister = 0;   // register(t0)
    environmentRange.RegisterSpace      = 0;
    environmentRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[kEnvironmentRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kEnvironmentRootParameterIndex]
        .DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[kEnvironmentRootParameterIndex]
        .DescriptorTable.pDescriptorRanges = &environmentRange;
    rootParameters[kEnvironmentRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    staticSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;   // 左右がつながる
    staticSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;  // 上下は極で打ち止め
    staticSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.MaxAnisotropy    = 1;
    staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    staticSampler.BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSampler.MinLOD           = 0.0f;
    staticSampler.MaxLOD           = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister   = 0;
    staticSampler.RegisterSpace    = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters     = _countof(rootParameters);
    rootSignatureDesc.pParameters       = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers   = &staticSampler;

    // ★ 頂点バッファを使わないので、入力アセンブラの許可が要らない。
    //   要らない権限を落としておくと、ドライバが最適化しやすくなる。
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

    // --- (2) パイプラインステート --------------------------------------------
    const std::wstring shaderPath = ResolveAssetPath(kShaderRelativePath);

    ComPtr<ID3DBlob> vertexShader = shader::Compile(shaderPath, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> pixelShader  = shader::Compile(shaderPath, "PSMain", "ps_5_0");

    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode              = D3D12_FILL_MODE_SOLID;

    // ★ カリングを切る。画面を覆う三角形は巻き順を気にする意味がない。
    rasterizerDesc.CullMode              = D3D12_CULL_MODE_NONE;
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthBias             = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizerDesc.DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerDesc.SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;

    // ★ 深度クリップを切る。z = 1.0 ちょうどの面が丸め次第で消えるのを防ぐ。
    rasterizerDesc.DepthClipEnable       = FALSE;
    rasterizerDesc.MultisampleEnable     = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount     = 0;
    rasterizerDesc.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable    = TRUE;

    // ★ 深度は書かない。背景は最も奥なので、他の判定に影響させる必要がない。
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    // ★ LESS_EQUAL でなければならない。背景の深度は 1.0、クリア値も 1.0 で、
    //   LESS だと「等しい」ため 1 ピクセルも通らず背景が出ない。
    depthStencilDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depthStencilDesc.StencilEnable  = FALSE;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable  = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();

    // ★ InputLayout は空のまま。頂点バッファを使わないため。
    psoDesc.InputLayout.pInputElementDescs = nullptr;
    psoDesc.InputLayout.NumElements        = 0;

    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(),  pixelShader->GetBufferSize() };
    psoDesc.RasterizerState   = rasterizerDesc;
    psoDesc.BlendState        = blendDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.SampleMask        = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets  = 1;
    psoDesc.RTVFormats[0]     = SwapChain::kRenderTargetViewFormat;
    psoDesc.DSVFormat         = DepthBuffer::kFormat;
    psoDesc.SampleDesc.Count  = 1;

    DX_CHECK(device->CreateGraphicsPipelineState(&psoDesc,
                                                 IID_PPV_ARGS(&m_pipelineState)));

    // --- (3) 定数バッファ -----------------------------------------------------
    m_constantBuffer.Initialize(device, sizeof(SkyboxConstants), frameCount);

    Log(L"背景（スカイボックス）の描画設定を構築しました。");
}


/// <summary>
/// このフレームぶんの定数を書き込みます。
/// </summary>
void SkyboxPipeline::Update(uint32_t frameIndex,
                            const Camera& camera,
                            float ambientIntensity)
{
    // カメラの 3 軸を組み立てる。
    //   ★ 位置は使わない。使うと、カメラを動かしたときに背景が流れてしまう。
    const XMVECTOR eye    = XMLoadFloat3(&camera.Position());
    const XMVECTOR target = XMLoadFloat3(&camera.Target());
    const XMVECTOR worldUp = XMLoadFloat3(&camera.Up());

    const XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, eye));

    // 左手座標系では cross(上, 前) が右になる。順序を逆にすると左右が反転する。
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    const XMVECTOR up    = XMVector3Cross(forward, right);

    SkyboxConstants constants = {};

    XMStoreFloat4(&constants.cameraRight,   right);
    XMStoreFloat4(&constants.cameraUp,      up);
    XMStoreFloat4(&constants.cameraForward, forward);

    // 画面の端が前方から何度ずれるかは、画角の半分の正接で決まる。
    const float tanHalfFov = std::tan(camera.FieldOfView() * 0.5f);

    constants.params = { tanHalfFov, camera.AspectRatio(), ambientIntensity, 0.0f };

    m_constantBuffer.Update(frameIndex, &constants, sizeof(constants));
}


/// <summary>
/// 背景を描く命令を記録します。
/// </summary>
void SkyboxPipeline::Record(ID3D12GraphicsCommandList* commandList,
                            uint32_t frameIndex,
                            D3D12_GPU_DESCRIPTOR_HANDLE environmentView) const
{
    commandList->SetPipelineState(m_pipelineState.Get());
    commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    commandList->SetGraphicsRootConstantBufferView(
        kConstantsRootParameterIndex, m_constantBuffer.GpuAddress(frameIndex));

    commandList->SetGraphicsRootDescriptorTable(
        kEnvironmentRootParameterIndex, environmentView);

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // ★ 頂点バッファを結び付けない。頂点シェーダーが SV_VertexID から座標を作る。
    commandList->IASetVertexBuffers(0, 0, nullptr);
    commandList->IASetIndexBuffer(nullptr);

    commandList->DrawInstanced(kFullScreenTriangleVertexCount, 1, 0, 0);
}

} // namespace dx12
