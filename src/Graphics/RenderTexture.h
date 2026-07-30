//=============================================================================
// RenderTexture.h
//   「描き込める」テクスチャ。画面ではなくここへ絵を描き、あとで加工する。
//
//   1 枚のリソースに RTV（書く用）と SRV（読む用）の 2 つのビューを付ける。
//   詳しい解説は docs/tutorial/25_ポストプロセス.md を参照。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

#include <cstdint>
#include <string>

namespace dx12
{
class DescriptorHeap;

/// <summary>
/// 描画先にもテクスチャにもなる 1 枚の画像。
/// </summary>
/// <remarks>
/// **HDR（浮動小数点）で持ちます。** 1.0 を超える明るさを保ったまま
/// 次の処理へ渡したいためです。8 bit だと、その場で切り落とされてしまいます。
/// </remarks>
class RenderTexture
{
public:
    /// <summary>
    /// 中間バッファの形式。1 成分 16 bit の浮動小数点。
    /// </summary>
    /// <remarks>
    /// 32 bit だと容量と帯域が 2 倍になります。明るさの表現には 16 bit で足ります。
    /// </remarks>
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    RenderTexture() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~RenderTexture() = default;

    /// <summary>コピー構築は禁止です。</summary>
    RenderTexture(const RenderTexture&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    RenderTexture& operator=(const RenderTexture&) = delete;

    /// <summary>
    /// 描画先とテクスチャを兼ねる 1 枚を生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="descriptorHeap">SRV を登録するシェーダー可視ヒープ。</param>
    /// <param name="width">幅（ピクセル）。</param>
    /// <param name="height">高さ（ピクセル）。</param>
    /// <param name="debugName">デバッグ表示用の名前。</param>
    /// <param name="clearColor">最適化クリア値。クリア時の値と揃えます。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    /// <remarks>
    /// RTV は専用のヒープを自分で 1 個だけ持ちます。RTV は
    /// **シェーダーから見えないヒープ**に置く決まりで、SRV とは同居できません。
    /// </remarks>
    void Initialize(ID3D12Device* device,
                    DescriptorHeap& descriptorHeap,
                    uint32_t width,
                    uint32_t height,
                    const std::wstring& debugName,
                    const float clearColor[4]);

    /// <summary>
    /// 描画先として使う準備をします（バリアと描画先の設定、クリア）。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="depthStencilView">併用する深度ビュー。不要なら `nullptr`。</param>
    /// <remarks>`EndRender` と必ず対で呼びます。</remarks>
    void BeginRender(ID3D12GraphicsCommandList* commandList,
                     const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencilView);

    /// <summary>
    /// 描画を終え、テクスチャとして読める状態へ戻します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    void EndRender(ID3D12GraphicsCommandList* commandList);

    /// <summary>シェーダーから読むためのハンドルを返します。</summary>
    /// <returns>`SetGraphicsRootDescriptorTable` に渡すハンドル。</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE ShaderResourceView() const { return m_srvGpuHandle; }

    /// <summary>描画先として指定するための RTV を返します。</summary>
    /// <returns>専用ヒープ内の CPU ディスクリプタハンドル。</returns>
    /// <remarks>
    /// `BeginRender` のあとで描画先を付け替えたいときに使います
    /// （深度ビューだけを差し替える場合など）。
    /// </remarks>
    D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView() const
    {
        return m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    }

    /// <summary>幅を返します。</summary>
    /// <returns>ピクセル単位の幅。</returns>
    uint32_t Width() const noexcept { return m_width; }

    /// <summary>高さを返します。</summary>
    /// <returns>ピクセル単位の高さ。</returns>
    uint32_t Height() const noexcept { return m_height; }

private:
    /// <summary>リソース本体。</summary>
    ComPtr<ID3D12Resource> m_texture;

    /// <summary>RTV 専用のディスクリプタヒープ（1 個ぶん）。</summary>
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    /// <summary>シェーダーから読むためのハンドル。</summary>
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpuHandle = {};

    /// <summary>幅。</summary>
    uint32_t m_width = 0;

    /// <summary>高さ。</summary>
    uint32_t m_height = 0;

    /// <summary>クリアに使う色。生成時の最適化クリア値と揃えます。</summary>
    float m_clearColor[4] = {};
};

} // namespace dx12
