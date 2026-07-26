//=============================================================================
// CommandQueue.h
//   GPU への命令の投入と、CPU / GPU の同期（フェンス）。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// @brief GPU へ命令を送るコマンドキューと、CPU / GPU の同期を担当するクラス。
///
/// **DirectX 12 で最初に理解すべき「非同期」の話**
///
/// DirectX 12 では、C++ のコードが GPU に対して命令を「直接」実行させることはできません。次の 3 段
/// 階を踏みます。
///
/// 1. コマンドリスト (`ID3D12GraphicsCommandList`) に命令を「記録」する。例）「この色でクリアして」
///    「この三角形を描いて」。この時点では GPU は何もしていません。ただのメモ書きです。
/// 2. コマンドキュー (`ID3D12CommandQueue`) に「投入」する。`ExecuteCommandLists` で GPU の実行待ち
///    行列に並べます。この関数は GPU の完了を待たずに即座に戻ります（非同期）。
/// 3. GPU が自分のペースで実行する。CPU は次のフレームの準備を並行して進められます。これが DirectX
///    12 が高速な理由です。
///
/// **そこで問題になること**
///
/// CPU が「GPU がまだ読んでいるメモリ」を書き換えてしまうと、描画結果が壊れます。かといって毎回待っ
/// ていては並列化の意味がありません。この「いつ待つか」を制御するのがフェンス (Fence) です。
///
/// **フェンス (ID3D12Fence) の仕組み**
///
/// フェンスは GPU と CPU が共有する「単なる 64bit のカウンタ」です。
///
/// - CPU が `Signal(値 N)` をキューに積む → GPU がそこまでの命令を実行し終えた瞬間、カウンタが N に
///   なる
/// - CPU は `fence->GetCompletedValue()` でカウンタを読む→ N 以上なら「N までの仕事は完了済み」と判
///   断できる
///
/// つまり「番号札」です。GPU の作業に番号を振り、「◯番まで終わった？」と CPU が確認できるようにする
/// 仕組みです。
class CommandQueue
{
public:
    /// @brief 既定のコンストラクタ。まだ何も生成されません。
    CommandQueue() = default;

    /// @brief デストラクタ。待機用イベントハンドルを閉じます。
    ///
    /// ComPtr のメンバは自動解放されますが、`HANDLE` は COM ではないので自分で `CloseHandle` する必要が
    /// あります。「自動で片付くもの」と「手で片付けるもの」を意識するのが大切です。
    ~CommandQueue();

    /// @brief コピー構築は禁止です。
    CommandQueue(const CommandQueue&) = delete;

    /// @brief コピー代入は禁止です。
    CommandQueue& operator=(const CommandQueue&) = delete;

    /// @brief コマンドキュー、フェンス、待機用イベントを生成します。
    /// @param device 生成に使う D3D12 デバイス。
    /// @param type キューの種類。既定は万能な DIRECT。
    /// @exception HrException キューまたはフェンスの生成に失敗した場合。
    /// @exception std::runtime_error 待機用イベントの生成に失敗した場合。
    ///
    /// **キューの種類**
    ///
    /// - **DIRECT** : 何でもできる万能キュー（描画・計算・コピー）
    /// - **COMPUTE** : 計算シェーダー専用
    /// - **COPY** : リソース転送専用
    ///
    /// COMPUTE / COPY は GPU 内の専用ハードウェアを使うため、DIRECT と並列に動かせて高速化できます（今
    /// 回は DIRECT のみ使用）。
    void Initialize(ID3D12Device* device,
                    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);

    /// @brief 記録済みのコマンドリストを GPU の実行待ち行列へ投入します。
    /// @param commandList Close 済みのコマンドリスト。
    ///
    /// この関数は GPU の完了を待たず、すぐに戻ります。
    void ExecuteCommandList(ID3D12GraphicsCommandList* commandList);

    /// @brief 「ここまでの仕事が終わったら番号を進めて」という指示をキューに積みます。
    /// @returns 積んだ番号。この値を `WaitForFenceValue` に渡します。
    /// @exception HrException Signal の発行に失敗した場合。
    ///
    /// この関数はコマンドキューの末尾に指示を積むだけで、呼んだ瞬間にカウンタが増えるわけではありません。
    uint64_t Signal();

    /// @brief 指定した番号まで GPU が到達するのを CPU 側で待ちます。
    /// @param fenceValue `Signal` が返した番号。
    /// @exception HrException 完了通知の予約に失敗した場合。
    ///
    /// すでに到達済みなら即座に戻ります（無駄な待ちは発生しません）。
    void WaitForFenceValue(uint64_t fenceValue);

    /// @brief GPU の作業が「全部」終わるまで待ちます。
    /// @returns 待機に使ったフェンス値。呼び出し側が「全フレームがこの値まで完了した」と記録するのに使
    ///     います。
    ///
    /// 使いどころ:
    ///
    /// - アプリ終了時（GPU が使用中のリソースを解放しないため。必須）
    /// - スワップチェーンのサイズ変更前（バックバッファを差し替えるため。必須）
    ///
    /// 毎フレーム呼ぶと CPU と GPU が完全に直列化し、性能が大きく落ちます。そのため通常の描画ループでは
    /// 使いません。描画ループでは `Signal` と `WaitForFenceValue` を個別に使い、「必要な分だけ待つ」よ
    /// うにします（フレームバッファリング）。
    uint64_t Flush();

    /// @brief 生成済みのコマンドキューを取得します。
    /// @returns キューの生ポインタ（所有権は移動しません）。
    ID3D12CommandQueue* Get() const noexcept { return m_commandQueue.Get(); }

private:
    /// @brief GPU への命令の投入口。
    ComPtr<ID3D12CommandQueue> m_commandQueue;

    /// @brief GPU と共有するカウンタ本体。
    ComPtr<ID3D12Fence> m_fence;

    /// @brief CPU 側が「次に発行する番号」を覚えておく変数。
    ///
    /// フェンス本体のカウンタ（GPU が進める）とは別物であることに注意してください。
    uint64_t m_nextFenceValue = 0;

    /// @brief GPU の完了を待つための Windows イベントオブジェクト。
    ///
    /// GPU の完了を待つ方法は 2 通りあります。
    ///
    /// - ビジーループ（`while (fence->GetCompletedValue() < value) {}`）…CPU コアを 100% 使い切って待つ
    ///   ため、電力も性能も無駄。
    /// - イベントで待つ（本実装）… OS にスレッドを寝かせてもらい、完了時に起こしてもらう。待っている間、
    ///   CPU を他の処理に譲れる。
    HANDLE m_fenceEvent = nullptr;
};

} // namespace dx12
