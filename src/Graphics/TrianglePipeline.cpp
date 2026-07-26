//=============================================================================
// TrianglePipeline.cpp
//   TrianglePipeline の実装。
//=============================================================================
#include "TrianglePipeline.h"

#include <cstdio>   // printf（シェーダーのコンパイルエラーをコンソールに出す）
#include <cstring>  // memcpy
#include <format>

namespace dx12
{
namespace
{
/// <summary>描画する三角形の頂点データ。</summary>
/// <remarks>
/// <para>
/// NDC（正規化デバイス座標）で位置を指定します（x は左 -1.0〜右 +1.0、
/// y は下 -1.0〜上 +1.0）。
/// <code>
///        (0.0, 0.5) 赤
///              ▲
///             ╱ ╲
///            ╱   ╲
///           ╱     ╲
///   (-0.5,-0.5)  (0.5,-0.5)
///       青          緑
/// </code>
/// </para>
/// <para>
/// 頂点を並べる順番（ワインディング順）が重要です。
/// DirectX の既定では「時計回り (Clockwise) に見える面が表」です。
/// 上 → 右下 → 左下 の順は画面上で時計回りになるため、表向きとなり描画されます。
/// 順序を逆にすると裏面になり、背面カリング（<c>D3D12_CULL_MODE_BACK</c>）によって
/// 何も表示されなくなります。「三角形が出ない」ときの定番の原因です。
/// </para>
/// </remarks>
constexpr Vertex kTriangleVertices[] = {
    // 位置 { x, y, z }          色 { r, g, b, a }
    { {  0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } }, // 上　: 赤
    { {  0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } }, // 右下: 緑
    { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }, // 左下: 青
};

/// <summary>シェーダーファイルの場所（プロジェクトルートからの相対パス）。</summary>
constexpr const wchar_t* kShaderRelativePath = L"shaders/Triangle.hlsl";
} // namespace


/// <summary>ルートシグネチャ・PSO・頂点バッファを生成します。</summary>
void TrianglePipeline::Initialize(ID3D12Device* device, DXGI_FORMAT renderTargetFormat)
{
    CreateRootSignature(device);
    CreatePipelineState(device, renderTargetFormat);
    CreateVertexBuffer(device);

    Log(L"三角形描画パイプラインを構築しました。");
}


/// <summary>ルートシグネチャを生成します。</summary>
void TrianglePipeline::CreateRootSignature(ID3D12Device* device)
{
    //-------------------------------------------------------------------------
    // 今回のシェーダーは定数バッファもテクスチャも使いません。
    // したがってパラメータ 0 個の「空のルートシグネチャ」になります。
    //-------------------------------------------------------------------------
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters     = 0;
    rootSignatureDesc.pParameters       = nullptr;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers   = nullptr;

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


/// <summary>HLSL ファイルをコンパイルして、GPU 用のバイトコードを得ます。</summary>
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


/// <summary>HLSL をコンパイルし、パイプラインステートオブジェクトを生成します。</summary>
void TrianglePipeline::CreatePipelineState(ID3D12Device* device, DXGI_FORMAT renderTargetFormat)
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
    //     合計 28 バイト
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
    //   三角形 1 枚だけなら不要なので無効化します。
    //   → 深度バッファを作る必要も無くなり、初期化がその分シンプルになります。
    //-------------------------------------------------------------------------
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable   = FALSE;
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

    // 深度バッファを使わないので UNKNOWN
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

    // MSAA 無し
    psoDesc.SampleDesc.Count   = 1;
    psoDesc.SampleDesc.Quality = 0;

    psoDesc.NodeMask = 0;
    psoDesc.Flags    = D3D12_PIPELINE_STATE_FLAG_NONE;

    DX_CHECK(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
}


/// <summary>頂点バッファを作り、頂点データを書き込みます。</summary>
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


/// <summary>コマンドリストに「三角形を描く」命令を記録します。</summary>
void TrianglePipeline::RecordDrawCommands(ID3D12GraphicsCommandList* commandList) const
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
    commandList->DrawInstanced(
        _countof(kTriangleVertices), // 頂点数 = 3
        1,                           // インスタンス数 = 1
        0,                           // 何番目の頂点から描き始めるか
        0);                          // 何番目のインスタンスから描き始めるか
}

} // namespace dx12
