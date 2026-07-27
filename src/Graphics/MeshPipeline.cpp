//=============================================================================
// MeshPipeline.cpp
//   MeshPipeline の実装。
//=============================================================================
#include "MeshPipeline.h"

#include "CommandQueue.h"
#include "DescriptorHeap.h"
#include "UploadHelper.h"

#include <cstdio>   // printf（シェーダーのコンパイルエラーをコンソールに出す）
#include <cstring>  // memcpy
#include <format>

namespace dx12
{
namespace
{
/// <summary>
/// 立方体の頂点データ（6 面 × 4 頂点 = 24 個）。
/// </summary>
/// <remarks>
/// 面ごとに UV を 0〜1 で貼りたいので、頂点は面ごとに分けて持ちます。8 個では
/// 足りません。各面は外から見て時計回りに並べており、逆にすると背面カリングで消えます。
/// </remarks>
constexpr Vertex kCubeVertices[] = {
    // 手前 (-Z) 青
    { { -0.5f,  0.5f, -0.5f }, { 0.45f, 0.68f, 1.00f, 1.0f }, { 0.0f, 0.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 0.45f, 0.68f, 1.00f, 1.0f }, { 1.0f, 0.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 0.20f, 0.40f, 0.90f, 1.0f }, { 1.0f, 1.0f } },
    { { -0.5f, -0.5f, -0.5f }, { 0.20f, 0.40f, 0.90f, 1.0f }, { 0.0f, 1.0f } },

    // 奥 (+Z) 緑
    { {  0.5f,  0.5f,  0.5f }, { 0.55f, 1.00f, 0.55f, 1.0f }, { 0.0f, 0.0f } },
    { { -0.5f,  0.5f,  0.5f }, { 0.55f, 1.00f, 0.55f, 1.0f }, { 1.0f, 0.0f } },
    { { -0.5f, -0.5f,  0.5f }, { 0.20f, 0.70f, 0.30f, 1.0f }, { 1.0f, 1.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 0.20f, 0.70f, 0.30f, 1.0f }, { 0.0f, 1.0f } },

    // 左 (-X) 赤
    { { -0.5f,  0.5f,  0.5f }, { 1.00f, 0.55f, 0.45f, 1.0f }, { 0.0f, 0.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 1.00f, 0.55f, 0.45f, 1.0f }, { 1.0f, 0.0f } },
    { { -0.5f, -0.5f, -0.5f }, { 0.85f, 0.25f, 0.20f, 1.0f }, { 1.0f, 1.0f } },
    { { -0.5f, -0.5f,  0.5f }, { 0.85f, 0.25f, 0.20f, 1.0f }, { 0.0f, 1.0f } },

    // 右 (+X) 黄
    { {  0.5f,  0.5f, -0.5f }, { 1.00f, 0.88f, 0.45f, 1.0f }, { 0.0f, 0.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 1.00f, 0.88f, 0.45f, 1.0f }, { 1.0f, 0.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 0.90f, 0.65f, 0.15f, 1.0f }, { 1.0f, 1.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 0.90f, 0.65f, 0.15f, 1.0f }, { 0.0f, 1.0f } },

    // 上 (+Y) 水色
    { { -0.5f,  0.5f,  0.5f }, { 0.60f, 0.95f, 1.00f, 1.0f }, { 0.0f, 0.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 0.60f, 0.95f, 1.00f, 1.0f }, { 1.0f, 0.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 0.30f, 0.80f, 0.95f, 1.0f }, { 1.0f, 1.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 0.30f, 0.80f, 0.95f, 1.0f }, { 0.0f, 1.0f } },

    // 下 (-Y) 紫
    { { -0.5f, -0.5f, -0.5f }, { 0.80f, 0.60f, 1.00f, 1.0f }, { 0.0f, 0.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 0.80f, 0.60f, 1.00f, 1.0f }, { 1.0f, 0.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 0.55f, 0.35f, 0.85f, 1.0f }, { 1.0f, 1.0f } },
    { { -0.5f, -0.5f,  0.5f }, { 0.55f, 0.35f, 0.85f, 1.0f }, { 0.0f, 1.0f } },
};

/// <summary>
/// 立方体のインデックスデータ（6 面 × 三角形 2 枚 × 3 頂点 = 36 個）。
/// </summary>
/// <remarks>
/// 四角形 1 枚は三角形 2 枚で作ります。同じ頂点を 2 つの三角形で共有できるため、
/// 頂点を並べ直すよりデータが小さくなります。
/// </remarks>
constexpr uint16_t kCubeIndices[] = {
     0,  1,  2,   0,  2,  3,   // 手前
     4,  5,  6,   4,  6,  7,   // 奥
     8,  9, 10,   8, 10, 11,   // 左
    12, 13, 14,  12, 14, 15,   // 右
    16, 17, 18,  16, 18, 19,   // 上
    20, 21, 22,  20, 22, 23,   // 下
};

/// <summary>
/// シェーダーファイルの場所（プロジェクトルートからの相対パス）。
/// </summary>
constexpr const wchar_t* kShaderRelativePath = L"shaders/Mesh.hlsl";

/// <summary>
/// 立方体が 1 回転するのにかかる秒数。
/// </summary>
constexpr float kSecondsPerRotation = 8.0f;

/// <summary>
/// 定数バッファを結び付けるルートパラメータの番号。
/// </summary>
constexpr uint32_t kSceneConstantsRootParameterIndex = 0;

/// <summary>
/// テクスチャ（SRV）を結び付けるルートパラメータの番号。
/// </summary>
constexpr uint32_t kTextureRootParameterIndex = 1;

/// <summary>
/// 生成するテクスチャの一辺のピクセル数。
/// </summary>
constexpr uint32_t kTextureSize = 256;

/// <summary>
/// 市松模様 1 マスのピクセル数。
/// </summary>
constexpr uint32_t kTextureCellSize = 32;
} // namespace


/// <summary>
/// ルートシグネチャ・PSO・頂点/インデックス/定数バッファを生成します。
/// </summary>
void MeshPipeline::Initialize(ID3D12Device* device,
                                  DXGI_FORMAT renderTargetFormat,
                                  DXGI_FORMAT depthStencilFormat,
                                  uint32_t frameCount,
                                  CommandQueue& commandQueue,
                                  DescriptorHeap& descriptorHeap)
{
    CreateRootSignature(device);
    CreatePipelineState(device, renderTargetFormat, depthStencilFormat);
    CreateGeometryBuffers(device, commandQueue);

    // 変換行列を毎フレーム渡すための定数バッファ。
    m_constantBuffer.Initialize(device, sizeof(SceneConstants), frameCount);

    // 立方体に貼るテクスチャ。画像ファイルは使わず、市松模様をコードで生成する。
    m_texture.Initialize(device,
                         commandQueue,
                         descriptorHeap,
                         kTextureSize,
                         kTextureSize,
                         CreateCheckerboardPixels(kTextureSize, kTextureSize, kTextureCellSize));

    Log(L"メッシュ描画パイプラインを構築しました。");
}


/// <summary>
/// ルートシグネチャを生成します。
/// </summary>
void MeshPipeline::CreateRootSignature(ID3D12Device* device)
{
    // ルートパラメータ : シェーダーへ何を渡すかの定義。関数の引数リストに相当する。
    D3D12_ROOT_PARAMETER rootParameters[2] = {};

    // 0 番 : シーン共通の定数バッファ（変換行列）
    rootParameters[kSceneConstantsRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_CBV;

    // ShaderRegister = 0 は HLSL 側の register(b0) に対応する。
    rootParameters[kSceneConstantsRootParameterIndex].Descriptor.ShaderRegister = 0;
    rootParameters[kSceneConstantsRootParameterIndex].Descriptor.RegisterSpace  = 0;

    // ShaderVisibility : どのシェーダー段から見えるようにするか。
    rootParameters[kSceneConstantsRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_VERTEX;

    // 1 番 : テクスチャ（ディスクリプタテーブル）
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

    // 静的サンプラー (Static Sampler)
    D3D12_STATIC_SAMPLER_DESC staticSampler = {};

    //   Filter : ピクセルとテクセルがぴったり一致しないときの読み方。
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

    //   AddressU/V/W : UV が 0〜1 の外に出たときの扱い。
    staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    staticSampler.MipLODBias       = 0.0f;
    staticSampler.MaxAnisotropy    = 0;
    staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    staticSampler.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    staticSampler.MinLOD           = 0.0f;
    staticSampler.MaxLOD           = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister   = 0;   // HLSL の register(s0) に対応
    staticSampler.RegisterSpace    = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters     = _countof(rootParameters);
    rootSignatureDesc.pParameters       = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers   = &staticSampler;

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
/// HLSL ファイルをコンパイルして、GPU 用のバイトコードを得ます。
/// </summary>
ComPtr<ID3DBlob> MeshPipeline::CompileShader(const std::wstring& filePath,
                                                 const char* entryPoint,
                                                 const char* target)
{
    UINT compileFlags = 0;

#if defined(_DEBUG)
    // DEBUG            : シェーダーデバッガで行単位のデバッグができる情報を埋め込む
    // SKIP_OPTIMIZATION: 最適化を行わない。変数が消えないためデバッグしやすい
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    // 最高レベルの最適化を行う
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    const HRESULT hr = ::D3DCompileFromFile(
        filePath.c_str(),
        nullptr,                               // マクロ定義（#define 相当）。今回は無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,     // #include を .hlsl と同じ階層から解決する
        entryPoint,                            // 入口となる関数名
        target,                                // シェーダーモデル
        compileFlags,
        0,                                     // エフェクト用フラグ（未使用）
        &shaderBlob,
        &errorBlob);

    if (FAILED(hr))
    {
        // シェーダーの文法エラーはここに出ます。
        LogError(L"シェーダーのコンパイルに失敗しました: " + filePath);

        if (errorBlob != nullptr)
        {
            const char* message = static_cast<const char*>(errorBlob->GetBufferPointer());
            ::OutputDebugStringA(message);
            ::OutputDebugStringA("\n");
            ::printf("%s\n", message); // コンソールにも出す
        }

        DX_CHECK(hr);
    }

    return shaderBlob;
}


/// <summary>
/// HLSL をコンパイルし、パイプラインステートオブジェクトを生成します。
/// </summary>
void MeshPipeline::CreatePipelineState(ID3D12Device* device,
                                           DXGI_FORMAT renderTargetFormat,
                                           DXGI_FORMAT depthStencilFormat)
{
    // (1) シェーダーのコンパイル
    //   "vs_5_0" の意味 : vs = Vertex Shader、5_0 = シェーダーモデル 5.0
    //   "ps_5_0" の意味 : ps = Pixel Shader
    const std::wstring shaderPath = ResolveAssetPath(kShaderRelativePath);
    Log(L"シェーダーを読み込みます: " + shaderPath);

    ComPtr<ID3DBlob> vertexShader = CompileShader(shaderPath, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> pixelShader  = CompileShader(shaderPath, "PSMain", "ps_5_0");

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
            "COLOR",                                     // シェーダー側の : COLOR に対応
            0,
            DXGI_FORMAT_R32G32B32A32_FLOAT,              // float4
            0,
            12,                                          // position の 12 バイト後ろ
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        },
        {
            "TEXCOORD",                                  // シェーダー側の : TEXCOORD に対応
            0,
            DXGI_FORMAT_R32G32_FLOAT,                    // float2
            0,
            28,                                          // position(12) + color(16) の後ろ
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        },
    };

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
    psoDesc.RTVFormats[0]    = renderTargetFormat;

    // 深度バッファの形式。DepthBuffer::kFormat と一致していないとエラーになります。
    psoDesc.DSVFormat = depthStencilFormat;

    // MSAA 無し
    psoDesc.SampleDesc.Count   = 1;
    psoDesc.SampleDesc.Quality = 0;

    psoDesc.NodeMask = 0;
    psoDesc.Flags    = D3D12_PIPELINE_STATE_FLAG_NONE;

    DX_CHECK(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
}


/// <summary>
/// 頂点バッファを作り、頂点データを書き込みます。
/// </summary>
void MeshPipeline::CreateGeometryBuffers(ID3D12Device* device, CommandQueue& commandQueue)
{
    const UINT vertexBufferSize = sizeof(kCubeVertices);
    const UINT indexBufferSize  = sizeof(kCubeIndices);

    // どちらも一度書いたら変わらないので、GPU 専用の DEFAULT ヒープに置く。
    m_vertexBuffer = upload::CreateBufferWithData(
        device, commandQueue, kCubeVertices, vertexBufferSize,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    m_indexBuffer = upload::CreateBufferWithData(
        device, commandQueue, kCubeIndices, indexBufferSize,
        D3D12_RESOURCE_STATE_INDEX_BUFFER);

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes  = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes    = vertexBufferSize;

    // インデックスは形式を指定する。頂点が 65536 個未満なら R16_UINT で足りる。
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format         = DXGI_FORMAT_R16_UINT;
    m_indexBufferView.SizeInBytes    = indexBufferSize;

    m_indexCount = _countof(kCubeIndices);

    Log(std::format(L"立方体を作成しました（頂点 {} 個 / インデックス {} 個, DEFAULT ヒープ）",
                    _countof(kCubeVertices), m_indexCount));
}


/// <summary>
/// このフレームの変換行列を計算し、定数バッファへ書き込みます。
/// </summary>
void MeshPipeline::Update(uint32_t frameIndex,
                          const DirectX::XMMATRIX& viewProjection,
                          float totalSeconds)
{
    using namespace DirectX;

    // ワールド行列 : 立方体そのものを回す。
    //   2 軸で回すと、面の前後関係が入れ替わる様子が分かりやすい。
    const float angle = totalSeconds * (XM_2PI / kSecondsPerRotation);
    const XMMATRIX world = XMMatrixRotationY(angle) * XMMatrixRotationX(angle * 0.45f);

    // ワールド × ビュー × 射影。行ベクトル規約なので「先に適用する変換を左」に書く。
    // アスペクト比の補正は射影行列が担うため、ここでは不要になった。
    const XMMATRIX worldViewProjection = world * viewProjection;

    // HLSL は定数バッファの行列を列優先で読むため、転置してから書き込む。
    SceneConstants constants = {};
    XMStoreFloat4x4(&constants.worldViewProjection, XMMatrixTranspose(worldViewProjection));

    m_constantBuffer.Update(frameIndex, &constants, sizeof(constants));
}


/// <summary>
/// コマンドリストに「三角形を描く」命令を記録します。
/// </summary>
void MeshPipeline::RecordDrawCommands(ID3D12GraphicsCommandList* commandList,
                                          uint32_t frameIndex) const
{
    // (0) 使用するパイプラインステート（PSO）を設定する
    //   コマンドリストは Reset するたびに「PSO 未設定」の状態に戻ります。
    commandList->SetPipelineState(m_pipelineState.Get());

    // (1) 使用するルートシグネチャを設定する。
    commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    // (1-b) 定数バッファをルートパラメータ 0 番に結び付ける
    //   ルートディスクリプタなので、ディスクリプタヒープを経由せず
    //   GPU アドレスを直接渡せます。
    commandList->SetGraphicsRootConstantBufferView(
        kSceneConstantsRootParameterIndex,
        m_constantBuffer.GpuAddress(frameIndex));

    // (1-c) テクスチャをルートパラメータ 1 番に結び付ける
    //   ★ 前提として、呼び出し側が SetDescriptorHeaps() で
    //     シェーダー可視ヒープを設定しておく必要があります（Renderer が行う）。
    commandList->SetGraphicsRootDescriptorTable(
        kTextureRootParameterIndex,
        m_texture.ShaderResourceView());

    // (2) プリミティブトポロジ（頂点の結び方）を設定する
    //   TRIANGLELIST : 3 頂点ごとに独立した三角形を作る（頂点 6 個 → 三角形 2 枚）
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // (3) 使用する頂点バッファを 0 番スロットに設定する
    //     IA は Input Assembler（入力アセンブラ）の略で、
    //     頂点データを組み立ててシェーダーに送り込む GPU の最初の段のこと。
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

    // インデックスバッファは 1 本だけ設定する（頂点バッファのような複数スロットは無い）
    commandList->IASetIndexBuffer(&m_indexBufferView);

    // (4) 描画命令
    //   インデックスの順に頂点を引いて描く。同じ頂点を複数の三角形で共有できる。
    commandList->DrawIndexedInstanced(
        m_indexCount,   // 描くインデックスの個数
        1,              // インスタンス数
        0,              // 何番目のインデックスから始めるか
        0,              // 各インデックスに足すオフセット
        0);             // 何番目のインスタンスから始めるか
}

} // namespace dx12
