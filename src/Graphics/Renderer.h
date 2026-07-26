//=============================================================================
// Renderer.h
//   デバイス／キュー／スワップチェーン／パイプラインを束ね、1 フレームを描く。
//=============================================================================
#pragma once

#include "../Common/FrameTimer.h"
#include "../Common/GraphicsCommon.h"
#include "CommandQueue.h"
#include "DepthBuffer.h"
#include "DescriptorHeap.h"
#include "GraphicsDevice.h"
#include "SwapChain.h"
#include "TrianglePipeline.h"

#include <array>

namespace dx12
{

/// @brief これまでの部品を束ね、「1 フレーム描く」という仕事を担当するクラス。
///
/// **1 フレームの流れ（`Render` の中身）**
///
/// 1. このバックバッファを前回使ったときの GPU 処理の完了を待つ
/// 2. コマンドアロケータをリセット … そのフレーム用の記録メモリを再利用する
/// 3. コマンドリストをリセット … 記録を開始する
/// 4. リソースバリア（PRESENT → RENDER_TARGET）
/// 5. ビューポート / シザー矩形を設定
/// 6. レンダーターゲットを設定
/// 7. 画面をクリア
/// 8. 三角形の描画命令を記録
/// 9. リソースバリア（RENDER_TARGET → PRESENT）
/// 10. コマンドリストを Close
/// 11. コマンドキューへ投入 … ここで初めて GPU が動き始める
/// 12. Present … 画面に表示
/// 13. フェンス値を記録 … 「このフレームの完了印」を予約する
///
/// **コマンドアロケータ (ID3D12CommandAllocator) とは**
///
/// コマンドリストに記録した命令が実際に格納されるメモリの持ち主です。コマンドリストは「記録するため
/// のペン」、アロケータは「ノート」に相当します。
///
/// 重要な制約として、アロケータの `Reset()` は「そのノートに書かれた命令を GPU が実行し終えた後」で
/// なければ呼べません。実行中のノートを消しゴムで消すようなもので、GPU がクラッシュします。この制約
/// こそが、フレーム間の同期（フェンス）が必要な最大の理由です。
///
/// **フレームバッファリング（このクラスの中核）**
///
/// 素直に上の制約を満たすなら「毎フレーム末尾で GPU の完了を待つ」で済みます。しかしそれでは CPU と
/// GPU が交互にしか動けません。
///
/// ```
/// CPU: [記録]              [記録]              [記録]
/// GPU:        [実行][待機]        [実行][待機]        [実行]
/// ```
///
/// そこで「ノート（アロケータ）をバックバッファの枚数ぶん用意する」ことで、GPU がノート 0 を実行し
/// ている間に CPU がノート 1 へ書き込めるようにします。
///
/// ```
/// CPU: [記録0][記録1][記録2][記録3]...
/// GPU:        [実行0][実行1][実行2]...
///             (CPU が 1 フレーム先行できる)
/// ```
///
/// 待つのは「今から使おうとしているノートが空いたか」だけです。そのために、フレームごとに
/// `Signal()` したフェンス値を配列で覚えておき、同じ番号のノートを再び使うときにその値だけを待ちま
/// す。
class Renderer
{
public:
    /// @brief 既定のコンストラクタ。まだ何も生成されません。
    Renderer() = default;

    /// @brief デストラクタ。破棄の前に GPU の作業完了を待ちます。
    ///
    /// メンバ変数（ComPtr）が破棄される前に必ず GPU の完了を待ちます。GPU が使用中のリソースを解放する
    /// とクラッシュや警告の原因になるためです。
    ~Renderer();

    /// @brief コピー構築は禁止です。
    Renderer(const Renderer&) = delete;

    /// @brief コピー代入は禁止です。
    Renderer& operator=(const Renderer&) = delete;

    /// @brief DirectX 12 の初期化を一式行います。
    /// @param hwnd 描画先のウィンドウ。
    /// @param width 初期の描画解像度（幅、ピクセル）。
    /// @param height 初期の描画解像度（高さ、ピクセル）。
    /// @exception HrException DirectX の初期化に失敗した場合。
    /// @exception std::runtime_error シェーダーファイルが見つからない場合。
    void Initialize(HWND hwnd, uint32_t width, uint32_t height);

    /// @brief 1 フレーム描画して画面に表示します。
    /// @exception HrException コマンドの記録または投入に失敗した場合。
    void Render();

    /// @brief ウィンドウサイズ変更に追従します。
    /// @param width 新しい幅（ピクセル）。
    /// @param height 新しい高さ（ピクセル）。
    ///
    /// 内部で GPU の完了待ちを行ってからスワップチェーンを作り直します。初期化前に呼ばれた場合は何もし
    /// ません。
    void Resize(uint32_t width, uint32_t height);

    /// @brief GPU の全作業完了を待ちます（終了時に必ず呼ぶこと）。
    ///
    /// これを忘れると、GPU がまだ使用中のリソースを CPU 側が解放してしまい、終了時にクラッシュしたり、
    /// デバッグレイヤーが警告を出したりします。あわせて `m_frameFenceValues` を最新の完了値で埋めます。
    void WaitForGpu();

private:
    /// @brief コマンドアロケータ（バックバッファ枚数ぶん）とコマンドリストを生成します。
    /// @exception HrException 生成に失敗した場合。
    void CreateCommandObjects();

    /// @brief ビューポートとシザー矩形をウィンドウサイズに合わせて更新します。
    /// @param width 新しい幅（ピクセル）。
    /// @param height 新しい高さ（ピクセル）。
    void UpdateViewportAndScissor(uint32_t width, uint32_t height);

    /// @brief リソースの状態遷移バリアをコマンドリストに記録します。
    /// @param commandList 記録先のコマンドリスト。
    /// @param resource 状態を変えるリソース。
    /// @param stateBefore 現在の状態。実際の状態と一致している必要があります。
    /// @param stateAfter これから使う状態。
    ///
    /// **リソースバリア (Resource Barrier) とは**
    ///
    /// 「このリソースの用途を今から変えます」と GPU に宣言する命令です。
    ///
    /// GPU 内部では、同じメモリでも用途によって最適な保持形式が異なります（描画先として書き込むとき用に
    /// 圧縮された状態、テクスチャとして読むとき用の状態、画面表示用の状態…）。用途を切り替えるときは、
    /// この宣言によって「形式の変換」と「それ以前の処理の完了待ち」を GPU に行わせます。
    ///
    /// DirectX 11 ではドライバが自動でやっていました。DirectX 12 では開発者が明示する代わりに、不要なバ
    /// リアを省いて高速化できます。逆に言えば、書き忘れると描画結果が壊れる・クラッシュする原因になりま
    /// す。
    static void RecordResourceBarrier(ID3D12GraphicsCommandList* commandList,
                                      ID3D12Resource* resource,
                                      D3D12_RESOURCE_STATES stateBefore,
                                      D3D12_RESOURCE_STATES stateAfter);

private:
    /// @brief デバイス・DXGI ファクトリ。
    GraphicsDevice m_graphicsDevice;

    /// @brief コマンドキューとフェンス。
    CommandQueue m_commandQueue;

    /// @brief スワップチェーンとバックバッファ・RTV。
    SwapChain m_swapChain;

    /// @brief 奥行き判定に使う深度バッファと DSV。
    ///
    /// バックバッファと違い 1 枚しか作りません。フレームをまたいで内容を持ち越さず、
    /// 毎フレーム先頭でクリアするためです。コマンドキューは 1 本なので
    /// フレーム N の書き込みが終わってからフレーム N+1 のクリアが実行され、
    /// フレームバッファリングと併用しても競合しません。
    DepthBuffer m_depthBuffer;

    /// @brief 三角形描画用の PSO と頂点・定数バッファ、テクスチャ。
    TrianglePipeline m_trianglePipeline;

    /// @brief テクスチャなどをシェーダーへ渡すためのディスクリプタヒープ。
    ///
    /// `SHADER_VISIBLE` なヒープはコマンドリストに同時 1 本しか設定できないため、
    /// レンダラが 1 本だけ持ち、全リソースで共有します。
    DescriptorHeap m_descriptorHeap;

    /// @brief コマンドアロケータ（バックバッファの枚数ぶん用意する）。
    ///
    /// フレーム N では `m_commandAllocators[N % kBackBufferCount]` を使います。GPU がまだ [0] の命令を
    /// 実行中でも、CPU は [1] に記録を始められます。
    std::array<ComPtr<ID3D12CommandAllocator>, SwapChain::kBackBufferCount> m_commandAllocators;

    /// @brief 命令を記録するためのコマンドリスト。
    ///
    /// アロケータと違い 1 本で足ります。`Reset()` のたびに「どのアロケータへ書くか」を指定し直せるため
    /// です。複数スレッドから並列に記録する段階になったら本数を増やします。
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    /// @brief フレームごとのフェンス値（完了印）。
    ///
    /// `m_frameFenceValues[i]` は「アロケータ [i] を使ったフレームの処理が終わったときの番号」です。再
    /// び [i] を使う前にこの値を待てば、そのアロケータは安全に Reset できます。
    ///
    /// 初期値 0 は「まだ一度も使っていない ＝ 待つ必要なし」を意味します（フェンスの初期値も 0
    /// なので、`WaitForFenceValue(0)` は即座に戻ります）。
    std::array<uint64_t, SwapChain::kBackBufferCount> m_frameFenceValues = {};

    /// @brief フレーム時間の計測（1 秒ごとに FPS をログ出力する）。
    FrameTimer m_frameTimer;

    /// @brief バックバッファ上の「描画に使う矩形領域」と深度範囲。
    ///
    /// NDC 座標 (-1〜+1) を実際のピクセル座標へ変換する係数になります。
    D3D12_VIEWPORT m_viewport = {};

    /// @brief この矩形の外に出たピクセルを問答無用で捨てる設定。
    ///
    /// ビューポートと似ていますが役割が違います。
    ///
    /// - ビューポート … 座標変換（どこに描くか）
    /// - シザー矩形 … 切り抜き（どこまで描いてよいか）
    ///
    /// 設定を忘れると（＝全て 0 のままだと）1 ピクセルも描画されません。「画面が真っ黒で三角形が出ない」
    /// ときの定番の原因のひとつです。
    D3D12_RECT m_scissorRect = {};

    /// @brief 初期化が完了しているかどうか。
    bool m_initialized = false;
};

} // namespace dx12
