//=============================================================================
// CommandQueue.h
//   GPU への命令の投入と、CPU / GPU の同期（フェンス）。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// <summary>
/// GPU へ命令を送るコマンドキューと、CPU / GPU の同期を担当するクラス。
/// </summary>
/// <remarks>
/// <para>
/// <b>DirectX 12 で最初に理解すべき「非同期」の話</b><br/>
/// DirectX 12 では、C++ のコードが GPU に対して命令を「直接」実行させる
/// ことはできません。次の 3 段階を踏みます。
/// <list type="number">
///   <item>
///     コマンドリスト (<c>ID3D12GraphicsCommandList</c>) に命令を「記録」する。
///     例）「この色でクリアして」「この三角形を描いて」。
///     この時点では GPU は何もしていません。ただのメモ書きです。
///   </item>
///   <item>
///     コマンドキュー (<c>ID3D12CommandQueue</c>) に「投入」する。
///     <c>ExecuteCommandLists</c> で GPU の実行待ち行列に並べます。
///     この関数は GPU の完了を待たずに即座に戻ります（非同期）。
///   </item>
///   <item>
///     GPU が自分のペースで実行する。CPU は次のフレームの準備を並行して
///     進められます。これが DirectX 12 が高速な理由です。
///   </item>
/// </list>
/// </para>
/// <para>
/// <b>そこで問題になること</b><br/>
/// CPU が「GPU がまだ読んでいるメモリ」を書き換えてしまうと、描画結果が壊れます。
/// かといって毎回待っていては並列化の意味がありません。
/// この「いつ待つか」を制御するのがフェンス (Fence) です。
/// </para>
/// <para>
/// <b>フェンス (ID3D12Fence) の仕組み</b><br/>
/// フェンスは GPU と CPU が共有する「単なる 64bit のカウンタ」です。
/// <list type="bullet">
///   <item>CPU が <c>Signal(値 N)</c> をキューに積む → GPU がそこまでの命令を
///         実行し終えた瞬間、カウンタが N になる</item>
///   <item>CPU は <c>fence-&gt;GetCompletedValue()</c> でカウンタを読む
///         → N 以上なら「N までの仕事は完了済み」と判断できる</item>
/// </list>
/// つまり「番号札」です。GPU の作業に番号を振り、
/// 「◯番まで終わった？」と CPU が確認できるようにする仕組みです。
/// </para>
/// </remarks>
class CommandQueue
{
public:
    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    CommandQueue() = default;

    /// <summary>デストラクタ。待機用イベントハンドルを閉じます。</summary>
    /// <remarks>
    /// ComPtr のメンバは自動解放されますが、<c>HANDLE</c> は COM ではないので
    /// 自分で <c>CloseHandle</c> する必要があります。
    /// 「自動で片付くもの」と「手で片付けるもの」を意識するのが大切です。
    /// </remarks>
    ~CommandQueue();

    /// <summary>コピー構築は禁止です。</summary>
    CommandQueue(const CommandQueue&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    CommandQueue& operator=(const CommandQueue&) = delete;

    /// <summary>コマンドキュー、フェンス、待機用イベントを生成します。</summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="type">キューの種類。既定は万能な DIRECT。</param>
    /// <exception cref="HrException">キューまたはフェンスの生成に失敗した場合。</exception>
    /// <exception cref="std::runtime_error">待機用イベントの生成に失敗した場合。</exception>
    /// <remarks>
    /// <b>キューの種類</b>
    /// <list type="table">
    ///   <item><term>DIRECT</term><description>何でもできる万能キュー（描画・計算・コピー）</description></item>
    ///   <item><term>COMPUTE</term><description>計算シェーダー専用</description></item>
    ///   <item><term>COPY</term><description>リソース転送専用</description></item>
    /// </list>
    /// COMPUTE / COPY は GPU 内の専用ハードウェアを使うため、
    /// DIRECT と並列に動かせて高速化できます（今回は DIRECT のみ使用）。
    /// </remarks>
    void Initialize(ID3D12Device* device,
                    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);

    /// <summary>記録済みのコマンドリストを GPU の実行待ち行列へ投入します。</summary>
    /// <param name="commandList">Close 済みのコマンドリスト。</param>
    /// <remarks>この関数は GPU の完了を待たず、すぐに戻ります。</remarks>
    void ExecuteCommandList(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// 「ここまでの仕事が終わったら番号を進めて」という指示をキューに積みます。
    /// </summary>
    /// <returns>積んだ番号。この値を <see cref="WaitForFenceValue"/> に渡します。</returns>
    /// <exception cref="HrException">Signal の発行に失敗した場合。</exception>
    /// <remarks>
    /// この関数はコマンドキューの末尾に指示を積むだけで、
    /// 呼んだ瞬間にカウンタが増えるわけではありません。
    /// </remarks>
    uint64_t Signal();

    /// <summary>指定した番号まで GPU が到達するのを CPU 側で待ちます。</summary>
    /// <param name="fenceValue"><see cref="Signal"/> が返した番号。</param>
    /// <exception cref="HrException">完了通知の予約に失敗した場合。</exception>
    /// <remarks>すでに到達済みなら即座に戻ります（無駄な待ちは発生しません）。</remarks>
    void WaitForFenceValue(uint64_t fenceValue);

    /// <summary>GPU の作業が「全部」終わるまで待ちます。</summary>
    /// <returns>
    /// 待機に使ったフェンス値。
    /// 呼び出し側が「全フレームがこの値まで完了した」と記録するのに使います。
    /// </returns>
    /// <remarks>
    /// <para>
    /// 使いどころ:
    /// <list type="bullet">
    ///   <item>アプリ終了時（GPU が使用中のリソースを解放しないため。必須）</item>
    ///   <item>スワップチェーンのサイズ変更前（バックバッファを差し替えるため。必須）</item>
    /// </list>
    /// </para>
    /// <para>
    /// 毎フレーム呼ぶと CPU と GPU が完全に直列化し、性能が大きく落ちます。
    /// そのため通常の描画ループでは使いません。
    /// 描画ループでは <see cref="Signal"/> と <see cref="WaitForFenceValue"/> を
    /// 個別に使い、「必要な分だけ待つ」ようにします（フレームバッファリング）。
    /// </para>
    /// </remarks>
    uint64_t Flush();

    /// <summary>生成済みのコマンドキューを取得します。</summary>
    /// <returns>キューの生ポインタ（所有権は移動しません）。</returns>
    ID3D12CommandQueue* Get() const noexcept { return m_commandQueue.Get(); }

private:
    /// <summary>GPU への命令の投入口。</summary>
    ComPtr<ID3D12CommandQueue> m_commandQueue;

    /// <summary>GPU と共有するカウンタ本体。</summary>
    ComPtr<ID3D12Fence> m_fence;

    /// <summary>
    /// CPU 側が「次に発行する番号」を覚えておく変数。
    /// </summary>
    /// <remarks>
    /// フェンス本体のカウンタ（GPU が進める）とは別物であることに注意してください。
    /// </remarks>
    uint64_t m_nextFenceValue = 0;

    /// <summary>
    /// GPU の完了を待つための Windows イベントオブジェクト。
    /// </summary>
    /// <remarks>
    /// GPU の完了を待つ方法は 2 通りあります。
    /// <list type="bullet">
    ///   <item>
    ///     ビジーループ（<c>while (fence-&gt;GetCompletedValue() &lt; value) {}</c>）…
    ///     CPU コアを 100% 使い切って待つため、電力も性能も無駄。
    ///   </item>
    ///   <item>
    ///     イベントで待つ（本実装）… OS にスレッドを寝かせてもらい、完了時に
    ///     起こしてもらう。待っている間、CPU を他の処理に譲れる。
    ///   </item>
    /// </list>
    /// </remarks>
    HANDLE m_fenceEvent = nullptr;
};

} // namespace dx12
