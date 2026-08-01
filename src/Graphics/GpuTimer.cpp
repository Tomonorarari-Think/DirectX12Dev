//=============================================================================
// GpuTimer.cpp
//   GpuTimer の実装。
//=============================================================================
#include "GpuTimer.h"

#include <format>

namespace dx12
{

/// <summary>
/// 区間の表示名。
/// </summary>
const std::array<const wchar_t*, GpuTimer::kPassCount> GpuTimer::kPassNames = {
    L"粒子更新",
    L"影",
    L"シーン",
    L"背景",
    L"半透明",
    L"露出",
    L"後処理",
    L"全体",
};


/// <summary>
/// クエリヒープと読み出し用バッファを生成します。
/// </summary>
void GpuTimer::Initialize(ID3D12Device* device,
                          ID3D12CommandQueue* commandQueue,
                          uint32_t frameCount)
{
    m_frameCount = frameCount;

    // --- (1) 1 秒あたりの刻み数 ----------------------------------------------
    //   ★ タイムスタンプの単位はキューごとに違う。ここで聞いておく。
    //     この値で割って初めて秒になる。
    const HRESULT hr = commandQueue->GetTimestampFrequency(&m_ticksPerSecond);

    if (FAILED(hr) || m_ticksPerSecond == 0)
    {
        // タイムスタンプに対応していない環境では、機能ごと諦める。
        LogError(L"タイムスタンプを取得できませんでした。GPU の計測を無効にします。");
        return;
    }

    // --- (2) クエリヒープ -----------------------------------------------------
    //   ディスクリプタヒープとは別物。クエリの結果を溜める専用の入れ物。
    const uint32_t totalQueries = kQueriesPerFrame * frameCount;

    D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
    queryHeapDesc.Type     = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryHeapDesc.Count    = totalQueries;
    queryHeapDesc.NodeMask = 0;

    DX_CHECK(device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_queryHeap)));

    // --- (3) 読み出し用バッファ -----------------------------------------------
    //   ★ READBACK ヒープは「GPU が書き、CPU が読む」ための場所。
    //     UPLOAD ヒープ（CPU が書き、GPU が読む）のちょうど逆。
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                 = D3D12_HEAP_TYPE_READBACK;
    heapProperties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask     = 1;
    heapProperties.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width            = sizeof(uint64_t) * totalQueries;
    bufferDesc.Height           = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels        = 1;
    bufferDesc.Format           = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // ★ READBACK ヒープの初期状態は COPY_DEST 固定。以後この状態のまま。
    DX_CHECK(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_readbackBuffer)));

    m_readbackBuffer->SetName(L"GPU タイムスタンプの読み出し");

    Log(std::format(L"GPU の計測を有効にしました（{} 刻み / 秒）。", m_ticksPerSecond));
}


/// <summary>
/// 指定した区間のクエリ番号（開始側）を返します。
/// </summary>
uint32_t GpuTimer::QueryIndex(uint32_t frameIndex, GpuPass pass) const
{
    return frameIndex * kQueriesPerFrame
         + static_cast<uint32_t>(pass) * kQueriesPerPass;
}


/// <summary>
/// 区間の開始を記録します。
/// </summary>
void GpuTimer::Begin(ID3D12GraphicsCommandList* commandList,
                     uint32_t frameIndex,
                     GpuPass pass)
{
    if (!IsAvailable())
    {
        return;
    }

    // ★ タイムスタンプに BeginQuery は無い。開始も終了も EndQuery で打つ。
    //   「その時点の時刻を書き込め」という 1 回きりの命令だからです。
    commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          QueryIndex(frameIndex, pass));
}


/// <summary>
/// 区間の終了を記録します。
/// </summary>
void GpuTimer::End(ID3D12GraphicsCommandList* commandList,
                   uint32_t frameIndex,
                   GpuPass pass)
{
    if (!IsAvailable())
    {
        return;
    }

    commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          QueryIndex(frameIndex, pass) + 1);
}


/// <summary>
/// 測った値を読み出し用バッファへ書き出す命令を記録します。
/// </summary>
void GpuTimer::Resolve(ID3D12GraphicsCommandList* commandList, uint32_t frameIndex)
{
    if (!IsAvailable())
    {
        return;
    }

    const uint32_t startIndex = frameIndex * kQueriesPerFrame;

    commandList->ResolveQueryData(
        m_queryHeap.Get(),
        D3D12_QUERY_TYPE_TIMESTAMP,
        startIndex,
        kQueriesPerFrame,
        m_readbackBuffer.Get(),
        sizeof(uint64_t) * startIndex);
}


/// <summary>
/// 前回このフレーム番号で測った結果を読み取ります。
/// </summary>
void GpuTimer::Collect(uint32_t frameIndex)
{
    if (!IsAvailable())
    {
        return;
    }

    const size_t begin = sizeof(uint64_t) * frameIndex * kQueriesPerFrame;
    const size_t end   = begin + sizeof(uint64_t) * kQueriesPerFrame;

    // ★ 読む範囲を渡す。範囲外を読まないと約束することで、
    //   ドライバが余計な同期やコピーを省ける。
    const D3D12_RANGE readRange  = { begin, end };
    const D3D12_RANGE writeRange = { 0, 0 };   // CPU からは何も書かない

    void* mapped = nullptr;
    if (FAILED(m_readbackBuffer->Map(0, &readRange, &mapped)))
    {
        return;
    }

    const uint64_t* timestamps = static_cast<const uint64_t*>(mapped);

    for (uint32_t i = 0; i < kPassCount; ++i)
    {
        const uint32_t index = frameIndex * kQueriesPerFrame + i * kQueriesPerPass;

        const uint64_t startTick = timestamps[index];
        const uint64_t endTick   = timestamps[index + 1];

        // 1 度も打っていない区間は 0 のまま。引き算が破綻しないよう弾く。
        if (endTick <= startTick)
        {
            m_milliseconds[i] = 0.0;
            continue;
        }

        const double ticks = static_cast<double>(endTick - startTick);

        m_milliseconds[i] = ticks * 1000.0 / static_cast<double>(m_ticksPerSecond);
    }

    m_readbackBuffer->Unmap(0, &writeRange);
}


/// <summary>
/// 直近に読み取った区間の時間をミリ秒で返します。
/// </summary>
double GpuTimer::Milliseconds(GpuPass pass) const
{
    const uint32_t index = static_cast<uint32_t>(pass);

    return (index < kPassCount) ? m_milliseconds[index] : 0.0;
}


/// <summary>
/// 全区間の結果を 1 行の文字列にまとめます。
/// </summary>
std::wstring GpuTimer::Format() const
{
    if (!IsAvailable())
    {
        return L"GPU: 計測できません";
    }

    std::wstring text = L"GPU:";

    // 全体は最後に別枠で出す。個々のパスを先に並べる。
    for (uint32_t i = 0; i + 1 < kPassCount; ++i)
    {
        if (m_milliseconds[i] <= 0.0)
        {
            continue;   // 動いていないパスは出さない
        }

        text += std::format(L" {} {:.3f}", kPassNames[i], m_milliseconds[i]);
    }

    const double total = Milliseconds(GpuPass::Frame);

    text += std::format(L" | 全体 {:.3f} ms", total);

    return text;
}

} // namespace dx12
