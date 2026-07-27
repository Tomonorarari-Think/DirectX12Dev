//=============================================================================
// DescriptorHeap.cpp
//   DescriptorHeap の実装。
//=============================================================================
#include "DescriptorHeap.h"

#include <cassert>
#include <format>

namespace dx12
{

/// <summary>
/// シェーダーから参照できる CBV/SRV/UAV 用ヒープを生成します。
/// </summary>
void DescriptorHeap::Initialize(ID3D12Device* device, uint32_t capacity)
{
    assert(device != nullptr);
    assert(capacity > 0);

    m_capacity       = capacity;
    m_allocatedCount = 0;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};

    // Type : CBV / SRV / UAV は「同じ種類」として 1 本のヒープにまとめて置けます。
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = capacity;

    // ★ SHADER_VISIBLE
    //   GPU 自身がこのヒープを読みに行けるようにするフラグです。
    desc.Flags    = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;

    DX_CHECK(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap)));

    // ディスクリプタ 1 個のサイズは GPU ごとに異なるため、実行時に取得する。
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    Log(std::format(L"シェーダー可視ディスクリプタヒープを生成しました（{} 個ぶん）", capacity));
}


/// <summary>
/// ディスクリプタを 1 個ぶん確保し、その番号を返します。
/// </summary>
uint32_t DescriptorHeap::Allocate()
{
    if (m_allocatedCount >= m_capacity)
    {
        // 学習用に固定容量で作っているため、超えたら明確に落とす。
        throw std::runtime_error("ディスクリプタヒープの容量を超えました。capacity を増やしてください。");
    }

    return m_allocatedCount++;
}


/// <summary>
/// 指定番号の CPU ディスクリプタハンドルを取得します。
/// </summary>
D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::CpuHandle(uint32_t index) const
{
    assert(index < m_allocatedCount);

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();

    // 先頭 + サイズ × 番号。RTV ヒープのときと同じ計算方法。
    handle.ptr += static_cast<SIZE_T>(index) * m_descriptorSize;

    return handle;
}


/// <summary>
/// 指定番号の GPU ディスクリプタハンドルを取得します。
/// </summary>
D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::GpuHandle(uint32_t index) const
{
    assert(index < m_allocatedCount);

    // GPU ハンドルは SHADER_VISIBLE なヒープでのみ意味を持つ。
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_heap->GetGPUDescriptorHandleForHeapStart();

    handle.ptr += static_cast<UINT64>(index) * m_descriptorSize;

    return handle;
}

} // namespace dx12
