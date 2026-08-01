//=============================================================================
// AutoExposure.cpp
//   AutoExposure の実装。
//=============================================================================
#include "AutoExposure.h"

#include "DescriptorHeap.h"
#include "RenderTexture.h"
#include "ShaderCompiler.h"

#include <format>

namespace dx12
{
namespace
{
/// <summary>シェーダーの場所。</summary>
constexpr const wchar_t* kShaderRelativePath = L"shaders/AutoExposure.hlsl";

/// <summary>目標の明るさ。小さいほど画面が暗くなります。</summary>
constexpr float kTargetLuminance = 0.20f;

/// <summary>露出が追いつく速さ（毎秒）。大きいほど機敏に、小さいほど穏やかに。</summary>
constexpr float kAdaptationSpeed = 1.6f;

/// <summary>露出の下限。明るい場面で暗くしすぎないための歯止め。</summary>
constexpr float kMinimumExposure = 0.25f;

/// <summary>露出の上限。暗い場面で持ち上げすぎないための歯止め。</summary>
constexpr float kMaximumExposure = 6.00f;

/// <summary>対数輝度の下限（2 の何乗か）。これより暗い所は切り上げます。</summary>
constexpr float kMinimumLogLuminance = -10.0f;

/// <summary>対数輝度の上限。</summary>
constexpr float kMaximumLogLuminance = 6.0f;

/// <summary>
/// 対数輝度を整数へ直すときの倍率。
/// </summary>
/// <remarks>
/// `InterlockedAdd` は整数にしか使えないため、固定小数にします。
/// 大きくすると精度が上がる代わりに、合計が uint の上限を超えやすくなります。
/// 1 点あたり最大 (6 + 10) x 256 = 4096。57,600 点で 2.4 億です。
///
/// ★ 1280 x 720 を全部測ると 921,600 点になり、最悪 37.7 億。
///   uint の上限 42.9 億のすぐ手前です。倍率を上げると溢れます。
/// </remarks>
constexpr float kFixedPointScale = 256.0f;

/// <summary>バッファの大きさ（合計 4 バイト ＋ 露出 4 バイト）。</summary>
constexpr uint32_t kStateBufferSize = 8;


/// <summary>シェーダーへ渡す定数。`AutoExposure.hlsl` と一致させること。</summary>
struct AutoExposureConstants
{
    /// <summary>xy = 測る画像の大きさ、z = 測る点の総数、w = 前フレームからの秒。</summary>
    float sourceSize[4];

    /// <summary>x = 目標の明るさ、y = 追従の速さ、z = 露出の下限、w = 上限。</summary>
    float tuning[4];

    /// <summary>x = 対数輝度の下限、y = 上限、z = 固定小数の倍率、w = 未使用。</summary>
    float range[4];
};
} // namespace


/// <summary>
/// 集計用バッファ・UAV / SRV・3 つの PSO を生成します。
/// </summary>
void AutoExposure::Initialize(ID3D12Device* device,
                              DescriptorHeap& descriptorHeap,
                              uint32_t width,
                              uint32_t height,
                              uint32_t frameCount)
{
    Resize(width, height);

    // --- (1) 集計用バッファ ---------------------------------------------------
    //   ★ 中身は「合計」と「露出」の 2 つだけ。8 バイトしかない。
    //     それでもフレームをまたいで GPU 上に残す必要があるので、
    //     DEFAULT ヒープのリソースとして持ちます。
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask     = 1;
    heapProperties.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width            = kStateBufferSize;
    bufferDesc.Height           = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels        = 1;
    bufferDesc.Format           = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    DX_CHECK(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_stateBuffer)));

    m_stateBuffer->SetName(L"自動露出の集計");

    // --- (2) UAV と SRV -------------------------------------------------------
    //   ★ 型を持たない「生バッファ」として見ます。
    //     RWByteAddressBuffer はバイト単位で読み書きするので、形式は R32_TYPELESS。
    const uint32_t uavIndex = descriptorHeap.Allocate();
    const uint32_t srvIndex = descriptorHeap.Allocate();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format                      = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement         = 0;
    uavDesc.Buffer.NumElements          = kStateBufferSize / 4;
    uavDesc.Buffer.StructureByteStride  = 0;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_RAW;

    device->CreateUnorderedAccessView(m_stateBuffer.Get(), nullptr, &uavDesc,
                                      descriptorHeap.CpuHandle(uavIndex));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                     = DXGI_FORMAT_R32_TYPELESS;
    srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement        = 0;
    srvDesc.Buffer.NumElements         = kStateBufferSize / 4;
    srvDesc.Buffer.StructureByteStride = 0;
    srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_RAW;

    device->CreateShaderResourceView(m_stateBuffer.Get(), &srvDesc,
                                     descriptorHeap.CpuHandle(srvIndex));

    m_unorderedAccessView = descriptorHeap.GpuHandle(uavIndex);
    m_shaderResourceView  = descriptorHeap.GpuHandle(srvIndex);

    // --- (3) ルートシグネチャ -------------------------------------------------
    D3D12_DESCRIPTOR_RANGE sourceRange = {};
    sourceRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    sourceRange.NumDescriptors                    = 1;
    sourceRange.BaseShaderRegister                = 0;   // t0
    sourceRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE stateRange = {};
    stateRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    stateRange.NumDescriptors                    = 1;
    stateRange.BaseShaderRegister                = 0;   // u0
    stateRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    rootParameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    rootParameters[1].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges   = &sourceRange;

    rootParameters[2].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[2].DescriptorTable.pDescriptorRanges   = &stateRange;

    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters     = _countof(rootParameters);
    rootSignatureDesc.pParameters       = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers   = &samplerDesc;
    rootSignatureDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

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

    // --- (4) PSO を 3 つ ------------------------------------------------------
    //   ★ 集計は同じ入口を 2 通りにコンパイルする。
    //     ソースは 1 本で、`-D` で切り替える。
    const std::wstring shaderPath = ResolveAssetPath(kShaderRelativePath);

    const wchar_t* const kWaveDefine[] = { L"-D", L"USE_WAVE_INTRINSICS=1" };

    shader::Bytecode waveShader =
        shader::Compile(shaderPath, L"CSAccumulate", shader::kComputeShaderTarget,
                        kWaveDefine, _countof(kWaveDefine));

    shader::Bytecode sharedShader =
        shader::Compile(shaderPath, L"CSAccumulate", shader::kComputeShaderTarget);

    shader::Bytecode resolveShader =
        shader::Compile(shaderPath, L"CSResolve", shader::kComputeShaderTarget);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();

    psoDesc.CS = { waveShader->GetBufferPointer(), waveShader->GetBufferSize() };
    DX_CHECK(device->CreateComputePipelineState(
        &psoDesc, IID_PPV_ARGS(&m_wavePipelineState)));

    psoDesc.CS = { sharedShader->GetBufferPointer(), sharedShader->GetBufferSize() };
    DX_CHECK(device->CreateComputePipelineState(
        &psoDesc, IID_PPV_ARGS(&m_groupSharedPipelineState)));

    psoDesc.CS = { resolveShader->GetBufferPointer(), resolveShader->GetBufferSize() };
    DX_CHECK(device->CreateComputePipelineState(
        &psoDesc, IID_PPV_ARGS(&m_resolvePipelineState)));

    // --- (5) 定数バッファ -----------------------------------------------------
    m_constantBuffer.Initialize(device, sizeof(AutoExposureConstants), frameCount);

    Log(std::format(L"自動露出を構築しました（{} x {} を測ります）。",
                    m_sampleWidth, m_sampleHeight));
}


/// <summary>
/// 画面の大きさが変わったときに、測る範囲を作り直します。
/// </summary>
void AutoExposure::Resize(uint32_t width, uint32_t height)
{
    m_screenWidth  = width;
    m_screenHeight = height;

    const uint32_t divisor = (m_sampleDivisor > 0) ? m_sampleDivisor : 1;

    m_sampleWidth  = (width > 0) ? ((width + divisor - 1) / divisor) : 1;
    m_sampleHeight = (height > 0) ? ((height + divisor - 1) / divisor) : 1;
}


/// <summary>
/// 測る解像度を 1/4 → 1/2 → 1/1 と切り替えます。
/// </summary>
uint32_t AutoExposure::CycleSampleDivisor()
{
    m_sampleDivisor = (m_sampleDivisor > 1) ? (m_sampleDivisor / 2) : 4;

    Resize(m_screenWidth, m_screenHeight);

    return m_sampleDivisor;
}


/// <summary>
/// このフレームぶんの定数を書き込みます。
/// </summary>
void AutoExposure::Update(uint32_t frameIndex, float deltaSeconds)
{
    AutoExposureConstants constants = {};

    const float sampleCount =
        static_cast<float>(m_sampleWidth) * static_cast<float>(m_sampleHeight);

    constants.sourceSize[0] = static_cast<float>(m_sampleWidth);
    constants.sourceSize[1] = static_cast<float>(m_sampleHeight);
    constants.sourceSize[2] = sampleCount;
    constants.sourceSize[3] = deltaSeconds;

    constants.tuning[0] = kTargetLuminance;
    constants.tuning[1] = kAdaptationSpeed;
    constants.tuning[2] = kMinimumExposure;
    constants.tuning[3] = kMaximumExposure;

    constants.range[0] = kMinimumLogLuminance;
    constants.range[1] = kMaximumLogLuminance;
    constants.range[2] = kFixedPointScale;
    constants.range[3] = 0.0f;

    m_constantBuffer.Update(frameIndex, &constants, sizeof(constants));
}


/// <summary>
/// 明るさを測り、露出を決める命令を記録します。
/// </summary>
void AutoExposure::Record(ID3D12GraphicsCommandList* commandList,
                          uint32_t frameIndex,
                          const RenderTexture& sceneTexture,
                          ReductionMode mode)
{
    // 前のフレームは「後処理が読める状態」で終わっている。書ける状態へ戻す。
    if (m_recorded)
    {
        D3D12_RESOURCE_BARRIER toWrite = {};
        toWrite.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toWrite.Transition.pResource   = m_stateBuffer.Get();
        toWrite.Transition.StateBefore =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        toWrite.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toWrite.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        commandList->ResourceBarrier(1, &toWrite);
    }

    m_recorded = true;

    commandList->SetComputeRootSignature(m_rootSignature.Get());

    commandList->SetComputeRootConstantBufferView(
        0, m_constantBuffer.GpuAddress(frameIndex));

    commandList->SetComputeRootDescriptorTable(1, sceneTexture.ShaderResourceView());
    commandList->SetComputeRootDescriptorTable(2, m_unorderedAccessView);

    // --- 第 1 段 : 画面を分担して測る -----------------------------------------
    commandList->SetPipelineState((mode == ReductionMode::Wave)
                                      ? m_wavePipelineState.Get()
                                      : m_groupSharedPipelineState.Get());

    const uint32_t groupsX = (m_sampleWidth + kThreadsX - 1) / kThreadsX;
    const uint32_t groupsY = (m_sampleHeight + kThreadsY - 1) / kThreadsY;

    commandList->Dispatch(groupsX, groupsY, 1);

    // ★ 第 2 段は第 1 段の結果を読む。UAV バリアで書き終わりを待たせる。
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_stateBuffer.Get();

    commandList->ResourceBarrier(1, &uavBarrier);

    // --- 第 2 段 : 露出を決める -----------------------------------------------
    commandList->SetPipelineState(m_resolvePipelineState.Get());
    commandList->Dispatch(1, 1, 1);

    // 後処理が読めるようにする。
    D3D12_RESOURCE_BARRIER transition = {};
    transition.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition.pResource   = m_stateBuffer.Get();
    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(1, &transition);
}

} // namespace dx12
