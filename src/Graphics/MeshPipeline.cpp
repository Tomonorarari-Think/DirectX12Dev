//=============================================================================
// MeshPipeline.cpp
//   MeshPipeline の実装。
//=============================================================================
#include "MeshPipeline.h"

#include "CommandQueue.h"
#include "DescriptorHeap.h"
#include "Geometry.h"

#include <cstddef>  // offsetof
#include <cstdio>   // printf（シェーダーのコンパイルエラーをコンソールに出す）
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
/// 環境光の強さ。光の当たらない面が真っ黒に潰れるのを防ぎます。
/// </summary>
/// <remarks>
/// 本来は周囲からの反射の積み重ねですが、ここでは定数で近似しています。
/// </remarks>
constexpr float kAmbientIntensity = 0.25f;

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
/// 生成するテクスチャの一辺のピクセル数。
/// </summary>
constexpr uint32_t kTextureSize = 256;

/// <summary>
/// 市松模様 1 マスのピクセル数。
/// </summary>
constexpr uint32_t kTextureCellSize = 32;
} // namespace


/// <summary>
/// ルートシグネチャ・PSO・定数バッファ・テクスチャを生成します。
/// </summary>
void MeshPipeline::Initialize(ID3D12Device* device,
                              DXGI_FORMAT renderTargetFormat,
                              DXGI_FORMAT depthStencilFormat,
                              uint32_t frameCount,
                              uint32_t maxObjectCount,
                              CommandQueue& commandQueue,
                              DescriptorHeap& descriptorHeap)
{
    m_maxObjectCount = maxObjectCount;

    CreateRootSignature(device);
    CreatePipelineState(device, renderTargetFormat, depthStencilFormat);

    // カメラとライトはフレームごとに 1 組あればよい。
    m_frameConstantBuffer.Initialize(device, sizeof(FrameConstants), frameCount);

    // 変換行列はオブジェクトごとに違うため、フレーム数 × オブジェクト数ぶん要る。
    //   GPU が前フレームを読んでいる最中に上書きしないためにフレーム別、
    //   1 フレームの中で描く順に上書きしないためにオブジェクト別。
    m_objectConstantBuffer.Initialize(
        device, sizeof(ObjectConstants), frameCount * maxObjectCount);

    // メッシュに貼るテクスチャ。画像ファイルは使わず、市松模様をコードで生成する。
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
    D3D12_ROOT_PARAMETER rootParameters[3] = {};

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

    // 静的サンプラー (Static Sampler)
    D3D12_STATIC_SAMPLER_DESC staticSampler = {};

    //   Filter : ピクセルとテクセルがぴったり一致しないときの読み方。
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

    //   AddressU/V/W : UV が 0〜1 の外に出たときの扱い。
    //   床は UV を 1 より大きくしているので、WRAP によって模様が繰り返される。
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
    };

    // オフセットは頂点構造体の並びと 1 バイトもずれてはいけない。
    // 手で足した数値が Vertex と食い違っていないことを、ここで機械的に確かめる。
    static_assert(sizeof(Vertex) == 48, "Vertex のサイズが入力レイアウトの前提と違います");
    static_assert(offsetof(Vertex, normal) == 12, "NORMAL のオフセットが違います");
    static_assert(offsetof(Vertex, color)  == 24, "COLOR のオフセットが違います");
    static_assert(offsetof(Vertex, uv)     == 40, "TEXCOORD のオフセットが違います");

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
/// このフレームの共通定数（カメラとライト）を書き込みます。
/// </summary>
void MeshPipeline::UpdateFrameConstants(uint32_t frameIndex,
                                        const DirectX::XMMATRIX& viewProjection,
                                        const DirectX::XMFLOAT3& cameraPosition)
{
    using namespace DirectX;

    FrameConstants constants = {};

    // HLSL は定数バッファの行列を列優先で読むため、転置してから書き込む。
    XMStoreFloat4x4(&constants.viewProjection, XMMatrixTranspose(viewProjection));

    // ライトの向きは長さ 1 でなければ内積が明るさにならない。
    const XMVECTOR lightDirection = XMVector3Normalize(
        XMVectorSet(kLightDirection[0], kLightDirection[1], kLightDirection[2], 0.0f));

    XMStoreFloat4(&constants.lightDirection, lightDirection);

    // w には環境光の強さを同居させている（16 バイトの空きを無駄にしないため）。
    constants.lightColor = { kLightColor[0], kLightColor[1], kLightColor[2],
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
void MeshPipeline::Bind(ID3D12GraphicsCommandList* commandList, uint32_t frameIndex) const
{
    // コマンドリストは Reset するたびに「PSO 未設定」の状態に戻ります。
    commandList->SetPipelineState(m_pipelineState.Get());
    commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    // ルートディスクリプタなので、ディスクリプタヒープを経由せず GPU アドレスを直接渡せる。
    commandList->SetGraphicsRootConstantBufferView(
        kFrameConstantsRootParameterIndex,
        m_frameConstantBuffer.GpuAddress(frameIndex));

    // ★ 前提として、呼び出し側が SetDescriptorHeaps() で
    //   シェーダー可視ヒープを設定しておく必要があります（Renderer が行う）。
    commandList->SetGraphicsRootDescriptorTable(
        kTextureRootParameterIndex,
        m_texture.ShaderResourceView());

    // TRIANGLELIST : 3 頂点ごとに独立した三角形を作る。
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
