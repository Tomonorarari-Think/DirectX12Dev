//=============================================================================
// Renderer.cpp
//   Renderer の実装。1 フレームの描画手順が全てここに集約されている。
//=============================================================================
#include "Renderer.h"

namespace dx12
{
namespace
{
//-----------------------------------------------------------------------------
// 画面のクリア色（RGBA、各 0.0〜1.0）
//   三角形が描かれていない部分がこの色になります。
//   真っ黒 (0,0,0) にすると「描画できているのか、そもそも動いていないのか」の
//   区別が付きにくいため、学習用にはっきり分かる濃紺にしています。
//-----------------------------------------------------------------------------
constexpr float kClearColor[4] = { 0.10f, 0.15f, 0.30f, 1.0f };
} // namespace


//-----------------------------------------------------------------------------
// デストラクタ
//   ★ メンバ変数（ComPtr）が破棄される前に、必ず GPU の完了を待ちます。
//     GPU が使用中のリソースを解放するとクラッシュや警告の原因になるためです。
//-----------------------------------------------------------------------------
Renderer::~Renderer()
{
    if (m_initialized)
    {
        WaitForGpu();
    }
}


//-----------------------------------------------------------------------------
// Initialize
//-----------------------------------------------------------------------------
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

    // (4) コマンドアロケータとコマンドリスト
    CreateCommandObjects();

    // (5) 三角形描画用のパイプライン
    m_trianglePipeline.Initialize(device, SwapChain::kBackBufferFormat);

    // (6) ビューポート／シザー矩形
    UpdateViewportAndScissor(width, height);

    m_initialized = true;
    Log(L"レンダラの初期化が完了しました。");
}


//-----------------------------------------------------------------------------
// CreateCommandObjects : コマンドアロケータとコマンドリストを作る
//-----------------------------------------------------------------------------
void Renderer::CreateCommandObjects()
{
    ID3D12Device* device = m_graphicsDevice.Device();

    // コマンドアロケータ（命令を書き込むメモリの持ち主）
    DX_CHECK(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_commandAllocator)));

    //-------------------------------------------------------------------------
    // コマンドリスト（命令を記録するための道具）
    //
    //   ★ CreateCommandList で作られた直後のコマンドリストは
    //     「開いた（記録中の）状態」になっています。
    //     Render() の先頭では毎回 Reset() を呼びますが、Reset は
    //     「閉じたリストに対して」呼ぶものなので、
    //     生成直後に一度 Close() して閉じた状態に揃えておきます。
    //     これを忘れると最初のフレームでエラーになります。
    //-------------------------------------------------------------------------
    DX_CHECK(device->CreateCommandList(
        0,                                  // NodeMask
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocator.Get(),           // 紐づけるアロケータ
        nullptr,                            // 初期 PSO（後で Reset 時に指定するので nullptr）
        IID_PPV_ARGS(&m_commandList)));

    DX_CHECK(m_commandList->Close());

    Log(L"コマンドアロケータとコマンドリストを生成しました。");
}


//-----------------------------------------------------------------------------
// UpdateViewportAndScissor
//-----------------------------------------------------------------------------
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
    //   D3D12_RECT の right / bottom は「その位置を含まない」境界です。
    m_scissorRect.left   = 0;
    m_scissorRect.top    = 0;
    m_scissorRect.right  = static_cast<LONG>(width);
    m_scissorRect.bottom = static_cast<LONG>(height);
}


//-----------------------------------------------------------------------------
// RecordResourceBarrier : リソースの状態遷移バリアを記録する
//-----------------------------------------------------------------------------
void Renderer::RecordResourceBarrier(ID3D12GraphicsCommandList* commandList,
                                     ID3D12Resource* resource,
                                     D3D12_RESOURCE_STATES stateBefore,
                                     D3D12_RESOURCE_STATES stateAfter)
{
    D3D12_RESOURCE_BARRIER barrier = {};

    //-------------------------------------------------------------------------
    // バリアには 3 種類あります。
    //   TRANSITION … リソースの用途（状態）を切り替える ← 最もよく使う
    //   ALIASING   … 同じメモリを複数リソースで共有するときの整合性確保
    //   UAV        … 順序保証のない書き込み同士の実行順序を制御する
    //-------------------------------------------------------------------------
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

    barrier.Transition.pResource = resource;

    // Subresource : テクスチャのミップレベルや配列要素を個別に指定できる。
    //   D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES は「全部まとめて」の意味。
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    //-------------------------------------------------------------------------
    // ★ StateBefore には「現在の実際の状態」を正確に書かなければなりません。
    //   DirectX 12 はリソースの状態を追跡してくれないため、
    //   ここが実際と食い違うとデバッグレイヤーがエラーを出します
    //   （デバッグレイヤーを有効にすべき理由がまさにこれです）。
    //-------------------------------------------------------------------------
    barrier.Transition.StateBefore = stateBefore;
    barrier.Transition.StateAfter  = stateAfter;

    // 複数のバリアをまとめて発行できるため、引数は配列
    commandList->ResourceBarrier(1, &barrier);
}


//-----------------------------------------------------------------------------
// Render : 1 フレーム描画する
//-----------------------------------------------------------------------------
void Renderer::Render()
{
    //=========================================================================
    // (1) コマンドアロケータのリセット
    //
    //   前フレームに記録した命令用メモリを再利用します。
    //   ★ 前提: そのフレームの GPU 実行が完了していること。
    //     本実装は毎フレーム末尾で GPU 完了を待っているため、この前提を満たします。
    //=========================================================================
    DX_CHECK(m_commandAllocator->Reset());

    //=========================================================================
    // (2) コマンドリストのリセット（記録の開始）
    //
    //   第 2 引数に PSO を渡すことで、リセットと同時に
    //   「このパイプライン設定で描く」状態にできます。
    //   ここで nullptr を渡した場合は、描画前に SetPipelineState を呼ぶ必要があります。
    //=========================================================================
    //   PSO は TrianglePipeline が所有しているため、ここでは nullptr を渡し、
    //   描画直前に TrianglePipeline::RecordDrawCommands の中で設定させています。
    //   こうすることで「PSO を知っているのはパイプラインだけ」という
    //   責務の分離を保てます。
    DX_CHECK(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

    ID3D12Resource* backBuffer = m_swapChain.CurrentBackBuffer();

    //=========================================================================
    // (3) バリア : PRESENT → RENDER_TARGET
    //
    //   スワップチェーンから取得したバックバッファは、
    //   Present 直後は「表示用 (PRESENT)」の状態になっています。
    //   これに描き込むには「描画先 (RENDER_TARGET)」へ切り替える必要があります。
    //=========================================================================
    RecordResourceBarrier(
        m_commandList.Get(),
        backBuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    //=========================================================================
    // (4) ビューポートとシザー矩形の設定
    //
    //   コマンドリストは Reset するたびに設定が全て初期状態に戻ります。
    //   そのため「初期化時に一度設定すれば OK」ではなく、
    //   毎フレーム設定し直す必要があります。ここは間違えやすいポイントです。
    //=========================================================================
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    //=========================================================================
    // (5) レンダーターゲット（描画先）の設定
    //=========================================================================
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_swapChain.CurrentRenderTargetView();

    m_commandList->OMSetRenderTargets(
        1,             // レンダーターゲットの数
        &rtvHandle,    // RTV ディスクリプタの配列
        FALSE,         // TRUE にすると「連続した複数の RTV」として扱う
        nullptr);      // 深度ステンシルビュー（今回は使わない）

    //=========================================================================
    // (6) 画面のクリア
    //
    //   前フレームの絵が残っていると困るので、毎フレーム塗り潰します。
    //   第 3・4 引数で「一部の矩形だけクリア」も可能ですが、
    //   0 / nullptr を渡すと全体をクリアします。
    //=========================================================================
    m_commandList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);

    //=========================================================================
    // (7) 三角形の描画命令を記録
    //=========================================================================
    m_trianglePipeline.RecordDrawCommands(m_commandList.Get());

    //=========================================================================
    // (8) バリア : RENDER_TARGET → PRESENT
    //
    //   描き終わったので、表示できる状態へ戻します。
    //   これを忘れると Present 時にデバッグレイヤーがエラーを出します。
    //=========================================================================
    RecordResourceBarrier(
        m_commandList.Get(),
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);

    //=========================================================================
    // (9) 記録の終了
    //   Close を呼ぶまでコマンドリストは GPU に投入できません。
    //=========================================================================
    DX_CHECK(m_commandList->Close());

    //=========================================================================
    // (10) GPU へ投入 — ここで初めて GPU が動き始める
    //=========================================================================
    m_commandQueue.ExecuteCommandList(m_commandList.Get());

    //=========================================================================
    // (11) 画面に表示
    //=========================================================================
    m_swapChain.Present(true /* 垂直同期あり */);

    //=========================================================================
    // (12) GPU の完了を待つ
    //
    //   ★ この実装は「分かりやすさ優先」の単純な方式です。
    //     CPU が毎フレーム GPU を待つため、両者が交互にしか動けず、
    //     本来得られるはずの並列性を捨てています。
    //
    //     正しい改善方法は「フレームバッファリング」です。
    //       ・コマンドアロケータをバックバッファ枚数ぶん用意する
    //       ・フレームごとのフェンス値を記録しておく
    //       ・N フレーム前の完了だけを待つ
    //     こうすると CPU は次フレームの記録を先行して進められます。
    //     詳細は docs/03_描画フローと1フレームの流れ.md を参照してください。
    //=========================================================================
    m_commandQueue.Flush();
}


//-----------------------------------------------------------------------------
// Resize
//-----------------------------------------------------------------------------
void Renderer::Resize(uint32_t width, uint32_t height)
{
    if (!m_initialized)
    {
        return;
    }

    // ★ バックバッファを差し替える前に、GPU が使い終わるのを必ず待つ
    WaitForGpu();

    m_swapChain.Resize(m_graphicsDevice.Device(), width, height);
    UpdateViewportAndScissor(width, height);
}


//-----------------------------------------------------------------------------
// WaitForGpu
//-----------------------------------------------------------------------------
void Renderer::WaitForGpu()
{
    m_commandQueue.Flush();
}

} // namespace dx12
