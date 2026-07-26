//=============================================================================
// CommandQueue.cpp
//   CommandQueue の実装。
//=============================================================================
#include "CommandQueue.h"

#include <format>

namespace dx12
{
//-----------------------------------------------------------------------------
// デストラクタ
//   ComPtr のメンバは自動解放されますが、HANDLE は COM ではないので
//   自分で CloseHandle する必要があります。
//   「自動で片付くもの」と「手で片付けるもの」を意識するのが大切です。
//-----------------------------------------------------------------------------
CommandQueue::~CommandQueue()
{
    if (m_fenceEvent != nullptr)
    {
        ::CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}


//-----------------------------------------------------------------------------
// Initialize
//-----------------------------------------------------------------------------
void CommandQueue::Initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
{
    //-------------------------------------------------------------------------
    // (1) コマンドキューの生成
    //-------------------------------------------------------------------------
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = type;

    // Priority : キューの優先度。通常は NORMAL でよい。
    //            HIGH は他アプリの描画より優先されるため、
    //            VR など遅延が致命的な用途以外では使いません。
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    // NodeMask : マルチ GPU 環境でどの GPU を使うかのビットマスク。
    //            GPU が 1 台なら 0（= 既定の GPU）。
    queueDesc.NodeMask = 0;

    DX_CHECK(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    //-------------------------------------------------------------------------
    // (2) フェンスの生成
    //   初期値 0 から開始する。以後 Signal のたびに 1, 2, 3 … と増えていく。
    //-------------------------------------------------------------------------
    DX_CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_nextFenceValue = 0;

    //-------------------------------------------------------------------------
    // (3) 待機用イベントの生成
    //   第 2 引数 FALSE : 自動リセットイベント
    //       → 待機が解除された瞬間に自動的に非シグナル状態へ戻る。
    //         毎回手動で ResetEvent を呼ぶ必要がなくなる。
    //   第 3 引数 FALSE : 初期状態は非シグナル（＝待たされる状態）
    //-------------------------------------------------------------------------
    m_fenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        throw std::runtime_error("フェンス待機用イベントの生成に失敗しました。");
    }

    Log(L"コマンドキューとフェンスを生成しました。");
}


//-----------------------------------------------------------------------------
// ExecuteCommandList : コマンドリストを GPU へ投入する
//-----------------------------------------------------------------------------
void CommandQueue::ExecuteCommandList(ID3D12GraphicsCommandList* commandList)
{
    // ExecuteCommandLists は「複数のコマンドリストをまとめて投入する」API。
    // 引数が配列なのはそのため。今回は 1 本だけなので要素 1 の配列にする。
    //
    // ID3D12GraphicsCommandList は ID3D12CommandList を継承しているため、
    // 基底型のポインタ配列として渡します。
    ID3D12CommandList* const commandLists[] = { commandList };

    m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
}


//-----------------------------------------------------------------------------
// Signal : 「ここまで終わったら番号を進めて」とキューに積む
//-----------------------------------------------------------------------------
uint64_t CommandQueue::Signal()
{
    // 次の番号を発行する（0 は初期値なので、最初の Signal は 1 になる）
    const uint64_t fenceValue = ++m_nextFenceValue;

    // 重要: この Signal はコマンドキューの「末尾に積まれる」だけで、
    //       この行を実行した瞬間にカウンタが増えるわけではありません。
    //       GPU がそれ以前に積まれた命令をすべて処理し終えた時点で、
    //       初めてフェンスのカウンタが fenceValue になります。
    DX_CHECK(m_commandQueue->Signal(m_fence.Get(), fenceValue));

    return fenceValue;
}


//-----------------------------------------------------------------------------
// WaitForFenceValue : 指定番号まで GPU が進むのを待つ
//-----------------------------------------------------------------------------
void CommandQueue::WaitForFenceValue(uint64_t fenceValue)
{
    // GetCompletedValue() は GPU が現在到達している番号を返す。
    // すでに追い越していれば待つ必要はない（この早期リターンが性能上とても重要）。
    if (m_fence->GetCompletedValue() >= fenceValue)
    {
        return;
    }

    // 「カウンタが fenceValue になったら、このイベントをシグナル状態にして」と予約する
    DX_CHECK(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent));

    // イベントがシグナルされるまでスレッドを眠らせる。
    // INFINITE = タイムアウトなし。GPU ハングすると永久に戻らない点は注意
    //（製品コードではタイムアウトを設けて復帰処理を書くこともあります）。
    ::WaitForSingleObject(m_fenceEvent, INFINITE);
}


//-----------------------------------------------------------------------------
// Flush : GPU の全作業完了を待つ
//-----------------------------------------------------------------------------
void CommandQueue::Flush()
{
    WaitForFenceValue(Signal());
}

} // namespace dx12
