//=============================================================================
// DescriptorHeap.h
//   シェーダーから参照できるディスクリプタヒープと、その単純な割り当て。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// <summary>
/// シェーダーから参照できるディスクリプタヒープを管理するクラス。
/// </summary>
class DescriptorHeap
{
public:
    /// <summary>
    /// 既定のコンストラクタ。まだ何も生成されません。
    /// </summary>
    DescriptorHeap() = default;

    /// <summary>
    /// デストラクタ。ComPtr により COM オブジェクトが自動解放されます。
    /// </summary>
    ~DescriptorHeap() = default;

    /// <summary>
    /// コピー構築は禁止です。
    /// </summary>
    DescriptorHeap(const DescriptorHeap&) = delete;

    /// <summary>
    /// コピー代入は禁止です。
    /// </summary>
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;

    /// <summary>
    /// シェーダーから参照できる CBV/SRV/UAV 用ヒープを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="capacity">収容するディスクリプタの最大数。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void Initialize(ID3D12Device* device, uint32_t capacity);

    /// <summary>
    /// ディスクリプタを 1 個ぶん確保し、その番号を返します。
    /// </summary>
    /// <returns>確保した位置の番号（0 始まり）。</returns>
    /// <exception cref="std::runtime_error">容量を超えた場合。</exception>
    uint32_t Allocate();

    /// <summary>
    /// 指定番号の CPU ディスクリプタハンドルを取得します。
    /// </summary>
    /// <param name="index">`Allocate()` が返した番号。</param>
    /// <returns>CPU 側の書き込み先。`CreateShaderResourceView` などに渡します。</returns>
    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(uint32_t index) const;

    /// <summary>
    /// 指定番号の GPU ディスクリプタハンドルを取得します。
    /// </summary>
    /// <param name="index">`Allocate()` が返した番号。</param>
    /// <returns>GPU 側の参照先。`SetGraphicsRootDescriptorTable` に渡します。</returns>
    /// <remarks>
    /// CPU ハンドルと GPU ハンドルは、同じディスクリプタを指していても 値がまったく別物です。用
    /// 途を取り違えないよう注意してください。
    /// </remarks>
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(uint32_t index) const;

    /// <summary>
    /// 生成済みのヒープを取得します。
    /// </summary>
    /// <returns>ヒープの生ポインタ（所有権は移動しません）。</returns>
    ID3D12DescriptorHeap* Get() const noexcept { return m_heap.Get(); }

private:
    /// <summary>
    /// ヒープ本体。
    /// </summary>
    ComPtr<ID3D12DescriptorHeap> m_heap;

    /// <summary>
    /// ディスクリプタ 1 個あたりのバイト数（GPU ごとに異なる）。
    /// </summary>
    uint32_t m_descriptorSize = 0;

    /// <summary>
    /// 収容できる最大数。
    /// </summary>
    uint32_t m_capacity = 0;

    /// <summary>
    /// 次に割り当てる番号。解放しないので単調に増えるだけ。
    /// </summary>
    uint32_t m_allocatedCount = 0;
};

} // namespace dx12
