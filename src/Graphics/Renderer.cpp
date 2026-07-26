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
/// <summary>画面のクリア色（RGBA、各 0.0〜1.0）。</summary>
/// <remarks>
/// 三角形が描かれていない部分がこの色になります。
/// 真っ黒 (0,0,0) にすると「描画できているのか、そもそも動いていないのか」の
/// 区別が付きにくいため、学習用にはっきり分かる濃紺にしています。
/// </remarks>
constexpr float kClearColor[4] = { 0.10f, 0.15f, 0.30f, 1.0f };

/// <summary>垂直同期 (VSync) を使うかどうか。</summary>
/// <remarks>
/// <para>
/// <c>true</c> … モニタのリフレッシュに同期する。ティアリングが起きず、
/// GPU の無駄な仕事も減るため通常はこちらが正解。
/// ただしモニタのリフレッシュレートで頭打ちになります。<br/>
/// <c>false</c> … 上限を外して描けるだけ描く。
/// </para>
/// <para>
/// フレームバッファリングの効果を数値で確認したいときは <c>false</c> にしてください。
/// <c>true</c> のままだと、改善前も改善後もリフレッシュレートに張り付いて差が見えません。
/// </para>
/// </remarks>
constexpr bool kEnableVSync = true;
} // namespace


/// <summary>デストラクタ。破棄の前に GPU の作業完了を待ちます。</summary>
Renderer::~Renderer()
{
    if (m_initialized)
    {
        WaitForGpu();
    }
}


/// <summary>DirectX 12 の初期化を一式行います。</summary>
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
    //     定数バッファをフレーム数ぶん確保させるため、バックバッファ枚数を渡す
    m_trianglePipeline.Initialize(device,
                                  SwapChain::kBackBufferFormat,
                                  SwapChain::kBackBufferCount);

    // (6) ビューポート／シザー矩形
    UpdateViewportAndScissor(width, height);

    m_initialized = true;
    Log(L"レンダラの初期化が完了しました。");
}


/// <summary>コマンドアロケータ（バックバッファ枚数ぶん）とコマンドリストを生成します。</summary>
void Renderer::CreateCommandObjects()
{
    ID3D12Device* device = m_graphicsDevice.Device();

    //-------------------------------------------------------------------------
    // コマンドアロケータ（命令を書き込むメモリの持ち主）を
    // バックバッファの枚数ぶん作る。
    //
    //   これがフレームバッファリングの土台です。
    //   GPU がアロケータ [0] の命令を実行している間に、
    //   CPU はアロケータ [1] へ次フレームの命令を記録できます。
    //-------------------------------------------------------------------------
    for (uint32_t i = 0; i < SwapChain::kBackBufferCount; ++i)
    {
        DX_CHECK(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_commandAllocators[i])));
    }

    //-------------------------------------------------------------------------
    // コマンドリスト（命令を記録するための道具）
    //
    //   アロケータと違い 1 本で足ります。
    //   Reset() のたびに「どのアロケータへ書き込むか」を指定し直せるためです。
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
        m_commandAllocators[0].Get(),       // 紐づけるアロケータ（後で切り替える）
        nullptr,                            // 初期 PSO（後で Reset 時に指定するので nullptr）
        IID_PPV_ARGS(&m_commandList)));

    DX_CHECK(m_commandList->Close());

    Log(std::format(L"コマンドアロケータ {} 個とコマンドリストを生成しました。",
                    SwapChain::kBackBufferCount));
}


/// <summary>ビューポートとシザー矩形をウィンドウサイズに合わせて更新します。</summary>
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


/// <summary>リソースの状態遷移バリアをコマンドリストに記録します。</summary>
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


/// <summary>1 フレーム描画して画面に表示します。</summary>
void Renderer::Render()
{
    // フレーム時間を計測する（1 秒ごとに FPS がログに出ます）
    m_frameTimer.Tick();

    //=========================================================================
    // (0) このフレームで使う「ノート（アロケータ）」が空くのを待つ
    //
    //   バックバッファが 2 枚なので、フレーム番号は 0, 1, 0, 1 … と循環します。
    //   同じ番号のアロケータを再び使うということは、
    //   「2 フレーム前に記録した命令」を上書きするということです。
    //   その命令を GPU が実行し終えていなければ、待たなければなりません。
    //
    //   ★ ここがフレームバッファリングの核心です。
    //     「GPU の全作業」ではなく「このアロケータに関係する作業だけ」を待ちます。
    //     直前のフレームの GPU 処理はまだ走っていて構いません。
    //
    //   初回（値が 0）は WaitForFenceValue 側の早期リターンで即座に戻ります。
    //=========================================================================
    const uint32_t frameIndex = m_swapChain.CurrentBackBufferIndex();

    m_commandQueue.WaitForFenceValue(m_frameFenceValues[frameIndex]);

    //=========================================================================
    // (1) コマンドアロケータのリセット
    //
    //   (0) で完了を確認したので、安全に中身を捨てて再利用できます。
    //=========================================================================
    DX_CHECK(m_commandAllocators[frameIndex]->Reset());

    //=========================================================================
    // (2) コマンドリストのリセット（記録の開始）
    //
    //   第 1 引数で「今回どのアロケータへ書き込むか」を指定します。
    //   コマンドリストが 1 本で足りるのはこの仕組みのおかげです。
    //
    //   第 2 引数に PSO を渡すことで、リセットと同時に
    //   「このパイプライン設定で描く」状態にできます。
    //   PSO は TrianglePipeline が所有しているため、ここでは nullptr を渡し、
    //   描画直前に TrianglePipeline::RecordDrawCommands の中で設定させています。
    //   こうすることで「PSO を知っているのはパイプラインだけ」という
    //   責務の分離を保てます。
    //=========================================================================
    DX_CHECK(m_commandList->Reset(m_commandAllocators[frameIndex].Get(), nullptr));

    //=========================================================================
    // (2-b) このフレームの定数（変換行列）を更新する
    //
    //   ★ ここで書き込んでよい理由
    //     (0) で frameIndex のフェンスを待っているため、
    //     GPU はもう frameIndex 番の定数バッファ領域を読んでいません。
    //     待たずに書き換えると、描画中に値が変わって絵が壊れます。
    //
    //   縦横比は毎フレーム取得します。ウィンドウがリサイズされても
    //   三角形の形が歪まないようにするためです。
    //=========================================================================
    const float aspectRatio =
        static_cast<float>(m_swapChain.Width()) / static_cast<float>(m_swapChain.Height());

    m_trianglePipeline.Update(frameIndex,
                              aspectRatio,
                              static_cast<float>(m_frameTimer.TotalSeconds()));

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
    m_trianglePipeline.RecordDrawCommands(m_commandList.Get(), frameIndex);

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
    //
    //   ★ この呼び出しで SwapChain の「現在のバックバッファ番号」が変わります。
    //     そのため (12) では frameIndex（表示前に取得した値）を使います。
    //=========================================================================
    m_swapChain.Present(kEnableVSync);

    //=========================================================================
    // (12) このフレームの「完了印」を予約する
    //
    //   Signal() はコマンドキューの末尾に積まれるだけで、
    //   この行を実行した時点ではまだ GPU は動き終えていません。
    //   GPU がこのフレームの命令を全て処理し終えた瞬間に、
    //   フェンスのカウンタがこの値になります。
    //
    //   その値を frameIndex 番の欄に控えておき、
    //   次に同じアロケータを使うとき（＝ 2 フレーム後）に (0) で待ちます。
    //
    //   ★ ここで待たないことが重要です。
    //     待たずに戻ることで、CPU は次フレームの記録へすぐ進めます。
    //=========================================================================
    m_frameFenceValues[frameIndex] = m_commandQueue.Signal();
}


/// <summary>ウィンドウサイズ変更に追従します。</summary>
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


/// <summary>GPU の全作業完了を待ち、全フレームのフェンス値を揃えます。</summary>
void Renderer::WaitForGpu()
{
    // GPU の全作業が終わるまで待つ
    const uint64_t fenceValue = m_commandQueue.Flush();

    //-------------------------------------------------------------------------
    // ★ 全フレームのフェンス値をこの値で埋める
    //
    //   これを忘れると、古い（既に完了済みの）値が配列に残り続けます。
    //   実害が出るのはリサイズ後です。
    //   バックバッファ番号の進み方が変わることがあるため、
    //   「まだ完了していない」と誤認して不要な待機が入る可能性があります。
    //
    //   全て「今の完了値」に揃えておけば、次の Render では
    //   確実に待機なしで進めます。
    //-------------------------------------------------------------------------
    m_frameFenceValues.fill(fenceValue);
}

} // namespace dx12
