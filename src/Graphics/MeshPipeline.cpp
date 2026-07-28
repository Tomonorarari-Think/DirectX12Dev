//=============================================================================
// MeshPipeline.cpp
//   MeshPipeline の実装。
//=============================================================================
#include "MeshPipeline.h"

#include "CommandQueue.h"
#include "DescriptorHeap.h"
#include "Geometry.h"
#include "ShaderCompiler.h"

#include <cstddef>  // offsetof
#include <stdexcept>

namespace dx12
{
namespace
{
/// <summary>
/// シェーダーファイルの場所（プロジェクトルートからの相対パス）。
/// </summary>
constexpr const wchar_t* kShaderRelativePath = L"shaders/Mesh.hlsl";

/// <summary>
/// 平行光源が進む向き（ワールド空間）。左上手前から差し込む設定。
/// </summary>
/// <remarks>
/// 「光源がある方向」ではなく「光が飛んでいく向き」です。正規化は更新時に行います。
/// 立方体が回っても光は動かないため、面の明るさが移り変わる様子が観察できます。
/// </remarks>
constexpr float kLightDirection[3] = { 0.55f, -0.75f, 0.35f };

/// <summary>
/// 光の色と強さ。1.0 を超えると白飛びします。
/// </summary>
constexpr float kLightColor[3] = { 1.0f, 0.96f, 0.88f };

/// <summary>
/// 環境光（IBL）の強さ。
/// </summary>
/// <remarks>
/// 21 章より前は「定数で近似した環境光」の値でしたが、いまは環境マップから
/// 求めた明るさに掛ける倍率です。1.0 が「環境マップのとおり」を意味します。
/// </remarks>
constexpr float kAmbientIntensity = 1.0f;

/// <summary>
/// 光の強さ。
/// </summary>
/// <remarks>
/// シェーダーが拡散反射を pi で割るため、そのままだと全体が 1/3 ほど暗くなります。
/// 埋め合わせに pi を掛けています。本来は光源の物理量（ルクスなど）を入れる場所です。
/// </remarks>
constexpr float kLightIntensity = 3.14159265f;

/// <summary>
/// フレーム共通の定数バッファを結び付けるルートパラメータの番号。
/// </summary>
constexpr uint32_t kFrameConstantsRootParameterIndex = 0;

/// <summary>
/// オブジェクト別の定数バッファを結び付けるルートパラメータの番号。
/// </summary>
constexpr uint32_t kObjectConstantsRootParameterIndex = 1;

/// <summary>
/// テクスチャ（SRV）を結び付けるルートパラメータの番号。
/// </summary>
constexpr uint32_t kTextureRootParameterIndex = 2;

/// <summary>
/// シャドウマップ（SRV）を結び付けるルートパラメータの番号。
/// </summary>
constexpr uint32_t kShadowMapRootParameterIndex = 3;

/// <summary>
/// 材質別の定数バッファを結び付けるルートパラメータの番号。
/// </summary>
constexpr uint32_t kMaterialConstantsRootParameterIndex = 4;

/// <summary>
/// 金属らしさ・粗さのテクスチャを結び付けるルートパラメータの番号。
/// </summary>
constexpr uint32_t kMetallicRoughnessRootParameterIndex = 5;

/// <summary>
/// 環境マップ（映り込み用）を結び付けるルートパラメータの番号。
/// </summary>
constexpr uint32_t kEnvironmentRootParameterIndex = 6;

/// <summary>
/// 積分済みの環境光を結び付けるルートパラメータの番号。
/// </summary>
constexpr uint32_t kIrradianceRootParameterIndex = 7;

/// <summary>
/// 法線マップを結び付けるルートパラメータの番号。
/// </summary>
constexpr uint32_t kNormalMapRootParameterIndex = 8;

/// <summary>
/// シャドウマップを描くときに深度へ加える下駄（整数バイアス）。
/// </summary>
/// <remarks>
/// 影を落とす面が自分自身を影と判定してしまう「シャドウアクネ」を防ぎます。
/// ラスタライザが深度を書く時点で加算されるので、シェーダー側で足すより正確です。
/// </remarks>
constexpr INT kShadowDepthBias = 3000;

/// <summary>
/// 面の傾きに比例して深度へ加える下駄。
/// </summary>
/// <remarks>
/// 光に対して斜めな面ほど、1 テクセルの中での深度差が大きくなります。
/// 傾きに応じて増える下駄を足すことで、面の角度によらず影が安定します。
/// </remarks>
constexpr float kShadowSlopeScaledDepthBias = 2.5f;

} // namespace


/// <summary>
/// ルートシグネチャ・PSO・定数バッファ・テクスチャを生成します。
/// </summary>
void MeshPipeline::Initialize(ID3D12Device* device,
                              DXGI_FORMAT renderTargetFormat,
                              DXGI_FORMAT depthStencilFormat,
                              uint32_t frameCount,
                              uint32_t maxObjectCount,
                              DXGI_FORMAT shadowMapFormat)
{
    m_maxObjectCount = maxObjectCount;

    CreateRootSignature(device);
    CreateShadowRootSignature(device);

    // 影を描く PSO も、画面を描く PSO と同じ入力レイアウトを使う。
    // そのため入力レイアウトの定義は CreatePipelineState 側から渡してもらう。
    CreatePipelineState(device, renderTargetFormat, depthStencilFormat, shadowMapFormat);

    // カメラとライトはフレームごとに 1 組あればよい。
    m_frameConstantBuffer.Initialize(device, sizeof(FrameConstants), frameCount);

    // 変換行列はオブジェクトごとに違うため、フレーム数 × オブジェクト数ぶん要る。
    //   GPU が前フレームを読んでいる最中に上書きしないためにフレーム別、
    //   1 フレームの中で描く順に上書きしないためにオブジェクト別。
    m_objectConstantBuffer.Initialize(
        device, sizeof(ObjectConstants), frameCount * maxObjectCount);

    Log(L"メッシュ描画パイプラインを構築しました。");
}


/// <summary>
/// ルートシグネチャを生成します。
/// </summary>
void MeshPipeline::CreateRootSignature(ID3D12Device* device)
{
    // ルートパラメータ : シェーダーへ何を渡すかの定義。関数の引数リストに相当する。
    D3D12_ROOT_PARAMETER rootParameters[9] = {};

    // 0 番 : フレーム共通の定数バッファ（カメラとライト）
    rootParameters[kFrameConstantsRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_CBV;

    // ShaderRegister = 0 は HLSL 側の register(b0) に対応する。
    rootParameters[kFrameConstantsRootParameterIndex].Descriptor.ShaderRegister = 0;
    rootParameters[kFrameConstantsRootParameterIndex].Descriptor.RegisterSpace  = 0;

    // ShaderVisibility : どのシェーダー段から見えるようにするか。
    //   変換行列は頂点シェーダー、ライト情報はピクセルシェーダーが読むため ALL にする。
    rootParameters[kFrameConstantsRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;

    // 1 番 : オブジェクト別の定数バッファ（変換行列）
    rootParameters[kObjectConstantsRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kObjectConstantsRootParameterIndex].Descriptor.ShaderRegister = 1;
    rootParameters[kObjectConstantsRootParameterIndex].Descriptor.RegisterSpace  = 0;

    // 行列を使うのは頂点シェーダーだけなので VERTEX に絞る。
    // 可視性は狭いほど GPU の負担が軽くなる。
    rootParameters[kObjectConstantsRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_VERTEX;

    // 2 番 : テクスチャ（ディスクリプタテーブル）
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors     = 1;
    srvRange.BaseShaderRegister = 0;   // HLSL の register(t0) に対応
    srvRange.RegisterSpace      = 0;

    // テーブル先頭からのオフセット。APPEND は「直前のレンジの続きから」の意味。
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[kTextureRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kTextureRootParameterIndex].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[kTextureRootParameterIndex].DescriptorTable.pDescriptorRanges   = &srvRange;

    // テクスチャを読むのはピクセルシェーダーだけなので PIXEL に限定する。
    rootParameters[kTextureRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    // 3 番 : シャドウマップ（ディスクリプタテーブル）
    //   基本色テクスチャと 1 つのテーブルにまとめることもできるが、
    //   その場合はヒープ上で連続していなければならない。別々にしておくと
    //   確保した順序に依存しない。
    D3D12_DESCRIPTOR_RANGE shadowRange = {};
    shadowRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowRange.NumDescriptors     = 1;
    shadowRange.BaseShaderRegister = 1;   // HLSL の register(t1) に対応
    shadowRange.RegisterSpace      = 0;
    shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[kShadowMapRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kShadowMapRootParameterIndex].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[kShadowMapRootParameterIndex].DescriptorTable.pDescriptorRanges =
        &shadowRange;
    rootParameters[kShadowMapRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    // 4 番 : 材質別の定数バッファ（基本色）
    //   サブメッシュを描く直前に差し替える。読むのはピクセルシェーダーだけ。
    rootParameters[kMaterialConstantsRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kMaterialConstantsRootParameterIndex].Descriptor.ShaderRegister = 2;
    rootParameters[kMaterialConstantsRootParameterIndex].Descriptor.RegisterSpace  = 0;
    rootParameters[kMaterialConstantsRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    // 5 番 : 金属らしさ・粗さのテクスチャ（ディスクリプタテーブル）
    D3D12_DESCRIPTOR_RANGE metallicRoughnessRange = {};
    metallicRoughnessRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    metallicRoughnessRange.NumDescriptors     = 1;
    metallicRoughnessRange.BaseShaderRegister = 2;   // HLSL の register(t2) に対応
    metallicRoughnessRange.RegisterSpace      = 0;
    metallicRoughnessRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[kMetallicRoughnessRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kMetallicRoughnessRootParameterIndex]
        .DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[kMetallicRoughnessRootParameterIndex]
        .DescriptorTable.pDescriptorRanges = &metallicRoughnessRange;
    rootParameters[kMetallicRoughnessRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    // 6 番・7 番 : 環境マップと、積分済みの環境光
    //   材質ではなくシーン全体で共通なので、Bind で 1 度だけ結び付ける。
    D3D12_DESCRIPTOR_RANGE environmentRange = {};
    environmentRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    environmentRange.NumDescriptors     = 1;
    environmentRange.BaseShaderRegister = 3;   // register(t3)
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

    D3D12_DESCRIPTOR_RANGE irradianceRange = {};
    irradianceRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    irradianceRange.NumDescriptors     = 1;
    irradianceRange.BaseShaderRegister = 4;   // register(t4)
    irradianceRange.RegisterSpace      = 0;
    irradianceRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[kIrradianceRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kIrradianceRootParameterIndex]
        .DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[kIrradianceRootParameterIndex]
        .DescriptorTable.pDescriptorRanges = &irradianceRange;
    rootParameters[kIrradianceRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    // 8 番 : 法線マップ（ディスクリプタテーブル）
    //   材質ごとに違うので、BindMaterial で差し替える。
    D3D12_DESCRIPTOR_RANGE normalMapRange = {};
    normalMapRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    normalMapRange.NumDescriptors     = 1;
    normalMapRange.BaseShaderRegister = 5;   // register(t5)
    normalMapRange.RegisterSpace      = 0;
    normalMapRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[kNormalMapRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kNormalMapRootParameterIndex]
        .DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[kNormalMapRootParameterIndex]
        .DescriptorTable.pDescriptorRanges = &normalMapRange;
    rootParameters[kNormalMapRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;

    // 静的サンプラー (Static Sampler)
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};

    // s0 : 基本色テクスチャ用
    //   Filter : ピクセルとテクセルがぴったり一致しないときの読み方。
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

    //   AddressU/V/W : UV が 0〜1 の外に出たときの扱い。
    //   床は UV を 1 より大きくしているので、WRAP によって模様が繰り返される。
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    staticSamplers[0].MipLODBias       = 0.0f;
    staticSamplers[0].MaxAnisotropy    = 0;
    staticSamplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    staticSamplers[0].MinLOD           = 0.0f;
    staticSamplers[0].MaxLOD           = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister   = 0;   // HLSL の register(s0) に対応
    staticSamplers[0].RegisterSpace    = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1 : シャドウマップ用の「比較サンプラー」
    //   COMPARISON 系のフィルタは、読んだ値をそのまま返すのではなく
    //   渡した値と比較し、「合格した割合」を返します。周囲 4 テクセルを
    //   比較して補間するので、1 回の読み取りで影の境目がなめらかになります。
    staticSamplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;

    //   シャドウマップの外側は「影ではない」ことにしたいので BORDER + 白。
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;

    staticSamplers[1].MipLODBias       = 0.0f;
    staticSamplers[1].MaxAnisotropy    = 0;
    staticSamplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[1].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[1].MinLOD           = 0.0f;
    staticSamplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister   = 1;   // HLSL の register(s1) に対応
    staticSamplers[1].RegisterSpace    = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters     = _countof(rootParameters);
    rootSignatureDesc.pParameters       = rootParameters;
    rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);
    rootSignatureDesc.pStaticSamplers   = staticSamplers;

    // ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT フラグ
    //   「頂点バッファから入力レイアウト経由で頂点を読み込む」ことを許可します。
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // ルートシグネチャは 2 段階で作ります。
    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> errorBlob;

    const HRESULT hr = ::D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSignature,
        &errorBlob);

    if (FAILED(hr))
    {
        // errorBlob には失敗理由が ASCII 文字列で入っている。
        if (errorBlob != nullptr)
        {
            const char* message = static_cast<const char*>(errorBlob->GetBufferPointer());
            LogError(L"ルートシグネチャのシリアライズに失敗しました:");
            ::OutputDebugStringA(message);
            ::OutputDebugStringA("\n");
        }
        DX_CHECK(hr); // ここで例外を投げる
    }

    DX_CHECK(device->CreateRootSignature(
        0,                                            // NodeMask（GPU 1 台なら 0）
        serializedRootSignature->GetBufferPointer(),
        serializedRootSignature->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)));
}


/// <summary>
/// シャドウマップ描画用の、より狭いルートシグネチャを生成します。
/// </summary>
void MeshPipeline::CreateShadowRootSignature(ID3D12Device* device)
{
    // 影の形を作るのに要るのは変換行列だけ。テクスチャもライトも読まない。
    // ルートパラメータの番号は画面描画側と揃えてあるので、BindObject を共用できる。
    D3D12_ROOT_PARAMETER rootParameters[2] = {};

    rootParameters[kFrameConstantsRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kFrameConstantsRootParameterIndex].Descriptor.ShaderRegister = 0;
    rootParameters[kFrameConstantsRootParameterIndex].Descriptor.RegisterSpace  = 0;
    rootParameters[kFrameConstantsRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_VERTEX;

    rootParameters[kObjectConstantsRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kObjectConstantsRootParameterIndex].Descriptor.ShaderRegister = 1;
    rootParameters[kObjectConstantsRootParameterIndex].Descriptor.RegisterSpace  = 0;
    rootParameters[kObjectConstantsRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters     = _countof(rootParameters);
    rootSignatureDesc.pParameters       = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers   = nullptr;

    // DENY_*_ROOT_ACCESS : その段からは一切見せない、という宣言。
    //   使わない段を明示的に閉じると、GPU が扱うデータが減って軽くなる。
    rootSignatureDesc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> errorBlob;

    const HRESULT hr = ::D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSignature,
        &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob != nullptr)
        {
            const char* message = static_cast<const char*>(errorBlob->GetBufferPointer());
            LogError(L"影用ルートシグネチャのシリアライズに失敗しました:");
            ::OutputDebugStringA(message);
            ::OutputDebugStringA("\n");
        }
        DX_CHECK(hr);
    }

    DX_CHECK(device->CreateRootSignature(
        0,
        serializedRootSignature->GetBufferPointer(),
        serializedRootSignature->GetBufferSize(),
        IID_PPV_ARGS(&m_shadowRootSignature)));
}


/// <summary>
/// HLSL をコンパイルし、パイプラインステートオブジェクトを生成します。
/// </summary>
void MeshPipeline::CreatePipelineState(ID3D12Device* device,
                                       DXGI_FORMAT renderTargetFormat,
                                       DXGI_FORMAT depthStencilFormat,
                                       DXGI_FORMAT shadowMapFormat)
{
    // (1) シェーダーのコンパイル
    //   "vs_5_0" の意味 : vs = Vertex Shader、5_0 = シェーダーモデル 5.0
    //   "ps_5_0" の意味 : ps = Pixel Shader
    const std::wstring shaderPath = ResolveAssetPath(kShaderRelativePath);
    Log(L"シェーダーを読み込みます: " + shaderPath);

    ComPtr<ID3DBlob> vertexShader = shader::Compile(shaderPath, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> pixelShader  = shader::Compile(shaderPath, "PSMain", "ps_5_0");

    // (2) 入力レイアウト
    //   「頂点バッファのバイト列を、どう切り分けてシェーダーに渡すか」の定義。
    const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        {
            "POSITION",                                  // シェーダー側の : POSITION に対応
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,                 // float3
            0,                                           // 0 番スロットの頂点バッファ
            0,                                           // 先頭から 0 バイト目
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        },
        {
            "NORMAL",                                    // シェーダー側の : NORMAL に対応
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,                 // float3
            0,
            12,                                          // position の 12 バイト後ろ
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        },
        {
            "COLOR",                                     // シェーダー側の : COLOR に対応
            0,
            DXGI_FORMAT_R32G32B32A32_FLOAT,              // float4
            0,
            24,                                          // position(12) + normal(12) の後ろ
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        },
        {
            "TEXCOORD",                                  // シェーダー側の : TEXCOORD に対応
            0,
            DXGI_FORMAT_R32G32_FLOAT,                    // float2
            0,
            40,                                          // + color(16) の後ろ
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        },
        {
            "TANGENT",                                   // シェーダー側の : TANGENT に対応
            0,
            DXGI_FORMAT_R32G32B32A32_FLOAT,              // float4（w は従接線の向き）
            0,
            48,                                          // + uv(8) の後ろ
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        },
    };

    // オフセットは頂点構造体の並びと 1 バイトもずれてはいけない。
    // 手で足した数値が Vertex と食い違っていないことを、ここで機械的に確かめる。
    static_assert(sizeof(Vertex) == 64, "Vertex のサイズが入力レイアウトの前提と違います");
    static_assert(offsetof(Vertex, normal)  == 12, "NORMAL のオフセットが違います");
    static_assert(offsetof(Vertex, color)   == 24, "COLOR のオフセットが違います");
    static_assert(offsetof(Vertex, uv)      == 40, "TEXCOORD のオフセットが違います");
    static_assert(offsetof(Vertex, tangent) == 48, "TANGENT のオフセットが違います");

    // (3) ラスタライザステート
    //   頂点を「ピクセルの集合」に変換する段の設定。
    D3D12_RASTERIZER_DESC rasterizerDesc = {};

    // SOLID = 面を塗りつぶす。WIREFRAME にすると輪郭線だけになる（デバッグに便利）
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    // CullMode : 見えない面を描画前に捨てる設定（カリング）。
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

    // FALSE = 時計回りの面を表とする（DirectX の伝統的な既定）
    rasterizerDesc.FrontCounterClockwise = FALSE;

    rasterizerDesc.DepthBias             = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizerDesc.DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerDesc.SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;

    // 視錐台の手前・奥からはみ出た部分を切り取る
    rasterizerDesc.DepthClipEnable       = TRUE;

    rasterizerDesc.MultisampleEnable     = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount     = 0;
    rasterizerDesc.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // (4) ブレンドステート
    //   「これから描く色」と「すでに描かれている色」をどう混ぜるかの設定。
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable  = FALSE;
    blendDesc.IndependentBlendEnable = FALSE; // 全レンダーターゲットで同じ設定を使う

    D3D12_RENDER_TARGET_BLEND_DESC& rtBlend = blendDesc.RenderTarget[0];
    rtBlend.BlendEnable   = FALSE;  // ブレンドしない（そのまま上書き）
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend      = D3D12_BLEND_ONE;
    rtBlend.DestBlend     = D3D12_BLEND_ZERO;
    rtBlend.BlendOp       = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha  = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtBlend.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp        = D3D12_LOGIC_OP_NOOP;

    // RGBA すべてのチャンネルに書き込みを許可する
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // (5) 深度ステンシルステート
    //   奥行き判定（手前のものが奥のものを隠す処理）の設定。
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};

    // DepthEnable : 深度テストを行うか。
    depthStencilDesc.DepthEnable = TRUE;

    // DepthWriteMask : 深度テストに合格したとき、深度バッファを更新するか。
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

    // DepthFunc : 「合格」の条件。新しい深度値 op 記録済みの深度値。
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    // ステンシル（型抜きや輪郭描画に使う付加機能）は今回使いません。
    depthStencilDesc.StencilEnable = FALSE;

    // (6) PSO の組み立て
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    psoDesc.pRootSignature = m_rootSignature.Get();

    // D3D12_SHADER_BYTECODE は「バイトコードの先頭アドレスとサイズ」を持つ構造体
    psoDesc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
    psoDesc.VS.BytecodeLength  = vertexShader->GetBufferSize();
    psoDesc.PS.pShaderBytecode = pixelShader->GetBufferPointer();
    psoDesc.PS.BytecodeLength  = pixelShader->GetBufferSize();

    psoDesc.InputLayout.pInputElementDescs = inputElements;
    psoDesc.InputLayout.NumElements        = _countof(inputElements);

    psoDesc.RasterizerState   = rasterizerDesc;
    psoDesc.BlendState        = blendDesc;
    psoDesc.DepthStencilState = depthStencilDesc;

    // SampleMask : どのマルチサンプルを有効にするかのビットマスク。
    psoDesc.SampleMask = UINT_MAX;

    // 描画するプリミティブの種類。POINT / LINE / TRIANGLE / PATCH から選ぶ。
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // 出力先（レンダーターゲット）の枚数と形式。
    psoDesc.NumRenderTargets = 1;
    // ★ ここに渡すのは RTV の形式（_SRGB 付き）です。
    //   スワップチェーン本体の形式を渡すと、PSO の生成は通るのに
    //   実行時に「形式が合わない」とデバッグレイヤーに怒られます。
    psoDesc.RTVFormats[0]    = renderTargetFormat;

    // 深度バッファの形式。DepthBuffer::kFormat と一致していないとエラーになります。
    psoDesc.DSVFormat = depthStencilFormat;

    // MSAA 無し
    psoDesc.SampleDesc.Count   = 1;
    psoDesc.SampleDesc.Quality = 0;

    psoDesc.NodeMask = 0;
    psoDesc.Flags    = D3D12_PIPELINE_STATE_FLAG_NONE;

    DX_CHECK(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

    // (7) 影用の PSO も、同じ入力レイアウトで作る
    CreateShadowPipelineState(device, shadowMapFormat, inputElements, _countof(inputElements));
}


/// <summary>
/// シャドウマップ描画用の PSO（深度だけを書く設定）を生成します。
/// </summary>
void MeshPipeline::CreateShadowPipelineState(ID3D12Device* device,
                                             DXGI_FORMAT shadowMapFormat,
                                             const D3D12_INPUT_ELEMENT_DESC* inputElements,
                                             uint32_t inputElementCount)
{
    const std::wstring shaderPath = ResolveAssetPath(kShaderRelativePath);

    // ★ 頂点シェーダーだけをコンパイルする。ピクセルシェーダーは無し。
    //   色は要らず、深度だけ書ければよいため。
    ComPtr<ID3DBlob> vertexShader = shader::Compile(shaderPath, "VSShadow", "vs_5_0");

    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode              = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode              = D3D12_CULL_MODE_BACK;
    rasterizerDesc.FrontCounterClockwise = FALSE;

    // ★ 深度に下駄を履かせる（シャドウアクネ対策）。
    //   自分自身との比較で「わずかに奥」と判定されるのを防ぐ。
    rasterizerDesc.DepthBias            = kShadowDepthBias;
    rasterizerDesc.DepthBiasClamp       = 0.0f;
    rasterizerDesc.SlopeScaledDepthBias = kShadowSlopeScaledDepthBias;

    rasterizerDesc.DepthClipEnable       = TRUE;
    rasterizerDesc.MultisampleEnable     = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount     = 0;
    rasterizerDesc.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable    = TRUE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    depthStencilDesc.StencilEnable  = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    psoDesc.pRootSignature     = m_shadowRootSignature.Get();
    psoDesc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
    psoDesc.VS.BytecodeLength  = vertexShader->GetBufferSize();

    psoDesc.InputLayout.pInputElementDescs = inputElements;
    psoDesc.InputLayout.NumElements        = inputElementCount;

    psoDesc.RasterizerState   = rasterizerDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.SampleMask        = UINT_MAX;

    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // ★ 色を出力しないので、レンダーターゲットは 0 枚。
    psoDesc.NumRenderTargets = 0;
    psoDesc.DSVFormat        = shadowMapFormat;

    psoDesc.SampleDesc.Count   = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.NodeMask           = 0;
    psoDesc.Flags              = D3D12_PIPELINE_STATE_FLAG_NONE;

    DX_CHECK(device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_shadowPipelineState)));
}


/// <summary>
/// このフレームの共通定数（カメラとライト）を書き込みます。
/// </summary>
void MeshPipeline::UpdateFrameConstants(uint32_t frameIndex,
                                        const DirectX::XMMATRIX& viewProjection,
                                        const DirectX::XMFLOAT3& cameraPosition,
                                        const DirectX::XMMATRIX& lightViewProjection)
{
    using namespace DirectX;

    FrameConstants constants = {};

    // HLSL は定数バッファの行列を列優先で読むため、転置してから書き込む。
    XMStoreFloat4x4(&constants.viewProjection, XMMatrixTranspose(viewProjection));
    XMStoreFloat4x4(&constants.lightViewProjection, XMMatrixTranspose(lightViewProjection));

    // ライトの向きは長さ 1 でなければ内積が明るさにならない。
    const XMVECTOR lightDirection = XMVector3Normalize(
        XMVectorSet(kLightDirection[0], kLightDirection[1], kLightDirection[2], 0.0f));

    XMStoreFloat4(&constants.lightDirection, lightDirection);

    // w には環境光の強さを同居させている（16 バイトの空きを無駄にしないため）。
    //   rgb には強さを掛けておく。シェーダー側で拡散反射を pi で割るため、
    //   その埋め合わせに pi 相当の値を使っている。
    constants.lightColor = { kLightColor[0] * kLightIntensity,
                             kLightColor[1] * kLightIntensity,
                             kLightColor[2] * kLightIntensity,
                             kAmbientIntensity };

    constants.cameraPosition = { cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f };

    m_frameConstantBuffer.Update(frameIndex, &constants, sizeof(constants));
}


/// <summary>
/// オブジェクト 1 個ぶんの定数（変換行列）を書き込みます。
/// </summary>
void MeshPipeline::UpdateObjectConstants(uint32_t frameIndex,
                                         uint32_t objectIndex,
                                         const DirectX::XMMATRIX& world,
                                         const DirectX::XMMATRIX& viewProjection)
{
    using namespace DirectX;

    if (objectIndex >= m_maxObjectCount)
    {
        throw std::out_of_range("オブジェクト数が maxObjectCount を超えました。");
    }

    ObjectConstants constants = {};

    // 行列の合成は CPU 側で済ませる。頂点が何万個あっても合成は 1 回で足りるため。
    XMStoreFloat4x4(&constants.worldViewProjection,
                    XMMatrixTranspose(world * viewProjection));

    // ワールド行列も単体で渡す。法線をワールド空間へ移すのに必要なため。
    XMStoreFloat4x4(&constants.world, XMMatrixTranspose(world));

    m_objectConstantBuffer.Update(
        ObjectSlot(frameIndex, objectIndex), &constants, sizeof(constants));
}


/// <summary>
/// 描画の共通設定（PSO・ルートシグネチャ・フレーム定数・テクスチャ）を記録します。
/// </summary>
void MeshPipeline::Bind(ID3D12GraphicsCommandList* commandList,
                        uint32_t frameIndex,
                        D3D12_GPU_DESCRIPTOR_HANDLE shadowMapView,
                        D3D12_GPU_DESCRIPTOR_HANDLE environmentView,
                        D3D12_GPU_DESCRIPTOR_HANDLE irradianceView) const
{
    // コマンドリストは Reset するたびに「PSO 未設定」の状態に戻ります。
    commandList->SetPipelineState(m_pipelineState.Get());
    commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    // ルートディスクリプタなので、ディスクリプタヒープを経由せず GPU アドレスを直接渡せる。
    commandList->SetGraphicsRootConstantBufferView(
        kFrameConstantsRootParameterIndex,
        m_frameConstantBuffer.GpuAddress(frameIndex));

    // シャドウマップは、直前のパスで書き終えたものをそのまま読む。
    //   ★ 前提として、呼び出し側が SetDescriptorHeaps() で
    //     シェーダー可視ヒープを設定しておく必要があります（Renderer が行う）。
    commandList->SetGraphicsRootDescriptorTable(
        kShadowMapRootParameterIndex, shadowMapView);

    // 環境マップと積分済みの環境光。シーン全体で共通なのでここで結び付ける。
    commandList->SetGraphicsRootDescriptorTable(
        kEnvironmentRootParameterIndex, environmentView);

    commandList->SetGraphicsRootDescriptorTable(
        kIrradianceRootParameterIndex, irradianceView);

    // TRIANGLELIST : 3 頂点ごとに独立した三角形を作る。
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}


/// <summary>
/// シャドウマップを描くための共通設定を記録します。
/// </summary>
void MeshPipeline::BindShadowPass(ID3D12GraphicsCommandList* commandList,
                                  uint32_t frameIndex) const
{
    commandList->SetPipelineState(m_shadowPipelineState.Get());

    // ★ ルートシグネチャを変えると、それまでに結び付けた値は全て無効になる。
    //   そのため定数バッファはこの後で改めて設定する。
    commandList->SetGraphicsRootSignature(m_shadowRootSignature.Get());

    commandList->SetGraphicsRootConstantBufferView(
        kFrameConstantsRootParameterIndex,
        m_frameConstantBuffer.GpuAddress(frameIndex));

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}


/// <summary>
/// これから描くサブメッシュの材質を結び付けます。
/// </summary>
void MeshPipeline::BindMaterial(ID3D12GraphicsCommandList* commandList,
                                D3D12_GPU_VIRTUAL_ADDRESS constantAddress,
                                D3D12_GPU_DESCRIPTOR_HANDLE textureView,
                                D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessView,
                                D3D12_GPU_DESCRIPTOR_HANDLE normalMapView) const
{
    commandList->SetGraphicsRootConstantBufferView(
        kMaterialConstantsRootParameterIndex, constantAddress);

    commandList->SetGraphicsRootDescriptorTable(
        kTextureRootParameterIndex, textureView);

    commandList->SetGraphicsRootDescriptorTable(
        kMetallicRoughnessRootParameterIndex, metallicRoughnessView);

    commandList->SetGraphicsRootDescriptorTable(
        kNormalMapRootParameterIndex, normalMapView);
}


/// <summary>
/// 平行光源が進む向きを返します。
/// </summary>
DirectX::XMFLOAT3 MeshPipeline::LightDirection()
{
    return { kLightDirection[0], kLightDirection[1], kLightDirection[2] };
}


/// <summary>
/// 環境光（IBL）の強さを返します。
/// </summary>
float MeshPipeline::AmbientIntensity()
{
    return kAmbientIntensity;
}


/// <summary>
/// これから描くオブジェクトの定数を結び付けます。
/// </summary>
void MeshPipeline::BindObject(ID3D12GraphicsCommandList* commandList,
                              uint32_t frameIndex,
                              uint32_t objectIndex) const
{
    // オブジェクトごとに違うのはここだけ。PSO の切り替えより遥かに軽い。
    commandList->SetGraphicsRootConstantBufferView(
        kObjectConstantsRootParameterIndex,
        m_objectConstantBuffer.GpuAddress(ObjectSlot(frameIndex, objectIndex)));
}

} // namespace dx12
