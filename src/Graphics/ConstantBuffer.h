//=============================================================================
// ConstantBuffer.h
//   シェーダーへ毎フレーム値を渡すための定数バッファ。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// <summary>
/// 定数バッファ（Constant Buffer）を、256 バイト境界に並べた領域の配列として管理します。
/// </summary>
class ConstantBuffer
{
public:
    /// <summary>
    /// 定数バッファのアドレス境界（バイト）。
    /// </summary>
    static constexpr uint32_t kAlignment = 256;

    /// <summary>
    /// 既定のコンストラクタ。まだ何も生成されません。
    /// </summary>
    ConstantBuffer() = default;

    /// <summary>
    /// デストラクタ。マップを解除します。
    /// </summary>
    ~ConstantBuffer();

    /// <summary>
    /// コピー構築は禁止です。
    /// </summary>
    ConstantBuffer(const ConstantBuffer&) = delete;

    /// <summary>
    /// コピー代入は禁止です。
    /// </summary>
    ConstantBuffer& operator=(const ConstantBuffer&) = delete;

    /// <summary>
    /// 指定した個数の領域（スロット）を持つ定数バッファを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="sizeInBytes">1 スロットぶんのデータサイズ（切り上げ前）。</param>
    /// <param name="slotCount">用意するスロット数。</param>
    /// <exception cref="HrException">リソースの生成またはマップに失敗した場合。</exception>
    /// <remarks>
    /// フレーム別に持ちたいだけなら `slotCount` はバックバッファの枚数です。
    /// オブジェクトごとにも分けたい場合は「フレーム数 × オブジェクト数」にします。
    /// </remarks>
    void Initialize(ID3D12Device* device, uint32_t sizeInBytes, uint32_t slotCount);

    /// <summary>
    /// 指定スロットへデータを書き込みます。
    /// </summary>
    /// <param name="slotIndex">書き込み先のスロット番号。</param>
    /// <param name="data">書き込むデータの先頭アドレス。</param>
    /// <param name="sizeInBytes">
    /// 書き込むバイト数。生成時のサイズ以下である必要があります。
    /// </param>
    void Update(uint32_t slotIndex, const void* data, uint32_t sizeInBytes);

    /// <summary>
    /// 指定スロットの GPU アドレスを取得します。
    /// </summary>
    /// <param name="slotIndex">取得するスロット番号。</param>
    /// <returns>`SetGraphicsRootConstantBufferView` に渡す GPU 仮想アドレス。</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(uint32_t slotIndex) const;

    /// <summary>
    /// サイズを 256 バイト境界へ切り上げます。
    /// </summary>
    /// <param name="sizeInBytes">元のサイズ（バイト）。</param>
    /// <returns>256 の倍数に切り上げたサイズ。</returns>
    static constexpr uint32_t Align(uint32_t sizeInBytes)
    {
        return (sizeInBytes + (kAlignment - 1)) & ~(kAlignment - 1);
    }

private:
    /// <summary>
    /// 全スロットぶんをまとめて確保した UPLOAD ヒープ上のバッファ。
    /// </summary>
    ComPtr<ID3D12Resource> m_resource;

    /// <summary>
    /// マップされた CPU 側の先頭アドレス（永続マップ）。
    /// </summary>
    uint8_t* m_mappedData = nullptr;

    /// <summary>
    /// 1 スロットぶんの領域サイズ（256 バイト境界へ切り上げ済み）。
    /// </summary>
    uint32_t m_alignedSize = 0;

    /// <summary>
    /// 確保したスロット数。
    /// </summary>
    uint32_t m_slotCount = 0;
};

} // namespace dx12
