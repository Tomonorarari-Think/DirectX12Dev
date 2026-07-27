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
class CommandQueue
{
public:
    /// <summary>
    /// 既定のコンストラクタ。まだ何も生成されません。
    /// </summary>
    CommandQueue() = default;

    /// <summary>
    /// デストラクタ。待機用イベントハンドルを閉じます。
    /// </summary>
    ~CommandQueue();

    /// <summary>
    /// コピー構築は禁止です。
    /// </summary>
    CommandQueue(const CommandQueue&) = delete;

    /// <summary>
    /// コピー代入は禁止です。
    /// </summary>
    CommandQueue& operator=(const CommandQueue&) = delete;

    /// <summary>
    /// コマンドキュー、フェンス、待機用イベントを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="type">キューの種類。既定は万能な DIRECT。</param>
    /// <exception cref="HrException">キューまたはフェンスの生成に失敗した場合。</exception>
    /// <exception cref="std::runtime_error">待機用イベントの生成に失敗した場合。</exception>
    void Initialize(ID3D12Device* device,
                    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);

    /// <summary>
    /// 記録済みのコマンドリストを GPU の実行待ち行列へ投入します。
    /// </summary>
    /// <param name="commandList">Close 済みのコマンドリスト。</param>
    void ExecuteCommandList(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// 「ここまでの仕事が終わったら番号を進めて」という指示をキューに積みます。
    /// </summary>
    /// <returns>積んだ番号。この値を `WaitForFenceValue` に渡します。</returns>
    /// <exception cref="HrException">Signal の発行に失敗した場合。</exception>
    uint64_t Signal();

    /// <summary>
    /// 指定した番号まで GPU が到達するのを CPU 側で待ちます。
    /// </summary>
    /// <param name="fenceValue">`Signal` が返した番号。</param>
    /// <exception cref="HrException">完了通知の予約に失敗した場合。</exception>
    void WaitForFenceValue(uint64_t fenceValue);

    /// <summary>
    /// GPU の作業が「全部」終わるまで待ちます。
    /// </summary>
    /// <returns>
    /// 待機に使ったフェンス値。呼び出し側が「全フレームがこの値まで完了した」と記録するのに使
    /// います。
    /// </returns>
    uint64_t Flush();

    /// <summary>
    /// 生成済みのコマンドキューを取得します。
    /// </summary>
    /// <returns>キューの生ポインタ（所有権は移動しません）。</returns>
    ID3D12CommandQueue* Get() const noexcept { return m_commandQueue.Get(); }

private:
    /// <summary>
    /// GPU への命令の投入口。
    /// </summary>
    ComPtr<ID3D12CommandQueue> m_commandQueue;

    /// <summary>
    /// GPU と共有するカウンタ本体。
    /// </summary>
    ComPtr<ID3D12Fence> m_fence;

    /// <summary>
    /// CPU 側が「次に発行する番号」を覚えておく変数。
    /// </summary>
    uint64_t m_nextFenceValue = 0;

    /// <summary>
    /// GPU の完了を待つための Windows イベントオブジェクト。
    /// </summary>
    HANDLE m_fenceEvent = nullptr;
};

} // namespace dx12
