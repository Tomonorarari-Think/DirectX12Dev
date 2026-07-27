//=============================================================================
// Renderer.cpp
//   Renderer の実装。1 フレームの描画手順が全てここに集約されている。
//=============================================================================
#include "Renderer.h"

#include <format>

namespace dx12
{
namespace
{
/// <summary>
/// 画面のクリア色（RGBA、各 0.0〜1.0）。
/// </summary>
constexpr float kClearColor[4] = { 0.10f, 0.15f, 0.30f, 1.0f };

/// <summary>
/// 垂直同期 (VSync) を使うかどうか。
/// </summary>
constexpr bool kEnableVSync = true;

/// <summary>
/// シェーダー可視ディスクリプタヒープに確保する数。
/// </summary>
constexpr uint32_t kDescriptorHeapCapacity = 16;

/// <summary>
/// シーンに置くオブジェクトの数（立方体と床）。
/// </summary>
constexpr uint32_t kObjectCount = 2;

/// <summary>
/// 定数バッファ上でのオブジェクトの通し番号。
/// </summary>
enum ObjectIndex : uint32_t
{
    kCubeObjectIndex  = 0,
    kFloorObjectIndex = 1,
};

/// <summary>
/// 立方体が 1 回転するのにかかる秒数。
/// </summary>
constexpr float kSecondsPerRotation = 8.0f;

/// <summary>
/// 立方体の一辺の半分の長さ。
/// </summary>
constexpr float kCubeHalfExtent = 0.5f;

/// <summary>
/// 立方体の中心を置く高さ。床から浮かせて影が見やすい位置にします。
/// </summary>
constexpr float kCubeCenterHeight = 0.9f;

/// <summary>
/// 床の中心から端までの距離。
/// </summary>
constexpr float kFloorHalfExtent = 2.5f;

/// <summary>
/// 床を置く高さ。
/// </summary>
constexpr float kFloorHeight = 0.0f;

/// <summary>
/// 床でテクスチャを繰り返す回数。サンプラーが WRAP なので模様が並びます。
/// </summary>
constexpr float kFloorUvTiling = 1.0f;

/// <summary>
/// シャドウマップの一辺のピクセル数。
/// </summary>
/// <remarks>
/// 大きいほど影の輪郭が細かくなりますが、メモリと描画時間が増えます。
/// この値を変えたら `shaders/Mesh.hlsl` の `kShadowMapSize` も合わせてください。
/// </remarks>
constexpr uint32_t kShadowMapSize = 2048;

/// <summary>
/// 影を落とす範囲の半径。この球に収まる範囲だけがシャドウマップに入ります。
/// </summary>
constexpr float kSceneRadius = 3.6f;
} // namespace


/// <summary>
/// デストラクタ。破棄の前に GPU の作業完了を待ちます。
/// </summary>
Renderer::~Renderer()
{
    if (m_initialized)
    {
        WaitForGpu();
    }
}


/// <summary>
/// DirectX 12 の初期化を一式行います。
/// </summary>
void Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    // (1) デバイス（DXGI ファクトリ、アダプタ、D3D12 デバイス）
    m_graphicsDevice.Initialize();

    ID3D12Device* device = m_graphicsDevice.Device();

    // (2) コマンドキューとフェンス
    m_commandQueue.Initialize(device);

    // (3) スワップチェーンと RTV
    //     ※ スワップチェーンの生成にはコマンドキューが必要なので、この順序になる
    m_swapChain.Initialize(
        m_graphicsDevice.Factory(),
        device,
        m_commandQueue.Get(),
        hwnd,
        width,
        height);

    // (4) 深度バッファと DSV
    //     レンダーターゲットと同じ解像度でなければならない
    m_depthBuffer.Initialize(device, width, height);

    // (5) コマンドアロケータとコマンドリスト
    CreateCommandObjects();

    // (6) シェーダー可視ディスクリプタヒープ
    //     テクスチャとシャドウマップの SRV を置く場所。
    //     数が増えても 1 本のヒープを共有するため、少し余裕を持たせておく。
    m_descriptorHeap.Initialize(device, kDescriptorHeapCapacity);

    // (6-b) シャドウマップ
    //     パイプラインが影用 PSO を作るのに深度形式を要るので、先に作る。
    m_shadowMap.Initialize(device, m_descriptorHeap, kShadowMapSize);

    // (7) メッシュ描画用のパイプライン
    //     PSO は描画先の形式（RTV / DSV）を知っている必要があるため両方渡す。
    m_meshPipeline.Initialize(device,
                              SwapChain::kBackBufferFormat,
                              DepthBuffer::kFormat,
                              SwapChain::kBackBufferCount,
                              kObjectCount,
                              ShadowMap::kDepthStencilViewFormat,
                              m_commandQueue,
                              m_descriptorHeap);

    // (8) 描くもの（形状データ）
    CreateSceneMeshes();

    // (9) 光源から見た深度を書き込む先と、その視点
    //     ライトの設定は MeshPipeline が持っているので、そこから受け取る。
    m_shadowMap.SetLight(MeshPipeline::LightDirection(),
                         DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
                         kSceneRadius);

    // (10) ビューポート／シザー矩形
    UpdateViewportAndScissor(width, height);

    m_initialized = true;
    Log(L"レンダラの初期化が完了しました。");
}


/// <summary>
/// コマンドアロケータ（バックバッファ枚数ぶん）とコマンドリストを生成します。
/// </summary>
void Renderer::CreateCommandObjects()
{
    ID3D12Device* device = m_graphicsDevice.Device();

    // コマンドアロケータ（命令を書き込むメモリの持ち主）を
    // バックバッファの枚数ぶん作る。
    for (uint32_t i = 0; i < SwapChain::kBackBufferCount; ++i)
    {
        DX_CHECK(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_commandAllocators[i])));
    }

    // コマンドリスト（命令を記録するための道具）
    DX_CHECK(device->CreateCommandList(
        0,                                  // NodeMask
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocators[0].Get(),       // 紐づけるアロケータ（後で切り替える）
        nullptr,                            // 初期 PSO（後で Reset 時に指定するので nullptr）
        IID_PPV_ARGS(&m_commandList)));

    DX_CHECK(m_commandList->Close());

    Log(std::format(L"コマンドアロケータ {} 個とコマンドリストを生成しました。",
                    SwapChain::kBackBufferCount));
}


/// <summary>
/// シーンに置くメッシュを生成します。
/// </summary>
void Renderer::CreateSceneMeshes()
{
    ID3D12Device* device = m_graphicsDevice.Device();

    // 形状データの生成は DirectX を使わない純粋な計算なので Geometry に分けてある。
    m_cube.Initialize(device, m_commandQueue, CreateCube(kCubeHalfExtent), L"立方体");

    m_floor.Initialize(
        device,
        m_commandQueue,
        CreatePlane(kFloorHalfExtent, kFloorHeight, kFloorUvTiling),
        L"床");
}


/// <summary>
/// このフレームぶんの定数バッファを更新します。
/// </summary>
void Renderer::UpdateConstants(uint32_t frameIndex)
{
    using namespace DirectX;

    // 縦横比は毎フレーム渡す。ウィンドウをリサイズしても歪まないようにするため。
    m_camera.SetAspectRatio(
        static_cast<float>(m_swapChain.Width()) / static_cast<float>(m_swapChain.Height()));

    const XMMATRIX viewProjection = m_camera.ViewProjectionMatrix();

    // カメラとライトは全オブジェクト共通なので 1 回だけ書く。
    // 光源から見た行列も渡す。影を描くときと、影の中かを調べるときの両方で使う。
    m_meshPipeline.UpdateFrameConstants(
        frameIndex, viewProjection, m_camera.Position(), m_shadowMap.LightViewProjection());

    // 立方体 : 2 軸で回しながら、床から浮かせた位置に置く。
    const float angle =
        static_cast<float>(m_frameTimer.TotalSeconds()) * (XM_2PI / kSecondsPerRotation);

    const XMMATRIX cubeWorld = XMMatrixRotationY(angle)
                             * XMMatrixRotationX(angle * 0.45f)
                             * XMMatrixTranslation(0.0f, kCubeCenterHeight, 0.0f);

    m_meshPipeline.UpdateObjectConstants(
        frameIndex, kCubeObjectIndex, cubeWorld, viewProjection);

    // 床 : 動かさないのでワールド行列は単位行列。
    m_meshPipeline.UpdateObjectConstants(
        frameIndex, kFloorObjectIndex, XMMatrixIdentity(), viewProjection);
}


/// <summary>
/// シーンの全メッシュを描く命令をコマンドリストに記録します。
/// </summary>
void Renderer::RecordMeshDrawCommands(uint32_t frameIndex)
{
    ID3D12GraphicsCommandList* commandList = m_commandList.Get();

    // オブジェクトごとに変えるのは定数バッファと頂点バッファだけ。
    m_meshPipeline.BindObject(commandList, frameIndex, kFloorObjectIndex);
    m_floor.RecordDrawCommands(commandList);

    m_meshPipeline.BindObject(commandList, frameIndex, kCubeObjectIndex);
    m_cube.RecordDrawCommands(commandList);
}


/// <summary>
/// 光源から見た深度をシャドウマップへ描きます。
/// </summary>
void Renderer::RecordShadowPass(uint32_t frameIndex)
{
    ID3D12GraphicsCommandList* commandList = m_commandList.Get();

    // バリア・ビューポート・描画先の設定・クリアまでを ShadowMap が行う。
    m_shadowMap.BeginRender(commandList);

    // 影の形しか要らないので、専用の（ピクセルシェーダーの無い）設定を使う。
    m_meshPipeline.BindShadowPass(commandList, frameIndex);

    // ★ 画面を描くときと同じメッシュを、同じワールド行列で描く。
    //   ここがずれると、物体と影の位置が合わなくなる。
    RecordMeshDrawCommands(frameIndex);

    // テクスチャとして読める状態に戻す。
    m_shadowMap.EndRender(commandList);
}


/// <summary>
/// ビューポートとシザー矩形をウィンドウサイズに合わせて更新します。
/// </summary>
void Renderer::UpdateViewportAndScissor(uint32_t width, uint32_t height)
{
    // ビューポート : バックバッファ全体を使う
    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;
    m_viewport.Width    = static_cast<float>(width);
    m_viewport.Height   = static_cast<float>(height);

    // 深度値の出力範囲。既定の 0.0〜1.0 をそのまま使う
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;

    // シザー矩形 : 切り抜かない（＝バックバッファ全体を許可する）
    m_scissorRect.left   = 0;
    m_scissorRect.top    = 0;
    m_scissorRect.right  = static_cast<LONG>(width);
    m_scissorRect.bottom = static_cast<LONG>(height);
}


/// <summary>
/// リソースの状態遷移バリアをコマンドリストに記録します。
/// </summary>
void Renderer::RecordResourceBarrier(ID3D12GraphicsCommandList* commandList,
                                     ID3D12Resource* resource,
                                     D3D12_RESOURCE_STATES stateBefore,
                                     D3D12_RESOURCE_STATES stateAfter)
{
    D3D12_RESOURCE_BARRIER barrier = {};

    // バリアには 3 種類あります。
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

    barrier.Transition.pResource = resource;

    // Subresource : テクスチャのミップレベルや配列要素を個別に指定できる。
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    // ★ StateBefore には「現在の実際の状態」を正確に書かなければなりません。
    barrier.Transition.StateBefore = stateBefore;
    barrier.Transition.StateAfter  = stateAfter;

    // 複数のバリアをまとめて発行できるため、引数は配列
    commandList->ResourceBarrier(1, &barrier);
}


/// <summary>
/// 1 フレーム描画して画面に表示します。
/// </summary>
void Renderer::Render()
{
    // フレーム時間を計測する（1 秒ごとに FPS がログに出ます）
    m_frameTimer.Tick();

    // (0) このフレームで使う「ノート（アロケータ）」が空くのを待つ
    //   バックバッファが 2 枚なので、フレーム番号は 0, 1, 0, 1 … と循環します。
    const uint32_t frameIndex = m_swapChain.CurrentBackBufferIndex();

    m_commandQueue.WaitForFenceValue(m_frameFenceValues[frameIndex]);

    // (1) コマンドアロケータのリセット
    //   (0) で完了を確認したので、安全に中身を捨てて再利用できます。
    DX_CHECK(m_commandAllocators[frameIndex]->Reset());

    // (2) コマンドリストのリセット（記録の開始）
    DX_CHECK(m_commandList->Reset(m_commandAllocators[frameIndex].Get(), nullptr));

    // (2-b) このフレームの定数（変換行列とライト）を更新する
    //   (0) でこのフレームのフェンスを待っているため、GPU はもうこの領域を読んでいない。
    UpdateConstants(frameIndex);

    // (2-c) ディスクリプタヒープの設定
    //   ★ SetGraphicsRootDescriptorTable より前に呼ぶ必要があります。
    //     ルートシグネチャを切り替えても、この設定は残ります。
    ID3D12DescriptorHeap* const descriptorHeaps[] = { m_descriptorHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // (2-d) 第 1 パス : 光源から見た深度をシャドウマップへ描く
    //   ★ 画面を描く前に済ませておく必要があります。
    //     第 2 パスは、この結果をテクスチャとして読むためです。
    RecordShadowPass(frameIndex);

    ID3D12Resource* backBuffer = m_swapChain.CurrentBackBuffer();

    // ここから第 2 パス : いつも通り画面へ描く
    // (3) バリア : PRESENT → RENDER_TARGET
    //   スワップチェーンから取得したバックバッファは、
    //   Present 直後は「表示用 (PRESENT)」の状態になっています。
    RecordResourceBarrier(
        m_commandList.Get(),
        backBuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    // (4) ビューポートとシザー矩形の設定
    //   ★ 影のパスでシャドウマップの大きさに変えてあるので、必ず戻します。
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // (5) レンダーターゲット（描画先）の設定
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_swapChain.CurrentRenderTargetView();
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_depthBuffer.DepthStencilView();

    m_commandList->OMSetRenderTargets(
        1,             // レンダーターゲットの数
        &rtvHandle,    // RTV ディスクリプタの配列
        FALSE,         // TRUE にすると「連続した複数の RTV」として扱う
        &dsvHandle);   // 深度ステンシルビュー（nullptr にすると深度テストは働かない）

    // (6) 画面のクリア
    //   前フレームの絵が残っていると困るので、毎フレーム塗り潰します。
    m_commandList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);

    // (6-b) 深度バッファのクリア
    //   ★ 色のクリアと同じくらい重要です。忘れると前フレームの深度が残り、
    //     2 フレーム目以降で「何も描かれない」「ちらつく」といった症状になります。
    m_commandList->ClearDepthStencilView(
        dsvHandle,
        D3D12_CLEAR_FLAG_DEPTH,     // 深度のみクリア（ステンシルは対象外）
        DepthBuffer::kClearDepth,   // 一番奥の値。作成時の最適化クリア値と一致必須
        0,                          // ステンシルのクリア値（未使用）
        0, nullptr);                // 部分クリアの矩形（0 / nullptr で全体）

    // (7) シーン（床と立方体）の描画命令を記録
    //   影のパスでルートシグネチャを切り替えたので、ここで改めて設定し直す。
    m_meshPipeline.Bind(m_commandList.Get(), frameIndex, m_shadowMap.ShaderResourceView());

    RecordMeshDrawCommands(frameIndex);

    // (8) バリア : RENDER_TARGET → PRESENT
    //   描き終わったので、表示できる状態へ戻します。
    RecordResourceBarrier(
        m_commandList.Get(),
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);

    // (9) 記録の終了
    //   Close を呼ぶまでコマンドリストは GPU に投入できません。
    DX_CHECK(m_commandList->Close());

    // (10) GPU へ投入 — ここで初めて GPU が動き始める
    m_commandQueue.ExecuteCommandList(m_commandList.Get());

    // (11) 画面に表示
    //   ★ この呼び出しで SwapChain の「現在のバックバッファ番号」が変わります。
    m_swapChain.Present(kEnableVSync);

    // (12) このフレームの「完了印」を予約する
    //   Signal() はコマンドキューの末尾に積まれるだけで、
    //   この行を実行した時点ではまだ GPU は動き終えていません。
    m_frameFenceValues[frameIndex] = m_commandQueue.Signal();
}


/// <summary>
/// ウィンドウサイズ変更に追従します。
/// </summary>
void Renderer::Resize(uint32_t width, uint32_t height)
{
    if (!m_initialized)
    {
        return;
    }

    // ★ バックバッファを差し替える前に、GPU が使い終わるのを必ず待つ
    WaitForGpu();

    m_swapChain.Resize(m_graphicsDevice.Device(), width, height);

    // 深度バッファもレンダーターゲットと同じ解像度に作り直す。
    m_depthBuffer.Resize(m_graphicsDevice.Device(), width, height);

    UpdateViewportAndScissor(width, height);
}


/// <summary>
/// GPU の全作業完了を待ち、全フレームのフェンス値を揃えます。
/// </summary>
void Renderer::WaitForGpu()
{
    // GPU の全作業が終わるまで待つ
    const uint64_t fenceValue = m_commandQueue.Flush();

    // ★ 全フレームのフェンス値をこの値で埋める
    //   これを忘れると、古い（既に完了済みの）値が配列に残り続けます。
    m_frameFenceValues.fill(fenceValue);
}

} // namespace dx12
