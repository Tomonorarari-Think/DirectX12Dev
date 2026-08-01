//=============================================================================
// PostProcessPipeline.cpp
//   PostProcessPipeline の実装。
//=============================================================================
#include "PostProcessPipeline.h"

#include "DescriptorHeap.h"
#include "ShaderCompiler.h"
#include "SwapChain.h"

#include <algorithm>

namespace dx12
{
namespace
{
using namespace DirectX;

/// <summary>後処理のシェーダー。</summary>
constexpr const wchar_t* kPostProcessShaderPath = L"shaders/PostProcess.hlsl";

/// <summary>ブルームのシェーダー。</summary>
constexpr const wchar_t* kBloomShaderPath = L"shaders/Bloom.hlsl";

/// <summary>定数バッファのルートパラメータ番号。</summary>
constexpr uint32_t kConstantsRootParameterIndex = 0;

/// <summary>入力テクスチャ（t0）のルートパラメータ番号。</summary>
constexpr uint32_t kSourceRootParameterIndex = 1;

/// <summary>ブルーム画像（t1）のルートパラメータ番号。</summary>
constexpr uint32_t kBloomRootParameterIndex = 2;

/// <summary>画面いっぱいの三角形に要る頂点数。</summary>
constexpr uint32_t kFullScreenTriangleVertexCount = 3;

/// <summary>1 フレームに使うブルーム用定数のスロット数（抽出・横・縦）。</summary>
constexpr uint32_t kBloomPassCount = 3;

/// <summary>露出。1.0 でそのまま。</summary>
constexpr float kExposure = 1.0f;

/// <summary>トーンマッピングで「白」とみなす明るさ。</summary>
constexpr float kWhitePoint = 2.2f;

/// <summary>ブルームを足す強さ。</summary>
constexpr float kBloomIntensity = 1.20f;

/// <summary>ブルームとして拾い始める明るさ。</summary>
/// <remarks>
/// 1.0 より少し上に置きます。ここを下げすぎると、普通の明るさの面まで
/// にじんで画面全体が眠くなります。
/// </remarks>
constexpr float kBloomThreshold = 1.00f;

/// <summary>しきい値の立ち上がりのなだらかさ。</summary>
constexpr float kBloomSoftness = 0.60f;

/// <summary>周辺減光の強さ。0 で無効。</summary>
constexpr float kVignetteStrength = 0.35f;

/// <summary>作業用テクスチャのクリア色（真っ黒）。</summary>
constexpr float kBloomClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };


/// <summary>
/// 後処理のパイプラインステートを 1 つ作ります。
/// </summary>
/// <param name="device">生成に使う D3D12 デバイス。</param>
/// <param name="rootSignature">共通のルートシグネチャ。</param>
/// <param name="shaderPath">シェーダーファイルの絶対パス。</param>
/// <param name="pixelEntryPoint">ピクセルシェーダーの入口。</param>
/// <param name="renderTargetFormat">書き込み先の形式。</param>
/// <returns>生成した PSO。</returns>
/// <remarks>
/// 後処理はどれも「画面を覆う三角形を描くだけ」なので、
/// 違うのはピクセルシェーダーと書き込み先の形式だけです。
/// </remarks>
ComPtr<ID3D12PipelineState> CreateFullScreenPipelineState(
    ID3D12Device* device,
    ID3D12RootSignature* rootSignature,
    const std::wstring& shaderPath,
    const wchar_t* pixelEntryPoint,
    DXGI_FORMAT renderTargetFormat)
{
    shader::Bytecode vertexShader =
        shader::Compile(shaderPath, L"VSMain", shader::kVertexShaderTarget);
    shader::Bytecode pixelShader = shader::Compile(shaderPath, pixelEntryPoint,
                                                    shader::kPixelShaderTarget);

    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode              = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode              = D3D12_CULL_MODE_NONE;
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthClipEnable       = FALSE;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // ★ 後処理では深度を一切使わない。深度バッファも結び付けない。
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable   = FALSE;
    depthStencilDesc.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = rootSignature;
    psoDesc.VS                    = { vertexShader->GetBufferPointer(),
                                      vertexShader->GetBufferSize() };
    psoDesc.PS                    = { pixelShader->GetBufferPointer(),
                                      pixelShader->GetBufferSize() };
    psoDesc.RasterizerState       = rasterizerDesc;
    psoDesc.BlendState            = blendDesc;
    psoDesc.DepthStencilState     = depthStencilDesc;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = renderTargetFormat;
    psoDesc.DSVFormat             = DXGI_FORMAT_UNKNOWN;   // 深度は使わない
    psoDesc.SampleDesc.Count      = 1;

    ComPtr<ID3D12PipelineState> pipelineState;
    DX_CHECK(device->CreateGraphicsPipelineState(&psoDesc,
                                                 IID_PPV_ARGS(&pipelineState)));
    return pipelineState;
}
} // namespace


/// <summary>
/// ルートシグネチャ・PSO・作業用テクスチャを生成します。
/// </summary>
void PostProcessPipeline::Initialize(ID3D12Device* device,
                                     DescriptorHeap& descriptorHeap,
                                     uint32_t width,
                                     uint32_t height,
                                     uint32_t frameCount)
{
    m_width  = width;
    m_height = height;

    // --- (1) ルートシグネチャ（3 パス共通）-----------------------------------
    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    rootParameters[kConstantsRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kConstantsRootParameterIndex].Descriptor.ShaderRegister = 0;
    rootParameters[kConstantsRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    // ★ t0 と t1 は別々のテーブルにする。1 つにまとめると
    //   「ヒープ上で隣り合っていること」が前提になり、確保した順序に縛られる。
    D3D12_DESCRIPTOR_RANGE sourceRange = {};
    sourceRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    sourceRange.NumDescriptors     = 1;
    sourceRange.BaseShaderRegister = 0;   // t0
    sourceRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[kSourceRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kSourceRootParameterIndex].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[kSourceRootParameterIndex].DescriptorTable.pDescriptorRanges =
        &sourceRange;
    rootParameters[kSourceRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE bloomRange = {};
    bloomRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    bloomRange.NumDescriptors     = 1;
    bloomRange.BaseShaderRegister = 1;   // t1
    bloomRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[kBloomRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kBloomRootParameterIndex].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[kBloomRootParameterIndex].DescriptorTable.pDescriptorRanges =
        &bloomRange;
    rootParameters[kBloomRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    staticSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

    // ★ CLAMP にする。WRAP だと、画面の端をぼかすときに反対側の色を拾う。
    staticSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.MaxAnisotropy    = 1;
    staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    staticSampler.BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSampler.MinLOD           = 0.0f;
    staticSampler.MaxLOD           = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister   = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters     = _countof(rootParameters);
    rootSignatureDesc.pParameters       = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers   = &staticSampler;
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
    const std::wstring bloomPath = ResolveAssetPath(kBloomShaderPath);
    const std::wstring postPath  = ResolveAssetPath(kPostProcessShaderPath);

    m_thresholdPipelineState = CreateFullScreenPipelineState(
        device, m_rootSignature.Get(), bloomPath, L"PSThreshold",
        RenderTexture::kFormat);

    m_blurPipelineState = CreateFullScreenPipelineState(
        device, m_rootSignature.Get(), bloomPath, L"PSBlur",
        RenderTexture::kFormat);

    // ★ 合成だけは書き込み先がスワップチェーン（sRGB）。形式が違う。
    m_compositePipelineState = CreateFullScreenPipelineState(
        device, m_rootSignature.Get(), postPath, L"PSMain",
        SwapChain::kRenderTargetViewFormat);

    // --- (3) 作業用テクスチャ（半分の解像度）---------------------------------
    //   ぼかす前提なので、細かさは要りません。半分にすると読む量が 1/4 になり、
    //   同じ半径でも 2 倍ぼけます。
    const uint32_t halfWidth  = std::max(width / 2, 1u);
    const uint32_t halfHeight = std::max(height / 2, 1u);

    m_bloomTexture[0].Initialize(device, descriptorHeap, halfWidth, halfHeight,
                                 L"ブルーム A", kBloomClearColor);
    m_bloomTexture[1].Initialize(device, descriptorHeap, halfWidth, halfHeight,
                                 L"ブルーム B", kBloomClearColor);

    // --- (4) 定数バッファ -----------------------------------------------------
    m_compositeConstants.Initialize(device, sizeof(PostProcessConstants), frameCount);
    m_bloomConstants.Initialize(device, sizeof(BloomConstants),
                                frameCount * kBloomPassCount);

    Log(L"後処理（ポストプロセス）の描画設定を構築しました。");
}


/// <summary>
/// 後処理の命令を記録します。
/// </summary>
void PostProcessPipeline::Record(ID3D12GraphicsCommandList* commandList,
                                 uint32_t frameIndex,
                                 const RenderTexture& scene,
                                 D3D12_CPU_DESCRIPTOR_HANDLE backBufferView,
                                 const D3D12_VIEWPORT& viewport,
                                 const D3D12_RECT& scissor)
{
    commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 0, nullptr);
    commandList->IASetIndexBuffer(nullptr);

    const uint32_t halfWidth  = m_bloomTexture[0].Width();
    const uint32_t halfHeight = m_bloomTexture[0].Height();

    D3D12_VIEWPORT halfViewport = {};
    halfViewport.Width    = static_cast<float>(halfWidth);
    halfViewport.Height   = static_cast<float>(halfHeight);
    halfViewport.MaxDepth = 1.0f;

    D3D12_RECT halfScissor = {};
    halfScissor.right  = static_cast<LONG>(halfWidth);
    halfScissor.bottom = static_cast<LONG>(halfHeight);

    const uint32_t bloomSlotBase = frameIndex * kBloomPassCount;

    if (m_bloomEnabled)
    {
        commandList->RSSetViewports(1, &halfViewport);
        commandList->RSSetScissorRects(1, &halfScissor);

        // --- (1) 明るい所だけを取り出す（同時に半分へ縮小）-------------------
        {
            BloomConstants constants = {};
            constants.params = { kBloomThreshold, kBloomSoftness, 0.0f, 0.0f };
            m_bloomConstants.Update(bloomSlotBase + 0, &constants, sizeof(constants));

            m_bloomTexture[0].BeginRender(commandList, nullptr);

            commandList->SetPipelineState(m_thresholdPipelineState.Get());
            commandList->SetGraphicsRootConstantBufferView(
                kConstantsRootParameterIndex,
                m_bloomConstants.GpuAddress(bloomSlotBase + 0));
            commandList->SetGraphicsRootDescriptorTable(
                kSourceRootParameterIndex, scene.ShaderResourceView());

            // t1 は使わないが、結び付けておかないとデバッグレイヤーが警告する。
            commandList->SetGraphicsRootDescriptorTable(
                kBloomRootParameterIndex, m_bloomTexture[1].ShaderResourceView());

            commandList->DrawInstanced(kFullScreenTriangleVertexCount, 1, 0, 0);

            m_bloomTexture[0].EndRender(commandList);
        }

        // --- (2) 横方向にぼかす（A → B）-------------------------------------
        {
            BloomConstants constants = {};
            constants.blurDirection = { 1.0f, 0.0f,
                                        1.0f / static_cast<float>(halfWidth),
                                        1.0f / static_cast<float>(halfHeight) };
            m_bloomConstants.Update(bloomSlotBase + 1, &constants, sizeof(constants));

            m_bloomTexture[1].BeginRender(commandList, nullptr);

            commandList->SetPipelineState(m_blurPipelineState.Get());
            commandList->SetGraphicsRootConstantBufferView(
                kConstantsRootParameterIndex,
                m_bloomConstants.GpuAddress(bloomSlotBase + 1));
            commandList->SetGraphicsRootDescriptorTable(
                kSourceRootParameterIndex, m_bloomTexture[0].ShaderResourceView());
            commandList->SetGraphicsRootDescriptorTable(
                kBloomRootParameterIndex, m_bloomTexture[0].ShaderResourceView());

            commandList->DrawInstanced(kFullScreenTriangleVertexCount, 1, 0, 0);

            m_bloomTexture[1].EndRender(commandList);
        }

        // --- (3) 縦方向にぼかす（B → A）-------------------------------------
        {
            BloomConstants constants = {};
            constants.blurDirection = { 0.0f, 1.0f,
                                        1.0f / static_cast<float>(halfWidth),
                                        1.0f / static_cast<float>(halfHeight) };
            m_bloomConstants.Update(bloomSlotBase + 2, &constants, sizeof(constants));

            m_bloomTexture[0].BeginRender(commandList, nullptr);

            commandList->SetPipelineState(m_blurPipelineState.Get());
            commandList->SetGraphicsRootConstantBufferView(
                kConstantsRootParameterIndex,
                m_bloomConstants.GpuAddress(bloomSlotBase + 2));
            commandList->SetGraphicsRootDescriptorTable(
                kSourceRootParameterIndex, m_bloomTexture[1].ShaderResourceView());
            commandList->SetGraphicsRootDescriptorTable(
                kBloomRootParameterIndex, m_bloomTexture[1].ShaderResourceView());

            commandList->DrawInstanced(kFullScreenTriangleVertexCount, 1, 0, 0);

            m_bloomTexture[0].EndRender(commandList);
        }
    }

    // --- (4) 合成して画面へ ---------------------------------------------------
    {
        PostProcessConstants constants = {};
        constants.params = { kExposure, kWhitePoint,
                             m_bloomEnabled ? kBloomIntensity : 0.0f,
                             kVignetteStrength };
        constants.screenSize = { static_cast<float>(m_width),
                                 static_cast<float>(m_height),
                                 1.0f / static_cast<float>(m_width),
                                 1.0f / static_cast<float>(m_height) };
        m_compositeConstants.Update(frameIndex, &constants, sizeof(constants));

        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);

        // 深度は結び付けない。後処理は画面を塗り替えるだけ。
        commandList->OMSetRenderTargets(1, &backBufferView, FALSE, nullptr);

        commandList->SetPipelineState(m_compositePipelineState.Get());
        commandList->SetGraphicsRootConstantBufferView(
            kConstantsRootParameterIndex, m_compositeConstants.GpuAddress(frameIndex));

        // t0 = シーン、t1 = ぼかし終えたブルーム。
        commandList->SetGraphicsRootDescriptorTable(
            kSourceRootParameterIndex, scene.ShaderResourceView());
        commandList->SetGraphicsRootDescriptorTable(
            kBloomRootParameterIndex, m_bloomTexture[0].ShaderResourceView());

        commandList->DrawInstanced(kFullScreenTriangleVertexCount, 1, 0, 0);
    }
}

} // namespace dx12
