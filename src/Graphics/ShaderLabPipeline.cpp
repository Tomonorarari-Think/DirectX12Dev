//=============================================================================
// ShaderLabPipeline.cpp
//   ShaderLabPipeline の実装。
//=============================================================================
#include "ShaderLabPipeline.h"

#include "ShaderCompiler.h"

#include <format>

namespace dx12
{
namespace
{
using namespace DirectX;

/// <summary>習作が共通で使う頂点シェーダー。</summary>
constexpr const wchar_t* kVertexShaderPath = L"shaders/lab/LabVS.hlsl";

/// <summary>画面いっぱいの三角形に要る頂点数。</summary>
constexpr uint32_t kFullScreenTriangleVertexCount = 3;

/// <summary>
/// 習作シェーダーの一覧。
/// </summary>
/// <remarks>
/// **習作を足すときはここに 1 行足すだけ**です。順番がそのまま
/// 画面で切り替わる順番になり、資料の番号とも一致させています。
/// </remarks>
struct ShaderEntry
{
    /// <summary>ファイル名（`shaders/lab/` からの相対）。</summary>
    const wchar_t* fileName;

    /// <summary>表示用の名前。</summary>
    const wchar_t* displayName;
};

constexpr ShaderEntry kShaderFiles[] = {
    { L"01_uv.hlsl",          L"01 UV と座標" },
    { L"02_shapes.hlsl",      L"02 距離関数で図形を描く" },
    { L"03_tiling.hlsl",      L"03 繰り返しと極座標" },
    { L"04_noise.hlsl",       L"04 値ノイズ" },
    { L"05_fbm.hlsl",         L"05 fBm（雲）" },
    { L"06_voronoi.hlsl",     L"06 ボロノイ（セル模様）" },
    { L"07_domainwarp.hlsl",  L"07 ドメインワープ" },
    { L"08_palette.hlsl",     L"08 cos によるカラーパレット" },
    { L"09_plasma.hlsl",      L"09 プラズマ（波の重ね合わせ）" },
    { L"10_water.hlsl",       L"10 水面（法線からの陰影）" },
    { L"11_raymarch.hlsl",    L"11 レイマーチング（SDF）" },
    { L"12_mandelbrot.hlsl",  L"12 マンデルブロ集合" },
    { L"13_truchet.hlsl",     L"13 トルシェタイル" },
    { L"14_effects.hlsl",     L"14 画面効果（走査線・色収差）" },
    { L"15_hash.hlsl",        L"15 ハッシュの精度" },
    { L"16_kaleidoscope.hlsl", L"16 万華鏡" },
    { L"17_fire.hlsl",        L"17 炎" },
    { L"18_starfield.hlsl",   L"18 星空と星雲" },
    { L"19_metaball.hlsl",    L"19 メタボール" },
    { L"20_bezier.hlsl",      L"20 ベジエ曲線" },
    { L"21_glitch.hlsl",      L"21 グリッチ" },
    { L"22_mandelbulb.hlsl",  L"22 マンデルバルブ" },
    { L"23_lensflare.hlsl",   L"23 レンズフレア" },
    { L"24_thinfilm.hlsl",    L"24 薄膜干渉（シャボン玉）" },
    { L"25_lightning.hlsl",   L"25 稲妻" },
    { L"26_woodmarble.hlsl",  L"26 木目と大理石" },
    { L"27_caustics.hlsl",    L"27 コースティクス" },
    { L"28_dissolve.hlsl",    L"28 ディゾルブの作り分け" },
    { L"29_shockwave.hlsl",   L"29 衝撃波（歪み）" },
    { L"30_portal.hlsl",      L"30 魔法陣とポータル" },
    { L"31_shield.hlsl",      L"31 エネルギーシールド" },
    { L"32_explosion.hlsl",   L"32 爆発" },
    { L"33_trail.hlsl",       L"33 軌跡（トレイル）" },
};
} // namespace


/// <summary>
/// 習作シェーダーをすべてコンパイルし、PSO を用意します。
/// </summary>
void ShaderLabPipeline::Initialize(ID3D12Device* device,
                                   DXGI_FORMAT renderTargetFormat,
                                   uint32_t frameCount)
{
    // --- (1) ルートシグネチャ -------------------------------------------------
    //   使うのは定数バッファ 1 本だけ。テクスチャも使わない。
    D3D12_ROOT_PARAMETER rootParameter = {};
    rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameter.Descriptor.ShaderRegister = 0;
    rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

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

    // --- (2) 頂点シェーダーは 1 つを使い回す --------------------------------
    ComPtr<ID3DBlob> vertexShader =
        shader::Compile(ResolveAssetPath(kVertexShaderPath), "VSMain", "vs_5_0");

    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode        = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode        = D3D12_CULL_MODE_NONE;
    rasterizerDesc.DepthClipEnable = FALSE;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable   = FALSE;
    depthStencilDesc.StencilEnable = FALSE;

    // --- (3) 習作ごとにピクセルシェーダーだけを差し替える --------------------
    m_pipelineStates.reserve(std::size(kShaderFiles));
    m_names.reserve(std::size(kShaderFiles));

    for (const ShaderEntry& entry : kShaderFiles)
    {
        const std::wstring path =
            ResolveAssetPath(std::wstring(L"shaders/lab/") + entry.fileName);

        ComPtr<ID3DBlob> pixelShader = shader::Compile(path, "PSMain", "ps_5_0");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature        = m_rootSignature.Get();
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
        psoDesc.DSVFormat             = DXGI_FORMAT_UNKNOWN;
        psoDesc.SampleDesc.Count      = 1;

        ComPtr<ID3D12PipelineState> pipelineState;
        DX_CHECK(device->CreateGraphicsPipelineState(&psoDesc,
                                                     IID_PPV_ARGS(&pipelineState)));

        m_pipelineStates.push_back(std::move(pipelineState));
        m_names.emplace_back(entry.displayName);
    }

    m_constantBuffer.Initialize(device, sizeof(ShaderLabConstants), frameCount);

    Log(std::format(L"習作シェーダーを {} 本読み込みました。", m_pipelineStates.size()));
}


/// <summary>
/// このフレームぶんの値を書き込みます。
/// </summary>
void ShaderLabPipeline::Update(uint32_t frameIndex,
                               float totalSeconds,
                               float deltaSeconds,
                               uint32_t width,
                               uint32_t height,
                               float mouseX,
                               float mouseY,
                               bool mouseDown)
{
    ShaderLabConstants constants = {};
    constants.time       = { totalSeconds, deltaSeconds,
                             static_cast<float>(m_currentIndex), 0.0f };
    constants.resolution = { static_cast<float>(width), static_cast<float>(height),
                             1.0f / static_cast<float>(width),
                             1.0f / static_cast<float>(height) };
    constants.mouse      = { mouseX, mouseY, mouseDown ? 1.0f : 0.0f, 0.0f };

    m_constantBuffer.Update(frameIndex, &constants, sizeof(constants));
}


/// <summary>
/// いま選ばれている習作を描く命令を記録します。
/// </summary>
void ShaderLabPipeline::Record(ID3D12GraphicsCommandList* commandList,
                               uint32_t frameIndex) const
{
    if (m_pipelineStates.empty())
    {
        return;
    }

    commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    commandList->SetPipelineState(m_pipelineStates[m_currentIndex].Get());
    commandList->SetGraphicsRootConstantBufferView(
        0, m_constantBuffer.GpuAddress(frameIndex));

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 0, nullptr);
    commandList->IASetIndexBuffer(nullptr);
    commandList->DrawInstanced(kFullScreenTriangleVertexCount, 1, 0, 0);
}


/// <summary>
/// いま選ばれている習作の名前を返します。
/// </summary>
const std::wstring& ShaderLabPipeline::CurrentName() const
{
    static const std::wstring empty;
    return m_names.empty() ? empty : m_names[m_currentIndex];
}


/// <summary>
/// 表示する習作を選びます。
/// </summary>
void ShaderLabPipeline::Select(int index)
{
    if (m_pipelineStates.empty())
    {
        return;
    }

    const int count = static_cast<int>(m_pipelineStates.size());

    // 範囲外は折り返す。負の数でも正しく回るよう 2 度足している。
    const int wrapped = ((index % count) + count) % count;

    m_currentIndex = static_cast<uint32_t>(wrapped);

    Log(std::format(L"習作 {} / {} : {}",
                    m_currentIndex + 1, count, m_names[m_currentIndex]));
}


/// <summary>
/// 表示する習作を前後に動かします。
/// </summary>
void ShaderLabPipeline::Advance(int delta)
{
    Select(static_cast<int>(m_currentIndex) + delta);
}

} // namespace dx12
