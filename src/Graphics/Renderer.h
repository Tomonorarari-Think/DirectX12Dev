//=============================================================================
// Renderer.h
//   デバイス／キュー／スワップチェーン／パイプラインを束ね、1 フレームを描く。
//=============================================================================
#pragma once

#include "../Common/FrameTimer.h"
#include "../Common/GraphicsCommon.h"
#include "CommandQueue.h"
#include "GraphicsDevice.h"
#include "SwapChain.h"
#include "TrianglePipeline.h"

#include <array>

namespace dx12
{

/// <summary>
/// これまでの部品を束ね、「1 フレーム描く」という仕事を担当するクラス。
/// </summary>
/// <remarks>
/// <para>
/// <b>1 フレームの流れ（<see cref="Render"/> の中身）</b>
/// <list type="number">
///   <item>このバックバッファを前回使ったときの GPU 処理の完了を待つ</item>
///   <item>コマンドアロケータをリセット … そのフレーム用の記録メモリを再利用する</item>
///   <item>コマンドリストをリセット … 記録を開始する</item>
///   <item>リソースバリア（PRESENT → RENDER_TARGET）</item>
///   <item>ビューポート / シザー矩形を設定</item>
///   <item>レンダーターゲットを設定</item>
///   <item>画面をクリア</item>
///   <item>三角形の描画命令を記録</item>
///   <item>リソースバリア（RENDER_TARGET → PRESENT）</item>
///   <item>コマンドリストを Close</item>
///   <item>コマンドキューへ投入 … ここで初めて GPU が動き始める</item>
///   <item>Present … 画面に表示</item>
///   <item>フェンス値を記録 … 「このフレームの完了印」を予約する</item>
/// </list>
/// </para>
/// <para>
/// <b>コマンドアロケータ (ID3D12CommandAllocator) とは</b><br/>
/// コマンドリストに記録した命令が実際に格納されるメモリの持ち主です。
/// コマンドリストは「記録するためのペン」、アロケータは「ノート」に相当します。
/// </para>
/// <para>
/// 重要な制約として、アロケータの <c>Reset()</c> は
/// 「そのノートに書かれた命令を GPU が実行し終えた後」でなければ呼べません。
/// 実行中のノートを消しゴムで消すようなもので、GPU がクラッシュします。
/// この制約こそが、フレーム間の同期（フェンス）が必要な最大の理由です。
/// </para>
/// <para>
/// <b>フレームバッファリング（このクラスの中核）</b><br/>
/// 素直に上の制約を満たすなら「毎フレーム末尾で GPU の完了を待つ」で済みます。
/// しかしそれでは CPU と GPU が交互にしか動けません。
/// <code>
/// CPU: [記録]              [記録]              [記録]
/// GPU:        [実行][待機]        [実行][待機]        [実行]
/// </code>
/// そこで「ノート（アロケータ）をバックバッファの枚数ぶん用意する」ことで、
/// GPU がノート 0 を実行している間に CPU がノート 1 へ書き込めるようにします。
/// <code>
/// CPU: [記録0][記録1][記録2][記録3]...
/// GPU:        [実行0][実行1][実行2]...
///             (CPU が 1 フレーム先行できる)
/// </code>
/// 待つのは「今から使おうとしているノートが空いたか」だけです。
/// そのために、フレームごとに <c>Signal()</c> したフェンス値を配列で覚えておき、
/// 同じ番号のノートを再び使うときにその値だけを待ちます。
/// </para>
/// </remarks>
class Renderer
{
public:
    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    Renderer() = default;

    /// <summary>デストラクタ。破棄の前に GPU の作業完了を待ちます。</summary>
    /// <remarks>
    /// メンバ変数（ComPtr）が破棄される前に必ず GPU の完了を待ちます。
    /// GPU が使用中のリソースを解放するとクラッシュや警告の原因になるためです。
    /// </remarks>
    ~Renderer();

    /// <summary>コピー構築は禁止です。</summary>
    Renderer(const Renderer&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    Renderer& operator=(const Renderer&) = delete;

    /// <summary>DirectX 12 の初期化を一式行います。</summary>
    /// <param name="hwnd">描画先のウィンドウ。</param>
    /// <param name="width">初期の描画解像度（幅、ピクセル）。</param>
    /// <param name="height">初期の描画解像度（高さ、ピクセル）。</param>
    /// <exception cref="HrException">DirectX の初期化に失敗した場合。</exception>
    /// <exception cref="std::runtime_error">シェーダーファイルが見つからない場合。</exception>
    void Initialize(HWND hwnd, uint32_t width, uint32_t height);

    /// <summary>1 フレーム描画して画面に表示します。</summary>
    /// <exception cref="HrException">コマンドの記録または投入に失敗した場合。</exception>
    void Render();

    /// <summary>ウィンドウサイズ変更に追従します。</summary>
    /// <param name="width">新しい幅（ピクセル）。</param>
    /// <param name="height">新しい高さ（ピクセル）。</param>
    /// <remarks>
    /// 内部で GPU の完了待ちを行ってからスワップチェーンを作り直します。
    /// 初期化前に呼ばれた場合は何もしません。
    /// </remarks>
    void Resize(uint32_t width, uint32_t height);

    /// <summary>GPU の全作業完了を待ちます（終了時に必ず呼ぶこと）。</summary>
    /// <remarks>
    /// これを忘れると、GPU がまだ使用中のリソースを CPU 側が解放してしまい、
    /// 終了時にクラッシュしたり、デバッグレイヤーが警告を出したりします。
    /// あわせて <c>m_frameFenceValues</c> を最新の完了値で埋めます。
    /// </remarks>
    void WaitForGpu();

private:
    /// <summary>コマンドアロケータ（バックバッファ枚数ぶん）とコマンドリストを生成します。</summary>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void CreateCommandObjects();

    /// <summary>ビューポートとシザー矩形をウィンドウサイズに合わせて更新します。</summary>
    /// <param name="width">新しい幅（ピクセル）。</param>
    /// <param name="height">新しい高さ（ピクセル）。</param>
    void UpdateViewportAndScissor(uint32_t width, uint32_t height);

    /// <summary>リソースの状態遷移バリアをコマンドリストに記録します。</summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="resource">状態を変えるリソース。</param>
    /// <param name="stateBefore">現在の状態。実際の状態と一致している必要があります。</param>
    /// <param name="stateAfter">これから使う状態。</param>
    /// <remarks>
    /// <para>
    /// <b>リソースバリア (Resource Barrier) とは</b><br/>
    /// 「このリソースの用途を今から変えます」と GPU に宣言する命令です。
    /// </para>
    /// <para>
    /// GPU 内部では、同じメモリでも用途によって最適な保持形式が異なります
    /// （描画先として書き込むとき用に圧縮された状態、テクスチャとして
    /// 読むとき用の状態、画面表示用の状態…）。
    /// 用途を切り替えるときは、この宣言によって
    /// 「形式の変換」と「それ以前の処理の完了待ち」を GPU に行わせます。
    /// </para>
    /// <para>
    /// DirectX 11 ではドライバが自動でやっていました。DirectX 12 では
    /// 開発者が明示する代わりに、不要なバリアを省いて高速化できます。
    /// 逆に言えば、書き忘れると描画結果が壊れる・クラッシュする原因になります。
    /// </para>
    /// </remarks>
    static void RecordResourceBarrier(ID3D12GraphicsCommandList* commandList,
                                      ID3D12Resource* resource,
                                      D3D12_RESOURCE_STATES stateBefore,
                                      D3D12_RESOURCE_STATES stateAfter);

private:
    /// <summary>デバイス・DXGI ファクトリ。</summary>
    GraphicsDevice m_graphicsDevice;

    /// <summary>コマンドキューとフェンス。</summary>
    CommandQueue m_commandQueue;

    /// <summary>スワップチェーンとバックバッファ・RTV。</summary>
    SwapChain m_swapChain;

    /// <summary>三角形描画用の PSO と頂点バッファ。</summary>
    TrianglePipeline m_trianglePipeline;

    /// <summary>コマンドアロケータ（バックバッファの枚数ぶん用意する）。</summary>
    /// <remarks>
    /// フレーム N では <c>m_commandAllocators[N % kBackBufferCount]</c> を使います。
    /// GPU がまだ [0] の命令を実行中でも、CPU は [1] に記録を始められます。
    /// </remarks>
    std::array<ComPtr<ID3D12CommandAllocator>, SwapChain::kBackBufferCount> m_commandAllocators;

    /// <summary>命令を記録するためのコマンドリスト。</summary>
    /// <remarks>
    /// アロケータと違い 1 本で足ります。
    /// <c>Reset()</c> のたびに「どのアロケータへ書くか」を指定し直せるためです。
    /// 複数スレッドから並列に記録する段階になったら本数を増やします。
    /// </remarks>
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    /// <summary>フレームごとのフェンス値（完了印）。</summary>
    /// <remarks>
    /// <para>
    /// <c>m_frameFenceValues[i]</c> は
    /// 「アロケータ [i] を使ったフレームの処理が終わったときの番号」です。
    /// 再び [i] を使う前にこの値を待てば、そのアロケータは安全に Reset できます。
    /// </para>
    /// <para>
    /// 初期値 0 は「まだ一度も使っていない ＝ 待つ必要なし」を意味します
    /// （フェンスの初期値も 0 なので、<c>WaitForFenceValue(0)</c> は即座に戻ります）。
    /// </para>
    /// </remarks>
    std::array<uint64_t, SwapChain::kBackBufferCount> m_frameFenceValues = {};

    /// <summary>フレーム時間の計測（1 秒ごとに FPS をログ出力する）。</summary>
    FrameTimer m_frameTimer;

    /// <summary>バックバッファ上の「描画に使う矩形領域」と深度範囲。</summary>
    /// <remarks>
    /// NDC 座標 (-1〜+1) を実際のピクセル座標へ変換する係数になります。
    /// </remarks>
    D3D12_VIEWPORT m_viewport = {};

    /// <summary>この矩形の外に出たピクセルを問答無用で捨てる設定。</summary>
    /// <remarks>
    /// <para>
    /// ビューポートと似ていますが役割が違います。
    /// <list type="bullet">
    ///   <item>ビューポート … 座標変換（どこに描くか）</item>
    ///   <item>シザー矩形 … 切り抜き（どこまで描いてよいか）</item>
    /// </list>
    /// </para>
    /// <para>
    /// 設定を忘れると（＝全て 0 のままだと）1 ピクセルも描画されません。
    /// 「画面が真っ黒で三角形が出ない」ときの定番の原因のひとつです。
    /// </para>
    /// </remarks>
    D3D12_RECT m_scissorRect = {};

    /// <summary>初期化が完了しているかどうか。</summary>
    bool m_initialized = false;
};

} // namespace dx12
