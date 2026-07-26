//=============================================================================
// ConstantBuffer.cpp
//   ConstantBuffer の実装。
//=============================================================================
#include "ConstantBuffer.h"

#include <cassert>
#include <cstring>
#include <format>

namespace dx12
{

/// <summary>デストラクタ。マップを解除します。</summary>
ConstantBuffer::~ConstantBuffer()
{
    if (m_resource != nullptr && m_mappedData != nullptr)
    {
        // 第 2 引数 nullptr は「書き込んだ範囲は全体」という意味。
        m_resource->Unmap(0, nullptr);
        m_mappedData = nullptr;
    }
}


/// <summary>フレーム数ぶんの領域を持つ定数バッファを生成します。</summary>
void ConstantBuffer::Initialize(ID3D12Device* device, uint32_t sizeInBytes, uint32_t frameCount)
{
    assert(device != nullptr);
    assert(sizeInBytes > 0);
    assert(frameCount > 0);

    // 1 フレームぶんのサイズを 256 バイト境界へ切り上げる。
    // 例) 64 バイトの行列 1 個 → 256 バイトを消費する。
    m_alignedSize = Align(sizeInBytes);
    m_frameCount  = frameCount;

    // フレーム数ぶんを 1 本のバッファにまとめて確保する
    const uint64_t totalSize = static_cast<uint64_t>(m_alignedSize) * frameCount;

    //-------------------------------------------------------------------------
    // UPLOAD ヒープ : CPU から書けて GPU から読める共有メモリ。
    //   毎フレーム書き換える定数バッファは、この用途のために存在します。
    //   （変化しないデータは DEFAULT ヒープの方が GPU から高速に読めます）
    //-------------------------------------------------------------------------
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                 = D3D12_HEAP_TYPE_UPLOAD;
    heapProperties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask     = 1;
    heapProperties.VisibleNodeMask      = 1;

    // バッファ（テクスチャではない生のバイト列）としてのリソース定義。
    // Height / DepthOrArraySize / MipLevels は 1、Format は UNKNOWN が決まりごと。
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment          = 0;
    resourceDesc.Width              = totalSize;
    resourceDesc.Height             = 1;
    resourceDesc.DepthOrArraySize   = 1;
    resourceDesc.MipLevels          = 1;
    resourceDesc.Format             = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count   = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    // UPLOAD ヒープのリソースは GENERIC_READ 状態で作ることが仕様で決まっており、
    // 以後この状態を変更することもできません（＝バリアが不要）。
    DX_CHECK(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_resource)));

    //-------------------------------------------------------------------------
    // 永続マップ
    //
    //   Map は「GPU 側のメモリを CPU のアドレス空間から見えるようにする」操作です。
    //   毎フレーム Map / Unmap を繰り返す必要はなく、生成時に一度だけ Map して
    //   ポインタを持ち続けるのが定石です（Unmap はデストラクタで行います）。
    //
    //   第 2 引数の D3D12_RANGE は「CPU が読む範囲」。
    //   Begin = End = 0 は「CPU からは一切読まない（書くだけ）」という意思表示で、
    //   ドライバがキャッシュの同期処理を省略できる最適化ヒントになります。
    //-------------------------------------------------------------------------
    D3D12_RANGE readRange = { 0, 0 };

    void* mapped = nullptr;
    DX_CHECK(m_resource->Map(0, &readRange, &mapped));
    m_mappedData = static_cast<uint8_t*>(mapped);

    Log(std::format(L"定数バッファを作成しました（{} バイト × {} フレーム = {} バイト）",
                    m_alignedSize, frameCount, totalSize));
}


/// <summary>指定フレームの領域へデータを書き込みます。</summary>
void ConstantBuffer::Update(uint32_t frameIndex, const void* data, uint32_t sizeInBytes)
{
    assert(m_mappedData != nullptr);
    assert(frameIndex < m_frameCount);
    assert(sizeInBytes <= m_alignedSize);

    // フレーム番号ぶんだけアドレスをずらした位置へ書き込む。
    // GPU が別フレームの領域を読んでいても干渉しない。
    std::memcpy(m_mappedData + static_cast<size_t>(frameIndex) * m_alignedSize,
                data,
                sizeInBytes);
}


/// <summary>指定フレームの領域の GPU アドレスを取得します。</summary>
D3D12_GPU_VIRTUAL_ADDRESS ConstantBuffer::GpuAddress(uint32_t frameIndex) const
{
    assert(m_resource != nullptr);
    assert(frameIndex < m_frameCount);

    // GetGPUVirtualAddress() は「GPU から見たアドレス」を返す。
    // CPU 側のポインタ（m_mappedData）とは別物である点に注意。
    // 256 バイト境界に揃えてあるので、この加算後も境界条件を満たす。
    return m_resource->GetGPUVirtualAddress()
         + static_cast<uint64_t>(frameIndex) * m_alignedSize;
}

} // namespace dx12
