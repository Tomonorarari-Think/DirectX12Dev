//=============================================================================
// MaterialSet.h
//   1 つのモデルが使う材質を、まとめて GPU 上の資源に変換して持つ。
//
//   材質ごとに「基本色（定数バッファ）」と「テクスチャ（SRV）」が要る。
//   描画時は、サブメッシュを描く直前にこの 2 つを差し替える。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"
#include "ConstantBuffer.h"
#include "Geometry.h"
#include "Texture2D.h"

#include <DirectXMath.h>

#include <memory>
#include <vector>

namespace dx12
{
class CommandQueue;
class DescriptorHeap;

/// <summary>
/// 材質 1 つぶんの、シェーダーへ渡す定数。
/// </summary>
/// <remarks>
/// 16 バイト単位で区切られるため、`XMFLOAT4` に揃えています
/// （[11 章](../../docs/tutorial/11_陰影を付ける_法線とライティング.md) と同じ理由）。
/// </remarks>
struct MaterialConstants
{
    /// <summary>基本色。テクスチャの色と掛け合わせます。</summary>
    DirectX::XMFLOAT4 baseColorFactor;

    /// <summary>
    /// x = 金属らしさ、y = 粗さ、z = 法線マップの効き具合。w は未使用。
    /// </summary>
    /// <remarks>16 バイト単位に揃えるため、3 つの値でも `XMFLOAT4` にしています。</remarks>
    DirectX::XMFLOAT4 materialParams;
};


/// <summary>
/// モデル 1 つぶんの材質を、GPU 上の資源として保持するクラス。
/// </summary>
class MaterialSet
{
public:
    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    MaterialSet() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~MaterialSet() = default;

    /// <summary>コピー構築は禁止です。</summary>
    MaterialSet(const MaterialSet&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    MaterialSet& operator=(const MaterialSet&) = delete;

    /// <summary>
    /// 材質の一覧から、定数バッファとテクスチャを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="commandQueue">テクスチャ転送に使うキュー。完了まで待機します。</param>
    /// <param name="descriptorHeap">SRV を登録するシェーダー可視ヒープ。</param>
    /// <param name="materials">読み込み済みの材質。</param>
    /// <param name="fallbackTexture">
    /// テクスチャを持たない材質に使う画像。空なら白 1 ピクセルを作ります。
    /// </param>
    /// <exception cref="HrException">リソースの生成または転送に失敗した場合。</exception>
    /// <remarks>
    /// テクスチャを持たない材質が複数あっても、代用の画像は 1 枚だけ作って共有します。
    /// </remarks>
    void Initialize(ID3D12Device* device,
                    CommandQueue& commandQueue,
                    DescriptorHeap& descriptorHeap,
                    const std::vector<MaterialData>& materials,
                    const assets::ImageData& fallbackTexture = {});

    /// <summary>材質の数を返します。</summary>
    /// <returns>材質の数。</returns>
    uint32_t Count() const noexcept { return static_cast<uint32_t>(m_textureIndices.size()); }

    /// <summary>
    /// 指定した材質の定数バッファの GPU アドレスを返します。
    /// </summary>
    /// <param name="materialIndex">材質の番号。</param>
    /// <returns>`SetGraphicsRootConstantBufferView` に渡すアドレス。</returns>
    D3D12_GPU_VIRTUAL_ADDRESS ConstantAddress(uint32_t materialIndex) const;

    /// <summary>
    /// 指定した材質の基本色テクスチャの GPU ディスクリプタハンドルを返します。
    /// </summary>
    /// <param name="materialIndex">材質の番号。</param>
    /// <returns>`SetGraphicsRootDescriptorTable` に渡すハンドル。</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE TextureView(uint32_t materialIndex) const;

    /// <summary>
    /// 指定した材質の、金属らしさと粗さのテクスチャを返します。
    /// </summary>
    /// <param name="materialIndex">材質の番号。</param>
    /// <returns>`SetGraphicsRootDescriptorTable` に渡すハンドル。</returns>
    /// <remarks>
    /// 持たない材質には白 1 ピクセルを割り当てます。
    /// 白を掛けても値が変わらないので、シェーダーに分岐が要りません。
    /// </remarks>
    D3D12_GPU_DESCRIPTOR_HANDLE MetallicRoughnessView(uint32_t materialIndex) const;

    /// <summary>
    /// 指定した材質の法線マップの GPU ディスクリプタハンドルを返します。
    /// </summary>
    /// <param name="materialIndex">材質の番号。</param>
    /// <returns>`SetGraphicsRootDescriptorTable` に渡すハンドル。</returns>
    /// <remarks>
    /// 持たない材質には「真上を向いた法線」1 ピクセルを割り当てます。
    /// 面の法線をそのまま使うのと同じ結果になるため、分岐が要りません。
    /// </remarks>
    D3D12_GPU_DESCRIPTOR_HANDLE NormalMapView(uint32_t materialIndex) const;

private:
    /// <summary>
    /// このモデルが使うテクスチャ。
    /// </summary>
    /// <remarks>
    /// `Texture2D` はコピーできないため、ポインタで持ちます。
    /// </remarks>
    std::vector<std::unique_ptr<Texture2D>> m_textures;

    /// <summary>材質ごとに、どの基本色テクスチャを使うかの番号。</summary>
    std::vector<uint32_t> m_textureIndices;

    /// <summary>材質ごとに、どの金属らしさ・粗さテクスチャを使うかの番号。</summary>
    std::vector<uint32_t> m_metallicRoughnessIndices;

    /// <summary>材質ごとに、どの法線マップを使うかの番号。</summary>
    std::vector<uint32_t> m_normalMapIndices;

    /// <summary>材質ごとの定数（基本色）。材質の数ぶんのスロットを持ちます。</summary>
    /// <remarks>
    /// フレーム別に分ける必要はありません。読み込み後は書き換わらないためです。
    /// </remarks>
    ConstantBuffer m_constants;
};

} // namespace dx12
