//=============================================================================
// UploadHelper.cpp
//   UploadHelper の実装。
//=============================================================================
#include "UploadHelper.h"

#include "CommandQueue.h"

#include <cassert>
#include <cstring>

namespace dx12
{
namespace upload
{
namespace
{

/// <summary>バッファ用のリソース定義を組み立てます。</summary>
D3D12_RESOURCE_DESC MakeBufferDesc(uint64_t sizeInBytes)
{
    // バッファでは Height / DepthOrArraySize / MipLevels は 1、
    // Format は UNKNOWN、Layout は ROW_MAJOR が決まりごと。
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment          = 0;
    desc.Width              = sizeInBytes;
    desc.Height             = 1;
    desc.DepthOrArraySize   = 1;
    desc.MipLevels          = 1;
    desc.Format             = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags              = D3D12_RESOURCE_FLAG_NONE;
    return desc;
}

/// <summary>ヒープの種類だけを指定したヒーププロパティを作ります。</summary>
D3D12_HEAP_PROPERTIES MakeHeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type                 = type;
    properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask     = 1;
    properties.VisibleNodeMask      = 1;
    return properties;
}

} // namespace


/// <summary>
/// 使い捨てのコマンドリストへ転送コマンドを記録し、実行して完了まで待ちます。
/// </summary>
void ExecuteImmediate(ID3D12Device* device,
                      CommandQueue& commandQueue,
                      const std::function<void(ID3D12GraphicsCommandList*)>& record)
{
    assert(device != nullptr);

    // 転送専用に使い捨てのアロケータとリストを作る。
    // 描画用のものと混ぜないほうが責務がはっきりする。
    ComPtr<ID3D12CommandAllocator>    allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;

    DX_CHECK(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));

    // CreateCommandList 直後は「開いた」状態なので、そのまま記録できる。
    DX_CHECK(device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
        IID_PPV_ARGS(&commandList)));

    record(commandList.Get());

    DX_CHECK(commandList->Close());

    commandQueue.ExecuteCommandList(commandList.Get());

    // 呼び出し元が中継バッファを破棄できるよう、ここで完了を待つ。
    commandQueue.Flush();
}


/// <summary>UPLOAD ヒープに中継用のバッファを作ります。</summary>
ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, uint64_t sizeInBytes)
{
    assert(device != nullptr);
    assert(sizeInBytes > 0);

    const D3D12_HEAP_PROPERTIES heapProperties = MakeHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC   resourceDesc   = MakeBufferDesc(sizeInBytes);

    // UPLOAD ヒープのリソースは GENERIC_READ 状態で作ることが仕様で決まっており、
    // 以後この状態を変更することもできない。
    ComPtr<ID3D12Resource> buffer;
    DX_CHECK(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&buffer)));

    return buffer;
}


/// <summary>DEFAULT ヒープにバッファを作り、データを転送して返します。</summary>
ComPtr<ID3D12Resource> CreateBufferWithData(ID3D12Device* device,
                                            CommandQueue& commandQueue,
                                            const void* data,
                                            uint64_t sizeInBytes,
                                            D3D12_RESOURCE_STATES finalState)
{
    assert(device != nullptr);
    assert(data != nullptr);
    assert(sizeInBytes > 0);

    // (1) DEFAULT ヒープに本体を作る。CPU からは書けないので COPY_DEST で開始する。
    const D3D12_HEAP_PROPERTIES defaultHeap  = MakeHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_RESOURCE_DESC   resourceDesc = MakeBufferDesc(sizeInBytes);

    ComPtr<ID3D12Resource> buffer;
    DX_CHECK(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&buffer)));

    // (2) 中継バッファを作り、CPU から書き込む。
    //     テクスチャと違い行ピッチの整列が無いので、一度に memcpy できる。
    ComPtr<ID3D12Resource> uploadBuffer = CreateUploadBuffer(device, sizeInBytes);

    void*         mapped    = nullptr;
    D3D12_RANGE   readRange = { 0, 0 };   // CPU からは読まない（書くだけ）

    DX_CHECK(uploadBuffer->Map(0, &readRange, &mapped));
    std::memcpy(mapped, data, static_cast<size_t>(sizeInBytes));
    uploadBuffer->Unmap(0, nullptr);

    // (3) コピーと状態遷移を記録して実行し、完了を待つ。
    ExecuteImmediate(device, commandQueue, [&](ID3D12GraphicsCommandList* commandList) {
        commandList->CopyBufferRegion(buffer.Get(), 0, uploadBuffer.Get(), 0, sizeInBytes);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = buffer.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter  = finalState;

        commandList->ResourceBarrier(1, &barrier);
    });

    // ここを抜けると uploadBuffer は破棄されるが、
    // ExecuteImmediate が完了を待っているので安全。
    return buffer;
}

} // namespace upload
} // namespace dx12
