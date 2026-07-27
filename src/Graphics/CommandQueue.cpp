//=============================================================================
// CommandQueue.cpp
//   CommandQueue の実装。
//=============================================================================
#include "CommandQueue.h"

#include <format>

namespace dx12
{
/// <summary>
/// デストラクタ。待機用イベントハンドルを閉じます。
/// </summary>
CommandQueue::~CommandQueue()
{
    if (m_fenceEvent != nullptr)
    {
        ::CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}


/// <summary>
/// コマンドキュー、フェンス、待機用イベントを生成します。
/// </summary>
void CommandQueue::Initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
{
    // (1) コマンドキューの生成
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = type;

    // Priority : キューの優先度。通常は NORMAL でよい。
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    // NodeMask : マルチ GPU 環境でどの GPU を使うかのビットマスク。
    queueDesc.NodeMask = 0;

    DX_CHECK(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // (2) フェンスの生成
    //   初期値 0 から開始する。以後 Signal のたびに 1, 2, 3 … と増えていく。
    DX_CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_nextFenceValue = 0;

    // (3) 待機用イベントの生成
    //   第 2 引数 FALSE : 自動リセットイベント
    //       → 待機が解除された瞬間に自動的に非シグナル状態へ戻る。
    m_fenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        throw std::runtime_error("フェンス待機用イベントの生成に失敗しました。");
    }

    Log(L"コマンドキューとフェンスを生成しました。");
}


/// <summary>
/// 記録済みのコマンドリストを GPU の実行待ち行列へ投入します。
/// </summary>
void CommandQueue::ExecuteCommandList(ID3D12GraphicsCommandList* commandList)
{
    // ExecuteCommandLists は「複数のコマンドリストをまとめて投入する」API。
    ID3D12CommandList* const commandLists[] = { commandList };

    m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
}


/// <summary>
/// 「ここまで終わったら番号を進めて」という指示をキューに積みます。
/// </summary>
uint64_t CommandQueue::Signal()
{
    // 次の番号を発行する（0 は初期値なので、最初の Signal は 1 になる）
    const uint64_t fenceValue = ++m_nextFenceValue;

    // 重要: この Signal はコマンドキューの「末尾に積まれる」だけで、
    //       この行を実行した瞬間にカウンタが増えるわけではありません。
    DX_CHECK(m_commandQueue->Signal(m_fence.Get(), fenceValue));

    return fenceValue;
}


/// <summary>
/// 指定した番号まで GPU が到達するのを CPU 側で待ちます。
/// </summary>
void CommandQueue::WaitForFenceValue(uint64_t fenceValue)
{
    // GetCompletedValue() は GPU が現在到達している番号を返す。
    if (m_fence->GetCompletedValue() >= fenceValue)
    {
        return;
    }

    // 「カウンタが fenceValue になったら、このイベントをシグナル状態にして」と予約する
    DX_CHECK(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent));

    // イベントがシグナルされるまでスレッドを眠らせる。
    ::WaitForSingleObject(m_fenceEvent, INFINITE);
}


/// <summary>
/// GPU の作業が「全部」終わるまで待ちます。
/// </summary>
uint64_t CommandQueue::Flush()
{
    const uint64_t fenceValue = Signal();
    WaitForFenceValue(fenceValue);

    // 呼び出し側が「この値まで全て完了済み」と記録できるよう返す。
    return fenceValue;
}

} // namespace dx12
