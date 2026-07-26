//=============================================================================
// TrianglePipeline.cpp
//   TrianglePipeline の実装。
//=============================================================================
#include "TrianglePipeline.h"

#include "CommandQueue.h"
#include "DescriptorHeap.h"

#include <cstdio>   // printf（シェーダーのコンパイルエラーをコンソールに出す）
#include <cstring>  // memcpy
#include <format>

namespace dx12
{
namespace
{
/// @brief 描画する三角形の頂点データ（3 枚ぶん = 9 頂点）。
///
/// NDC（正規化デバイス座標）で位置を指定します（x は左 -1.0〜右 +1.0、y は下 -1.0〜上 +1.0、
/// z は手前 0.0〜奥 1.0）。
///
/// **深度テストを実証するための配置**
///
/// 3 枚を少しずつずらして重ね、それぞれ別の奥行き (z) に置いています。
///
/// ```
///        奥 (z=0.75) 赤        中 (z=0.50) 緑        手前 (z=0.25) 青
///              ▲                    ▲                     ▲
///             ╱ ╲                  ╱ ╲                   ╱ ╲
///            ╱   ╲ ── 重なる ──   ╱   ╲  ── 重なる ──   ╱   ╲
///           ╱     ╲              ╱     ╲               ╱     ╲
///          ▔▔▔▔▔▔               ▔▔▔▔▔▔                ▔▔▔▔▔▔
/// ```
///
/// **★ わざと「手前 → 奥」の順に並べています**
///
/// 配列の先頭が手前 (z=0.25)、末尾が奥 (z=0.75) です。GPU は配列順に描くため、
/// **奥の三角形を最後に描く**ことになります。
///
/// 深度テストが無ければ、あとから描いた奥の三角形が手前を上書きしてしまい、
/// 前後関係が逆に見えます。深度テストを有効にすると、奥のピクセルは
/// 「すでに手前のものが描かれている」と判定されて捨てられ、正しく見えます。
///
/// つまりこの並び順は、深度テストが効いているかどうかを一目で確認するための仕掛けです。
///
/// **ワインディング順（頂点を並べる向き）**
///
/// DirectX の既定では「時計回り (Clockwise) に見える面が表」です。
/// 各三角形は 上 → 右下 → 左下 の順で、画面上で時計回りになるため表向きになります。
/// 逆順にすると裏面と判定され、背面カリング（`D3D12_CULL_MODE_BACK`）によって
/// 何も表示されなくなります。「三角形が出ない」ときの定番の原因です。
constexpr Vertex kTriangleVertices[] = {
    // --- 1 枚目 : 手前 (z = 0.25) 青系。右上寄りに配置 -------------------------
    // 位置 { x, y, z }              色 { r, g, b, a }                UV { u, v }
    { {  0.25f,  0.45f, 0.25f }, { 0.35f, 0.65f, 1.00f, 1.0f }, { 0.5f, 0.0f } }, // 上
    { {  0.60f, -0.18f, 0.25f }, { 0.10f, 0.25f, 0.90f, 1.0f }, { 1.0f, 1.0f } }, // 右下
    { { -0.10f, -0.18f, 0.25f }, { 0.55f, 0.85f, 1.00f, 1.0f }, { 0.0f, 1.0f } }, // 左下

    // --- 2 枚目 : 中間 (z = 0.50) 緑系。下寄りに配置 ---------------------------
    { {  0.00f,  0.20f, 0.50f }, { 0.40f, 1.00f, 0.40f, 1.0f }, { 0.5f, 0.0f } }, // 上
    { {  0.35f, -0.43f, 0.50f }, { 0.10f, 0.70f, 0.20f, 1.0f }, { 1.0f, 1.0f } }, // 右下
    { { -0.35f, -0.43f, 0.50f }, { 0.70f, 1.00f, 0.30f, 1.0f }, { 0.0f, 1.0f } }, // 左下

    // --- 3 枚目 : 奥 (z = 0.75) 赤系。左上寄りに配置 ---------------------------
    { { -0.25f,  0.45f, 0.75f }, { 1.00f, 0.45f, 0.35f, 1.0f }, { 0.5f, 0.0f } }, // 上
    { {  0.10f, -0.18f, 0.75f }, { 0.90f, 0.15f, 0.15f, 1.0f }, { 1.0f, 1.0f } }, // 右下
    { { -0.60f, -0.18f, 0.75f }, { 1.00f, 0.70f, 0.30f, 1.0f }, { 0.0f, 1.0f } }, // 左下
};

/// @brief シェーダーファイルの場所（プロジェクトルートからの相対パス）。
constexpr const wchar_t* kShaderRelativePath = L"shaders/Triangle.hlsl";

/// @brief 三角形が 1 回転するのにかかる秒数。
constexpr float kSecondsPerRotation = 4.0f;

/// @brief 定数バッファを結び付けるルートパラメータの番号。
///
/// ルートシグネチャに登録した順番（0 始まり）です。`SetGraphicsRootConstantBufferView` の第 1 引数
/// に渡す値であり、HLSL の `register(b0)` の番号とは別物である点に注意してください。
constexpr uint32_t kSceneConstantsRootParameterIndex = 0;

/// @brief テクスチャ（SRV）を結び付けるルートパラメータの番号。
constexpr uint32_t kTextureRootParameterIndex = 1;

/// @brief 生成するテクスチャの一辺のピクセル数。
constexpr uint32_t kTextureSize = 256;

/// @brief 市松模様 1 マスのピクセル数。
constexpr uint32_t kTextureCellSize = 32;
} // namespace


/// @brief ルートシグネチャ・PSO・頂点バッファ・定数バッファを生成します。
void TrianglePipeline::Initialize(ID3D12Device* device,
                                  DXGI_FORMAT renderTargetFormat,
                                  DXGI_FORMAT depthStencilFormat,
                                  uint32_t frameCount,
                                  CommandQueue& commandQueue,
                                  DescriptorHeap& descriptorHeap)
{
    CreateRootSignature(device);
    CreatePipelineState(device, renderTargetFormat, depthStencilFormat);
    CreateVertexBuffer(device);

    // 変換行列を毎フレーム渡すための定数バッファ。
    // フレーム数ぶん確保することで、GPU が読んでいる領域を CPU が
    // 書き換えてしまう事故を防ぐ（詳細は ConstantBuffer のコメント参照）。
    m_constantBuffer.Initialize(device, sizeof(SceneConstants), frameCount);

    // 三角形に貼るテクスチャ。画像ファイルは使わず、市松模様をコードで生成する。
    // テクスチャは DEFAULT ヒープに置くため、内部で GPU 転送と完了待ちを行う。
    m_texture.Initialize(device,
                         commandQueue,
                         descriptorHeap,
                         kTextureSize,
                         kTextureSize,
                         CreateCheckerboardPixels(kTextureSize, kTextureSize, kTextureCellSize));

    Log(L"三角形描画パイプラインを構築しました。");
}


/// @brief ルートシグネチャを生成します。
void TrianglePipeline::CreateRootSignature(ID3D12Device* device)
{
    //-------------------------------------------------------------------------
    // ルートパラメータ : シェーダーへ何を渡すかの定義。関数の引数リストに相当する。
    //
    //   ■ 渡し方は 3 種類あり、用途で使い分けます
    //
    //     (a) ルート定数 (ROOT_32BIT_CONSTANTS)
    //           数値を数個だけルートシグネチャに直接埋め込む。最速だが容量が極小。
    //           行列 1 個（16 個の float）でも枠を大きく消費するため、多用は不可。
    //
    //     (b) ルートディスクリプタ (CBV / SRV / UAV)  ← 今回採用
    //           GPU アドレスを直接渡す。ディスクリプタヒープが不要で手軽。
    //           バッファ 1 本を丸ごと渡す用途に向く。
    //
    //     (c) ディスクリプタテーブル
    //           ディスクリプタヒープ上の範囲を渡す。テクスチャを何十枚も
    //           まとめて渡すときに必須。最も柔軟だがヒープ管理が要る。
    //
    //   ■ なぜ今回は (b) なのか
    //     渡すのは行列 1 個だけで、ディスクリプタヒープを導入すると
    //     学ぶことが一気に増えるためです。テクスチャを扱う段階になったら
    //     (c) を追加します。
    //
    //   ■ ルートシグネチャは小さいほど速い
    //     ルートシグネチャの中身は GPU の高速な専用領域に置かれます。
    //     大きくすると溢れてメモリ経由になり遅くなるため、
    //     「本当に必要なものだけ」を並べるのが原則です。
    //-------------------------------------------------------------------------
    D3D12_ROOT_PARAMETER rootParameters[2] = {};

    // 0 番 : シーン共通の定数バッファ（変換行列）
    rootParameters[kSceneConstantsRootParameterIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_CBV;

    // ShaderRegister = 0 は HLSL 側の register(b0) に対応する。
    // ここがずれるとシェーダーに値が届かない（エラーにはならず絵が壊れる）。
    rootParameters[kSceneConstantsRootParameterIndex].Descriptor.ShaderRegister = 0;
    rootParameters[kSceneConstantsRootParameterIndex].Descriptor.RegisterSpace  = 0;

    // ShaderVisibility : どのシェーダー段から見えるようにするか。
    //   今回、行列を使うのは頂点シェーダーだけなので VERTEX に限定する。
    //   ALL より狭くするほど GPU が最適化しやすくなる。
    rootParameters[kSceneConstantsRootParameterIndex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_VERTEX;

    //-------------------------------------------------------------------------
    // 1 番 : テクスチャ（ディスクリプタテーブル）
    //
    //   ★ ここが Step 6 の定数バッファとの違いです。
    //     定数バッファは「ルートディスクリプタ」で GPU アドレスを直接渡せました。
    //     しかしテクスチャ (SRV) はルートディスクリプタでは渡せません。
    //     ディスクリプタヒープ上の範囲を指す「ディスクリプタテーブル」を使います。
    //
    //   ディスクリプタレンジ = 「ヒープのどこから何個ぶんを、何番のレジスタに割り当てるか」
    //   ここでは「SRV を 1 個、t0 に」という指定です。
    //   テクスチャを 10 枚まとめて渡したければ NumDescriptors を 10 にするだけで、
    //   ルートシグネチャのサイズは変わりません。これがテーブルの利点です。
    //-------------------------------------------------------------------------
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

    //-------------------------------------------------------------------------
    // 静的サンプラー (Static Sampler)
    //
    //   サンプラーは「テクスチャをどう読むか」の設定です。
    //   本来はディスクリプタヒープに置きますが、実行中に変わらないものは
    //   ルートシグネチャに直接埋め込めます。これが静的サンプラーです。
    //   サンプラー用のヒープを作らずに済むため、固定の設定ならこちらが有利です。
    //-------------------------------------------------------------------------
    D3D12_STATIC_SAMPLER_DESC staticSampler = {};

    //   Filter : ピクセルとテクセルがぴったり一致しないときの読み方。
    //     MIN_MAG_MIP_LINEAR … 周囲 4 テクセルを混ぜる。なめらかになる（本実装）
    //     MIN_MAG_MIP_POINT  … 最も近い 1 テクセルをそのまま使う。くっきりする
    //     ドット絵を拡大表示したいときは POINT が正解です。
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

    //   AddressU/V/W : UV が 0〜1 の外に出たときの扱い。
    //     WRAP   … 繰り返す（タイル状に敷き詰める）
    //     CLAMP  … 端の色を引き伸ばす
    //     MIRROR … 折り返す
    //   今回の UV は 0〜1 に収まっているのでどれでも同じ見た目になりますが、
    //   模様を敷き詰めたくなったときのために WRAP にしています。
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

    //-------------------------------------------------------------------------
    // ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT フラグ
    //
    //   「頂点バッファから入力レイアウト経由で頂点を読み込む」ことを許可します。
    //   これを付け忘れると、PSO 生成時に
    //   「入力レイアウトが指定されているのに許可されていない」という
    //   エラーになります。頂点バッファを使う描画では必須のフラグです。
    //
    //   ちなみに「頂点バッファを使わない描画」も存在します（SV_VertexID から
    //   シェーダー内で座標を計算する手法）。その場合はこのフラグは不要です。
    //-------------------------------------------------------------------------
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    //-------------------------------------------------------------------------
    // ルートシグネチャは 2 段階で作ります。
    //   (1) D3D12SerializeRootSignature で「バイナリ形式」に変換（シリアライズ）
    //   (2) CreateRootSignature でそのバイナリからオブジェクトを生成
    //
    //   ID3DBlob は「サイズ付きの生メモリの塊」を表す COM オブジェクトです。
    //   （Binary Large OBject の略。シェーダーのバイトコード等にも使われます）
    //-------------------------------------------------------------------------
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
        // これを読まずに HRESULT だけ見ると原因が全く分からないため、必ず出力する。
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


/// @brief HLSL ファイルをコンパイルして、GPU 用のバイトコードを得ます。
ComPtr<ID3DBlob> TrianglePipeline::CompileShader(const std::wstring& filePath,
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
        //---------------------------------------------------------------------
        // シェーダーの文法エラーはここに出ます。
        // errorBlob の中身には行番号付きの詳しいメッセージが入っているため、
        // これを表示できるかどうかがデバッグ効率を大きく左右します。
        //---------------------------------------------------------------------
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


/// @brief HLSL をコンパイルし、パイプラインステートオブジェクトを生成します。
void TrianglePipeline::CreatePipelineState(ID3D12Device* device,
                                           DXGI_FORMAT renderTargetFormat,
                                           DXGI_FORMAT depthStencilFormat)
{
    //-------------------------------------------------------------------------
    // (1) シェーダーのコンパイル
    //   "vs_5_0" の意味 : vs = Vertex Shader、5_0 = シェーダーモデル 5.0
    //   "ps_5_0" の意味 : ps = Pixel Shader
    //-------------------------------------------------------------------------
    const std::wstring shaderPath = ResolveAssetPath(kShaderRelativePath);
    Log(L"シェーダーを読み込みます: " + shaderPath);

    ComPtr<ID3DBlob> vertexShader = CompileShader(shaderPath, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> pixelShader  = CompileShader(shaderPath, "PSMain", "ps_5_0");

    //-------------------------------------------------------------------------
    // (2) 入力レイアウト
    //
    //   「頂点バッファのバイト列を、どう切り分けてシェーダーに渡すか」の定義。
    //   Vertex 構造体（TrianglePipeline.h）と対応させます。
    //
    //   Vertex のメモリ配置:
    //     オフセット  0 〜 11 : position[3]  (float 3 個 = 12 バイト)
    //     オフセット 12 〜 27 : color[4]     (float 4 個 = 16 バイト)
    //     オフセット 28 〜 35 : uv[2]        (float 2 個 =  8 バイト)
    //     合計 36 バイト
    //
    //   要素を増やすときは、この 3 か所を必ず同時に直すこと:
    //     C++ の Vertex 構造体 / ここの入力レイアウト / HLSL の VSInput
    //
    //   各フィールドの意味:
    //     SemanticName         シェーダー側のセマンティクス名と一致させる
    //     SemanticIndex        同名セマンティクスを複数使うときの番号（例: TEXCOORD0/1）
    //     Format               この要素の型。R32G32B32_FLOAT = float 3 個
    //     InputSlot            何番の頂点バッファから読むか（複数バッファを使う場合に指定）
    //     AlignedByteOffset    バッファ先頭からのバイト位置
    //     InputSlotClass       頂点ごとか、インスタンスごとか
    //-------------------------------------------------------------------------
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

    //-------------------------------------------------------------------------
    // (3) ラスタライザステート
    //   頂点を「ピクセルの集合」に変換する段の設定。
    //-------------------------------------------------------------------------
    D3D12_RASTERIZER_DESC rasterizerDesc = {};

    // SOLID = 面を塗りつぶす。WIREFRAME にすると輪郭線だけになる（デバッグに便利）
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    //-------------------------------------------------------------------------
    // CullMode : 見えない面を描画前に捨てる設定（カリング）。
    //   BACK  = 裏を向いている面を捨てる（既定。処理量が約半分になる）
    //   NONE  = 捨てない。表裏どちらでも描く
    //   FRONT = 表を向いている面を捨てる
    //
    //   ★ 学習中のヒント
    //     三角形が表示されないとき、ここを一時的に NONE にしてみてください。
    //     それで表示されるなら、原因は「頂点の並び順（ワインディング）」です。
    //-------------------------------------------------------------------------
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

    //-------------------------------------------------------------------------
    // (4) ブレンドステート
    //   「これから描く色」と「すでに描かれている色」をどう混ぜるかの設定。
    //   半透明表現に使いますが、今回は不透明なのでブレンド無効（上書き）。
    //-------------------------------------------------------------------------
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

    //-------------------------------------------------------------------------
    // (5) 深度ステンシルステート
    //   奥行き判定（手前のものが奥のものを隠す処理）の設定。
    //-------------------------------------------------------------------------
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};

    // DepthEnable : 深度テストを行うか。
    //   FALSE にすると「あとから描いたものが必ず手前」になります。
    //   ★ 学習のヒント: ここを一時的に FALSE にして実行すると、
    //     奥の赤い三角形が手前の青を上書きして前後関係が壊れます。
    //     深度テストが何をしているかを体感できるので一度試してみてください。
    depthStencilDesc.DepthEnable = TRUE;

    //-------------------------------------------------------------------------
    // DepthWriteMask : 深度テストに合格したとき、深度バッファを更新するか。
    //   ALL  … 更新する（通常の不透明物体）
    //   ZERO … 更新しない。「奥行きは見るが記録しない」
    //          半透明物体を描くときに使います。半透明は後ろが透けて見えるため、
    //          深度を書き込むと後から描く物体が消えてしまうからです。
    //-------------------------------------------------------------------------
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

    //-------------------------------------------------------------------------
    // DepthFunc : 「合格」の条件。新しい深度値 op 記録済みの深度値。
    //   LESS          … 新しい方が小さい（＝手前）なら合格。最も一般的（本実装）
    //   LESS_EQUAL    … 同じ深度も合格。同じ位置に重ね描きするときに使う
    //   GREATER       … 逆順の深度（Reversed-Z）を使う高度な手法向け
    //   ALWAYS        … 常に合格。実質的に深度テスト無効
    //
    //   深度は「手前が 0.0、奥が 1.0」なので、
    //   「小さい方が手前」＝ LESS で手前が勝つ、という関係になります。
    //-------------------------------------------------------------------------
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    // ステンシル（型抜きや輪郭描画に使う付加機能）は今回使いません。
    depthStencilDesc.StencilEnable = FALSE;

    //-------------------------------------------------------------------------
    // (6) PSO の組み立て
    //-------------------------------------------------------------------------
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
    //              MSAA を使わない場合は「全ビット 1」にするのが決まりです。
    //              ここを 0 にすると何も描画されません（有名な落とし穴）。
    psoDesc.SampleMask = UINT_MAX;

    // 描画するプリミティブの種類。POINT / LINE / TRIANGLE / PATCH から選ぶ。
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // 出力先（レンダーターゲット）の枚数と形式。
    // ここの形式がバックバッファの形式と食い違うと PSO 生成が失敗します。
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


/// @brief 頂点バッファを作り、頂点データを書き込みます。
void TrianglePipeline::CreateVertexBuffer(ID3D12Device* device)
{
    const UINT vertexBufferSize = sizeof(kTriangleVertices);

    //-------------------------------------------------------------------------
    // ヒープの種類 (D3D12_HEAP_TYPE) — DirectX 12 のメモリ管理の要
    //
    //   DEFAULT
    //     GPU 専用の高速メモリ。GPU からの読み書きが最速。
    //     ただし CPU から直接書き込めないため、UPLOAD 経由でコピーする必要がある。
    //     毎フレーム変わらないデータ（モデルの頂点、テクスチャ）はこちら。
    //
    //   UPLOAD（今回採用）
    //     CPU から書き込め、GPU から読める共有メモリ。
    //     ただし GPU からのアクセスは DEFAULT より遅い。
    //     毎フレーム更新するデータ（変換行列などの定数バッファ）に向く。
    //
    //   READBACK
    //     GPU が書いた結果を CPU で読み戻すためのメモリ。
    //
    //   ■ 今回 UPLOAD を選んだ理由
    //     本来、動かない三角形の頂点は DEFAULT に置くのが定石です。
    //     しかし DEFAULT に置くには
    //       「UPLOAD バッファを作る → コピー命令を記録 → 実行 → 完了を待つ」
    //     という手順が増え、初回の学習には情報量が多すぎます。
    //     まずは UPLOAD で「動く」ことを確認し、
    //     DEFAULT への転送は次のステップで学ぶ、という構成にしています。
    //-------------------------------------------------------------------------
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                 = D3D12_HEAP_TYPE_UPLOAD;
    heapProperties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask     = 1;
    heapProperties.VisibleNodeMask      = 1;

    //-------------------------------------------------------------------------
    // リソースの形状の指定
    //   テクスチャ（2D 画像）ではなく、ただのバイト列（バッファ）を作ります。
    //   バッファの場合は以下が決まりごとです:
    //     Height = 1, DepthOrArraySize = 1, MipLevels = 1
    //     Format = UNKNOWN（バイト列に「ピクセル形式」は無いため）
    //     Layout = ROW_MAJOR
    //-------------------------------------------------------------------------
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment          = 0;                // 0 = 既定のアライメントに任せる
    resourceDesc.Width              = vertexBufferSize; // バッファでは Width がバイト数
    resourceDesc.Height             = 1;
    resourceDesc.DepthOrArraySize   = 1;
    resourceDesc.MipLevels          = 1;
    resourceDesc.Format             = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count   = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    //-------------------------------------------------------------------------
    // リソースの生成
    //
    //   CreateCommittedResource は「メモリの確保」と「リソースの作成」を
    //   まとめて行う手軽な API です。
    //   （上級者向けに、大きなヒープを一括確保してその中に複数リソースを
    //     配置する CreatePlacedResource もありますが、断片化対策の話なので後回しで OK）
    //
    //   初期状態 D3D12_RESOURCE_STATE_GENERIC_READ について:
    //     UPLOAD ヒープのリソースは、この状態で作ることが仕様で決められています。
    //     そして状態を変更することもできません。
    //-------------------------------------------------------------------------
    DX_CHECK(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,                               // クリア値（テクスチャ用。バッファでは不要）
        IID_PPV_ARGS(&m_vertexBuffer)));

    //-------------------------------------------------------------------------
    // CPU から書き込む（Map → memcpy → Unmap）
    //
    //   Map は「GPU 側のメモリを CPU のアドレス空間に見えるようにする」操作です。
    //   返ってきたポインタに memcpy すれば、そのまま GPU から見える状態になります。
    //-------------------------------------------------------------------------
    void* mappedData = nullptr;

    // 第 2 引数の D3D12_RANGE は「CPU が読む範囲」の指定。
    // Begin = End = 0 は「CPU からは一切読まない（書くだけ）」という意思表示で、
    // ドライバがキャッシュの同期処理を省略できる最適化ヒントになります。
    D3D12_RANGE readRange = { 0, 0 };

    DX_CHECK(m_vertexBuffer->Map(0, &readRange, &mappedData));
    std::memcpy(mappedData, kTriangleVertices, vertexBufferSize);

    // 第 2 引数 nullptr は「書き込んだ範囲は全体」という意味
    m_vertexBuffer->Unmap(0, nullptr);

    //-------------------------------------------------------------------------
    // 頂点バッファビューの設定
    //   GetGPUVirtualAddress() は「GPU から見たアドレス」を返します。
    //   CPU のポインタとは別物である点に注意してください。
    //-------------------------------------------------------------------------
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes  = sizeof(Vertex);   // 1 頂点あたりのバイト数
    m_vertexBufferView.SizeInBytes    = vertexBufferSize; // バッファ全体のバイト数

    Log(std::format(L"頂点バッファを作成しました（{} 頂点 / {} バイト）",
                    _countof(kTriangleVertices), vertexBufferSize));
}


/// @brief このフレームの変換行列を計算し、定数バッファへ書き込みます。
void TrianglePipeline::Update(uint32_t frameIndex, float aspectRatio, float totalSeconds)
{
    using namespace DirectX;

    //-------------------------------------------------------------------------
    // (1) ワールド行列 : 物体そのものを動かす変換
    //
    //   ここでは Z 軸まわりの回転だけを行います。
    //   Z 軸は「画面の奥から手前へ向かう軸」なので、
    //   Z 軸まわりの回転 ＝ 画面内でくるくる回る動きになります。
    //
    //   角度はラジアン（1 周 = 2π）で指定します。度ではありません。
    //   経過時間に比例させることで、フレームレートに依存しない
    //   一定速度の回転になります（fps が変わっても見た目の速さは同じ）。
    //-------------------------------------------------------------------------
    const float rotationAngle = totalSeconds * (XM_2PI / kSecondsPerRotation);

    const XMMATRIX world = XMMatrixRotationZ(rotationAngle);

    //-------------------------------------------------------------------------
    // (2) アスペクト比の補正
    //
    //   NDC（正規化デバイス座標）は、縦横どちらも -1〜+1 の「正方形」です。
    //   しかし実際のウィンドウは 1280x720 のような横長です。
    //   この正方形が横長に引き伸ばされて表示されるため、
    //   何も補正しないと回転中の三角形が歪んで見えます。
    //
    //   そこで X 方向を 1/アスペクト比 だけ縮めておき、
    //   引き伸ばされた結果が正しい形になるようにします。
    //
    //     ウィンドウ 1280x720 → aspectRatio = 1.777...
    //     → X を 0.5625 倍しておく → 表示時に 1.777 倍されて元通り
    //
    //   本来ここには「ビュー行列（カメラの位置と向き）」と
    //   「プロジェクション行列（透視投影）」が入ります。
    //   3D を扱う段階になったら、この行を差し替えることになります。
    //-------------------------------------------------------------------------
    const XMMATRIX aspectCorrection = XMMatrixScaling(1.0f / aspectRatio, 1.0f, 1.0f);

    //-------------------------------------------------------------------------
    // (3) 行列を 1 個にまとめる
    //
    //   行列の掛け算は順序が意味を持ちます（交換法則が成り立たない）。
    //   DirectXMath は「行ベクトル規約」なので、
    //   頂点は v × world × aspectCorrection の順に変換されます。
    //   つまり「先に適用したい変換を左に書く」ことになります。
    //
    //   まとめておけば、シェーダー側は行列 1 個を掛けるだけで済みます。
    //   頂点が何万個あっても掛け算は 1 回で済むため、これが定石です。
    //-------------------------------------------------------------------------
    const XMMATRIX worldViewProjection = world * aspectCorrection;

    //-------------------------------------------------------------------------
    // (4) 転置してから定数バッファへ書き込む
    //
    //   ★ 初学者が必ず一度は嵌まる箇所です。
    //
    //   DirectXMath は行列を「行優先 (row-major)」でメモリに並べます。
    //   一方 HLSL は、定数バッファ内の行列を既定で「列優先 (column-major)」
    //   として読み取ります。そのまま渡すと転置された行列と解釈され、
    //   三角形が意図しない方向に飛んだり潰れたりします。
    //
    //   あらかじめ CPU 側で転置しておけば、
    //   「行優先で並べた M の転置」＝「列優先で並べた M」となり辻褄が合います。
    //
    //   （別解として HLSL 側に row_major と書く方法もありますが、
    //     CPU で 1 回転置する方が GPU の負担が軽く、一般的です）
    //-------------------------------------------------------------------------
    SceneConstants constants = {};
    XMStoreFloat4x4(&constants.worldViewProjection, XMMatrixTranspose(worldViewProjection));

    m_constantBuffer.Update(frameIndex, &constants, sizeof(constants));
}


/// @brief コマンドリストに「三角形を描く」命令を記録します。
void TrianglePipeline::RecordDrawCommands(ID3D12GraphicsCommandList* commandList,
                                          uint32_t frameIndex) const
{
    //-------------------------------------------------------------------------
    // (0) 使用するパイプラインステート（PSO）を設定する
    //
    //   コマンドリストは Reset するたびに「PSO 未設定」の状態に戻ります。
    //   Reset の第 2 引数で渡す方法もありますが、本プロジェクトでは
    //   「PSO を知っているのは TrianglePipeline だけ」という責務分離のため、
    //   ここで設定しています。
    //-------------------------------------------------------------------------
    commandList->SetPipelineState(m_pipelineState.Get());

    // (1) 使用するルートシグネチャを設定する。
    //     PSO にも設定済みだが、コマンドリスト側にも明示的に設定する必要がある
    //     （PSO とルートシグネチャは別々に管理されているため。省略するとエラー）
    commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    //-------------------------------------------------------------------------
    // (1-b) 定数バッファをルートパラメータ 0 番に結び付ける
    //
    //   ルートディスクリプタなので、ディスクリプタヒープを経由せず
    //   GPU アドレスを直接渡せます。
    //
    //   ★ frameIndex ぶんずらしたアドレスを渡すのが重要です。
    //     GPU がまだ前のフレームの領域を読んでいる可能性があるため、
    //     フレームごとに別の領域を指す必要があります。
    //
    //   なお、この設定もコマンドリストを Reset するたびに消えるため、
    //   毎フレーム呼び直す必要があります。
    //-------------------------------------------------------------------------
    commandList->SetGraphicsRootConstantBufferView(
        kSceneConstantsRootParameterIndex,
        m_constantBuffer.GpuAddress(frameIndex));

    //-------------------------------------------------------------------------
    // (1-c) テクスチャをルートパラメータ 1 番に結び付ける
    //
    //   ★ 前提として、呼び出し側が SetDescriptorHeaps() で
    //     シェーダー可視ヒープを設定しておく必要があります（Renderer が行う）。
    //     設定を忘れると、このハンドルはどのヒープの何番目か解決できず、
    //     デバッグレイヤーがエラーを出します。
    //
    //   渡すのは GPU ハンドルです。CPU ハンドルを渡すとまったく別の場所を指します。
    //-------------------------------------------------------------------------
    commandList->SetGraphicsRootDescriptorTable(
        kTextureRootParameterIndex,
        m_texture.ShaderResourceView());

    //-------------------------------------------------------------------------
    // (2) プリミティブトポロジ（頂点の結び方）を設定する
    //
    //   TRIANGLELIST : 3 頂点ごとに独立した三角形を作る（頂点 6 個 → 三角形 2 枚）
    //   TRIANGLESTRIP: 1 頂点追加するごとに三角形が 1 枚増える（頂点 4 個 → 2 枚）
    //
    //   PSO で指定した PrimitiveTopologyType が「大分類（三角形かどうか）」、
    //   こちらが「具体的な結び方」を決めます。2 段階に分かれているのは、
    //   LIST と STRIP の切り替えでは PSO を作り直さなくて済むようにするためです。
    //-------------------------------------------------------------------------
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // (3) 使用する頂点バッファを 0 番スロットに設定する
    //     IA は Input Assembler（入力アセンブラ）の略で、
    //     頂点データを組み立ててシェーダーに送り込む GPU の最初の段のこと。
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

    //-------------------------------------------------------------------------
    // (4) 描画命令
    //   DrawInstanced(頂点数, インスタンス数, 開始頂点位置, 開始インスタンス位置)
    //
    //   「Instanced」と付いていますが、インスタンス数 1 なら普通の描画です。
    //   DirectX 12 には非インスタンス版の Draw が存在せず、
    //   常にこの形を使います（インスタンス描画が特別なものではなくなったため）。
    //-------------------------------------------------------------------------
    //   頂点 9 個 = 三角形 3 枚ぶんを 1 回の呼び出しでまとめて描きます。
    //   TRIANGLELIST なので、3 頂点ずつ独立した三角形として解釈されます。
    commandList->DrawInstanced(
        _countof(kTriangleVertices), // 頂点数 = 9（三角形 3 枚）
        1,                           // インスタンス数 = 1
        0,                           // 何番目の頂点から描き始めるか
        0);                          // 何番目のインスタンスから描き始めるか
}

} // namespace dx12
