//=============================================================================
// DescriptorHeap.h
//   シェーダーから参照できるディスクリプタヒープと、その単純な割り当て。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// @brief シェーダーから参照できるディスクリプタヒープを管理するクラス。
///
/// **これまでのヒープとの違い**
///
/// `SwapChain` の RTV ヒープや `DepthBuffer` の DSV ヒープは、
/// CPU が「どこに描くか」を指定するためだけのものでした。
/// GPU が中身を読むわけではないので `D3D12_DESCRIPTOR_HEAP_FLAG_NONE` です。
///
/// テクスチャ (SRV) や定数バッファ (CBV) をシェーダーから使う場合は、
/// **GPU 自身がディスクリプタを読みに行く**ため、
/// `SHADER_VISIBLE` フラグを付けたヒープが必要になります。
///
/// **重要な制約**
///
/// `SHADER_VISIBLE` なヒープは、コマンドリストに同時に 1 本しか設定できません
/// （CBV/SRV/UAV 用と、サンプラー用で 1 本ずつ）。
/// そのため実際のエンジンでは「巨大なヒープを 1 本作り、
/// その中を切り分けて全リソースで共有する」設計になります。
/// このクラスも、先頭から順番に切り出していく最も単純な形で作っています。
///
/// **なぜ解放を実装していないのか**
///
/// 本プロジェクトでは、起動時に確保したディスクリプタを終了まで使い続けます。
/// 途中で解放する必要が無いため、割り当てポインタを進めるだけの
/// 「使い捨て（リニアアロケータ）」で十分です。
/// リソースを動的に生成・破棄するようになったら、
/// 空き番号を管理するフリーリストが必要になります。
class DescriptorHeap
{
public:
    /// @brief 既定のコンストラクタ。まだ何も生成されません。
    DescriptorHeap() = default;

    /// @brief デストラクタ。ComPtr により COM オブジェクトが自動解放されます。
    ~DescriptorHeap() = default;

    /// @brief コピー構築は禁止です。
    DescriptorHeap(const DescriptorHeap&) = delete;

    /// @brief コピー代入は禁止です。
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;

    /// @brief シェーダーから参照できる CBV/SRV/UAV 用ヒープを生成します。
    /// @param device 生成に使う D3D12 デバイス。
    /// @param capacity 収容するディスクリプタの最大数。
    /// @exception HrException 生成に失敗した場合。
    void Initialize(ID3D12Device* device, uint32_t capacity);

    /// @brief ディスクリプタを 1 個ぶん確保し、その番号を返します。
    /// @returns 確保した位置の番号（0 始まり）。
    /// @exception std::runtime_error 容量を超えた場合。
    ///
    /// 返ってきた番号を `CpuHandle()` / `GpuHandle()` に渡すと、
    /// 実際の書き込み先・参照先が得られます。
    uint32_t Allocate();

    /// @brief 指定番号の CPU ディスクリプタハンドルを取得します。
    /// @param index `Allocate()` が返した番号。
    /// @returns CPU 側の書き込み先。`CreateShaderResourceView` などに渡します。
    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(uint32_t index) const;

    /// @brief 指定番号の GPU ディスクリプタハンドルを取得します。
    /// @param index `Allocate()` が返した番号。
    /// @returns GPU 側の参照先。`SetGraphicsRootDescriptorTable` に渡します。
    ///
    /// @note CPU ハンドルと GPU ハンドルは、同じディスクリプタを指していても
    /// 値がまったく別物です。用途を取り違えないよう注意してください。
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(uint32_t index) const;

    /// @brief 生成済みのヒープを取得します。
    /// @returns ヒープの生ポインタ（所有権は移動しません）。
    ///
    /// 描画前に `commandList->SetDescriptorHeaps()` へ渡す必要があります。
    ID3D12DescriptorHeap* Get() const noexcept { return m_heap.Get(); }

private:
    /// @brief ヒープ本体。
    ComPtr<ID3D12DescriptorHeap> m_heap;

    /// @brief ディスクリプタ 1 個あたりのバイト数（GPU ごとに異なる）。
    uint32_t m_descriptorSize = 0;

    /// @brief 収容できる最大数。
    uint32_t m_capacity = 0;

    /// @brief 次に割り当てる番号。解放しないので単調に増えるだけ。
    uint32_t m_allocatedCount = 0;
};

} // namespace dx12
