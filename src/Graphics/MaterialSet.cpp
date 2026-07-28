//=============================================================================
// MaterialSet.cpp
//   MaterialSet の実装。
//=============================================================================
#include "MaterialSet.h"

#include "CommandQueue.h"
#include "DescriptorHeap.h"

#include <cassert>
#include <format>

namespace dx12
{
namespace
{
/// <summary>
/// テクスチャを持たない材質のために使う、白 1 ピクセルの画像を作ります。
/// </summary>
/// <returns>1 x 1 の白い画像。</returns>
/// <remarks>
/// シェーダー側に分岐を入れずに済ませるための工夫です。
/// 白を掛けても色は変わらないので、基本色がそのまま出ます。
/// </remarks>
assets::ImageData CreateWhitePixel()
{
    assets::ImageData image;
    image.width  = 1;
    image.height = 1;
    image.pixels = { 255, 255, 255, 255 };
    return image;
}
} // namespace


/// <summary>
/// 材質の一覧から、定数バッファとテクスチャを生成します。
/// </summary>
void MaterialSet::Initialize(ID3D12Device* device,
                             CommandQueue& commandQueue,
                             DescriptorHeap& descriptorHeap,
                             const std::vector<MaterialData>& materials,
                             const assets::ImageData& fallbackTexture)
{
    assert(!materials.empty());

    // (1) 代用テクスチャ。テクスチャを持たない材質が何個あっても 1 枚で足りる。
    const assets::ImageData fallback =
        fallbackTexture.pixels.empty() ? CreateWhitePixel() : fallbackTexture;

    auto fallbackTexture2D = std::make_unique<Texture2D>();
    fallbackTexture2D->Initialize(device, commandQueue, descriptorHeap,
                                  fallback.width, fallback.height, fallback.pixels,
                                  /* isColorTexture */ true);

    m_textures.push_back(std::move(fallbackTexture2D));

    const uint32_t fallbackIndex = 0;

    // (2) 材質ごとにテクスチャを用意する。
    m_textureIndices.reserve(materials.size());

    uint32_t texturedCount = 0;

    for (const MaterialData& material : materials)
    {
        if (!material.HasTexture())
        {
            m_textureIndices.push_back(fallbackIndex);
            continue;
        }

        auto texture = std::make_unique<Texture2D>();
        // 基本色テクスチャは「見た目の色」なので sRGB として読む。
        texture->Initialize(device, commandQueue, descriptorHeap,
                            material.baseColorTexture.width,
                            material.baseColorTexture.height,
                            material.baseColorTexture.pixels,
                            /* isColorTexture */ true);

        m_textureIndices.push_back(static_cast<uint32_t>(m_textures.size()));
        m_textures.push_back(std::move(texture));

        ++texturedCount;
    }

    // (3) 基本色を定数バッファへ。読み込み後は書き換わらないので 1 回だけ書く。
    m_constants.Initialize(device, sizeof(MaterialConstants),
                           static_cast<uint32_t>(materials.size()));

    for (size_t i = 0; i < materials.size(); ++i)
    {
        MaterialConstants constants = {};
        constants.baseColorFactor = { materials[i].baseColorFactor[0],
                                      materials[i].baseColorFactor[1],
                                      materials[i].baseColorFactor[2],
                                      materials[i].baseColorFactor[3] };

        m_constants.Update(static_cast<uint32_t>(i), &constants, sizeof(constants));
    }

    Log(std::format(L"材質を {} 個構築しました（うちテクスチャ付き {} 個）",
                    materials.size(), texturedCount));
}


/// <summary>
/// 指定した材質の定数バッファの GPU アドレスを返します。
/// </summary>
D3D12_GPU_VIRTUAL_ADDRESS MaterialSet::ConstantAddress(uint32_t materialIndex) const
{
    assert(materialIndex < Count());
    return m_constants.GpuAddress(materialIndex);
}


/// <summary>
/// 指定した材質のテクスチャの GPU ディスクリプタハンドルを返します。
/// </summary>
D3D12_GPU_DESCRIPTOR_HANDLE MaterialSet::TextureView(uint32_t materialIndex) const
{
    assert(materialIndex < Count());
    return m_textures[m_textureIndices[materialIndex]]->ShaderResourceView();
}

} // namespace dx12
