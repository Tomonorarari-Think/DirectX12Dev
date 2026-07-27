//=============================================================================
// Texture2D.cpp
//   Texture2D の実装。DEFAULT ヒープへのステージング転送を行う。
//=============================================================================
#include "Texture2D.h"

#include "CommandQueue.h"
#include "DescriptorHeap.h"

#include <cassert>
#include <cstring>
#include <format>

namespace dx12
{

/// <summary>
/// ピクセルデータから GPU 上にテクスチャを作り、SRV を登録します。
/// </summary>
void Texture2D::Initialize(ID3D12Device* device,
                           CommandQueue& commandQueue,
                           DescriptorHeap& descriptorHeap,
                           uint32_t width,
                           uint32_t height,
                           const std::vector<uint8_t>& pixels)
{
    assert(device != nullptr);
    assert(width > 0 && height > 0);
    assert(pixels.size() == static_cast<size_t>(width) * height * kBytesPerPixel);

    m_width  = width;
    m_height = height;

    // (1) DEFAULT ヒープにテクスチャ本体を作る
    //   GPU 専用の高速メモリ。CPU からは直接書き込めない。
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeap.CreationNodeMask     = 1;
    defaultHeap.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Alignment          = 0;
    textureDesc.Width              = width;
    textureDesc.Height             = height;
    textureDesc.DepthOrArraySize   = 1;

    // MipLevels : 縮小版を何段作るか。1 は「原寸のみ」。
    textureDesc.MipLevels          = 1;

    textureDesc.Format             = kFormat;
    textureDesc.SampleDesc.Count   = 1;
    textureDesc.SampleDesc.Quality = 0;

    // テクスチャの内部配置は GPU に最適な形へ任せる。
    textureDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    DX_CHECK(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_texture)));

    // (2) 中継バッファに必要なサイズと配置を GPU に問い合わせる
    //   ★ 自分で「幅 × 4 × 高さ」と計算してはいけません。
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT   numRows        = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes     = 0;

    device->GetCopyableFootprints(
        &textureDesc,
        0,              // 先頭のサブリソース番号（ミップ 0）
        1,              // サブリソースの個数
        0,              // 中継バッファ内のオフセット
        &footprint,
        &numRows,
        &rowSizeInBytes,
        &totalBytes);

    Log(std::format(L"テクスチャ転送: {} x {}, 実データ {} B/行 → 中継バッファ {} B/行（256 境界に整列）",
                    width, height, rowSizeInBytes, footprint.Footprint.RowPitch));

    // (3) UPLOAD ヒープに中継バッファを作る
    //   ローカル変数にしているのは、転送が終われば不要になるためです。
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type                 = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeap.CreationNodeMask     = 1;
    uploadHeap.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Alignment          = 0;
    uploadDesc.Width              = totalBytes;
    uploadDesc.Height             = 1;
    uploadDesc.DepthOrArraySize   = 1;
    uploadDesc.MipLevels          = 1;
    uploadDesc.Format             = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count   = 1;
    uploadDesc.SampleDesc.Quality = 0;
    uploadDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> uploadBuffer;
    DX_CHECK(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    // (4) CPU から中継バッファへ、1 行ずつコピーする
    //   ★ 画像全体を一度に memcpy できません。
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 }; // CPU からは読まない（書くだけ）

    DX_CHECK(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped)));

    const uint8_t* source          = pixels.data();
    const size_t   sourceRowPitch  = static_cast<size_t>(width) * kBytesPerPixel;
    const size_t   destRowPitch    = footprint.Footprint.RowPitch;

    for (uint32_t row = 0; row < numRows; ++row)
    {
        std::memcpy(mapped + row * destRowPitch,
                    source + row * sourceRowPitch,
                    static_cast<size_t>(rowSizeInBytes));
    }

    uploadBuffer->Unmap(0, nullptr);

    // (5) 「中継バッファ → テクスチャ」のコピー命令を記録する
    //   コピーも GPU の仕事なので、コマンドリストに記録して実行させます。
    ComPtr<ID3D12CommandAllocator>    allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;

    DX_CHECK(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    DX_CHECK(device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
    // CreateCommandList 直後は「開いた」状態なので、そのまま記録を続けられる。

    // コピー元と行き先の指定
    //   同じ D3D12_TEXTURE_COPY_LOCATION 型ですが、Type によって中身が変わります。
    D3D12_TEXTURE_COPY_LOCATION source_ = {};
    source_.pResource       = uploadBuffer.Get();
    source_.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source_.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource        = m_texture.Get();
    destination.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;

    // 最後の 3 引数（コピー先の x, y, z）と nullptr（コピー範囲）で、
    // 「テクスチャ全体を (0,0,0) へ丸ごとコピー」を指定している。
    commandList->CopyTextureRegion(&destination, 0, 0, 0, &source_, nullptr);

    // (6) バリア : COPY_DEST → PIXEL_SHADER_RESOURCE
    //   「コピーの宛先」から「ピクセルシェーダーが読むテクスチャ」へ用途を切り替える。
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = m_texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    commandList->ResourceBarrier(1, &barrier);

    DX_CHECK(commandList->Close());

    // (7) 転送を実行し、完了を待つ
    commandQueue.ExecuteCommandList(commandList.Get());
    commandQueue.Flush();

    // (8) SRV（シェーダーリソースビュー）を作る
    //   「このテクスチャをシェーダーから読む」ための説明書です。
    const uint32_t srvIndex = descriptorHeap.Allocate();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format        = kFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

    // Shader4ComponentMapping
    //   「テクスチャの R,G,B,A を、シェーダーから見たときどの成分に割り当てるか」
    //   の指定です。入れ替えたり定数で埋めたりできます。
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    srvDesc.Texture2D.MostDetailedMip     = 0;
    srvDesc.Texture2D.MipLevels           = 1;
    srvDesc.Texture2D.PlaneSlice          = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    device->CreateShaderResourceView(m_texture.Get(), &srvDesc, descriptorHeap.CpuHandle(srvIndex));

    // 描画時に使うのは GPU ハンドルの方。
    m_srvGpuHandle = descriptorHeap.GpuHandle(srvIndex);

    Log(std::format(L"テクスチャを生成しました（{} x {}, DEFAULT ヒープ）", width, height));
}


/// <summary>
/// 市松模様（チェッカーボード）のピクセル列を生成します。
/// </summary>
std::vector<uint8_t> CreateCheckerboardPixels(uint32_t width, uint32_t height, uint32_t cellSize)
{
    assert(cellSize > 0);

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * Texture2D::kBytesPerPixel);

    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            // マス目の座標が偶数か奇数かで色を切り替える。
            const bool isLight = (((x / cellSize) + (y / cellSize)) % 2) == 0;

            const size_t index = (static_cast<size_t>(y) * width + x) * Texture2D::kBytesPerPixel;

            if (isLight)
            {
                pixels[index + 0] = 235; // R
                pixels[index + 1] = 235; // G
                pixels[index + 2] = 235; // B
            }
            else
            {
                pixels[index + 0] = 60;
                pixels[index + 1] = 60;
                pixels[index + 2] = 70;
            }
            pixels[index + 3] = 255; // A（不透明）
        }
    }

    return pixels;
}

} // namespace dx12
