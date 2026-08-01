//=============================================================================
// MaterialSet.cpp
//   MaterialSet の実装。
//=============================================================================
#include "MaterialSet.h"

#include "../Assets/MipGenerator.h"

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

/// <summary>
/// 法線マップを持たない材質のための、「真上を向いた法線」1 ピクセルを作ります。
/// </summary>
/// <returns>1 x 1 の (128, 128, 255) の画像。</returns>
/// <remarks>
/// 接線空間の法線 (0, 0, 1) を 0〜1 に詰め直すと (0.5, 0.5, 1.0) になります。
/// これは「面の向きから傾いていない」という意味なので、
/// 法線マップを持たない材質に割り当てても見た目が変わりません。
/// </remarks>
assets::ImageData CreateFlatNormalPixel()
{
    assets::ImageData image;
    image.width  = 1;
    image.height = 1;
    image.pixels = { 128, 128, 255, 255 };
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
    fallbackTexture2D->Initialize(
        device, commandQueue, descriptorHeap,
        assets::GenerateMipChain(fallback, assets::MipFilter::Color),
        /* isColorTexture */ true);

    m_textures.push_back(std::move(fallbackTexture2D));

    const uint32_t fallbackIndex = 0;

    // 金属らしさ・粗さを持たない材質のための白 1 ピクセル。
    //   ★ こちらは色ではなく数値なので、sRGB として読んではいけない。
    auto neutralMaterialTexture = std::make_unique<Texture2D>();
    {
        const assets::ImageData white = CreateWhitePixel();
        neutralMaterialTexture->Initialize(device, commandQueue, descriptorHeap,
                                           white.width, white.height, white.pixels,
                                           /* isColorTexture */ false);
    }

    const uint32_t neutralIndex = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(std::move(neutralMaterialTexture));

    // 法線マップを持たない材質のための、真上を向いた法線 1 ピクセル。
    //   ★ こちらもベクトルなので sRGB として読んではいけない。
    auto flatNormalTexture = std::make_unique<Texture2D>();
    {
        const assets::ImageData flat = CreateFlatNormalPixel();
        flatNormalTexture->Initialize(device, commandQueue, descriptorHeap,
                                      flat.width, flat.height, flat.pixels,
                                      /* isColorTexture */ false);
    }

    const uint32_t flatNormalIndex = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(std::move(flatNormalTexture));

    // (2) 材質ごとにテクスチャを用意する。
    m_textureIndices.reserve(materials.size());
    m_metallicRoughnessIndices.reserve(materials.size());
    m_normalMapIndices.reserve(materials.size());

    uint32_t texturedCount = 0;

    for (const MaterialData& material : materials)
    {
        if (!material.HasTexture())
        {
            m_textureIndices.push_back(fallbackIndex);
        }
        else
        {
            auto texture = std::make_unique<Texture2D>();
            // 基本色テクスチャは「見た目の色」なので sRGB として読む。
            //   ★ 平均はリニアで取る必要があるので、縮小の仕方も色用を指定する。
            texture->Initialize(
                device, commandQueue, descriptorHeap,
                assets::GenerateMipChain(material.baseColorTexture,
                                         assets::MipFilter::Color),
                /* isColorTexture */ true);

            m_textureIndices.push_back(static_cast<uint32_t>(m_textures.size()));
            m_textures.push_back(std::move(texture));

            ++texturedCount;
        }

        if (material.metallicRoughnessTexture.pixels.empty())
        {
            m_metallicRoughnessIndices.push_back(neutralIndex);
        }
        else
        {
            auto texture = std::make_unique<Texture2D>();
            // ★ 金属らしさと粗さは数値。色ではないので sRGB にしない。
            //   縮小もそのまま平均するだけでよい。
            texture->Initialize(
                device, commandQueue, descriptorHeap,
                assets::GenerateMipChain(material.metallicRoughnessTexture,
                                         assets::MipFilter::Linear),
                /* isColorTexture */ false);

            m_metallicRoughnessIndices.push_back(
                static_cast<uint32_t>(m_textures.size()));
            m_textures.push_back(std::move(texture));
        }

        if (material.normalTexture.pixels.empty())
        {
            m_normalMapIndices.push_back(flatNormalIndex);
        }
        else
        {
            auto texture = std::make_unique<Texture2D>();
            // ★ 法線マップはベクトル。色ではないので sRGB にしない。
            //   縮小では -1〜1 に戻して平均し、長さを 1 に戻す必要がある。
            texture->Initialize(
                device, commandQueue, descriptorHeap,
                assets::GenerateMipChain(material.normalTexture,
                                         assets::MipFilter::Normal),
                /* isColorTexture */ false);

            m_normalMapIndices.push_back(static_cast<uint32_t>(m_textures.size()));
            m_textures.push_back(std::move(texture));
        }
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

        constants.materialParams = { materials[i].metallicFactor,
                                     materials[i].roughnessFactor,
                                     materials[i].normalScale,
                                     0.0f };

        // ★ テクスチャの「場所」ではなく「番号」を持たせる。
        //   描くたびにディスクリプタテーブルを結び直す必要がなくなる。
        constants.textureIndices[0] =
            m_textures[m_textureIndices[i]]->DescriptorIndex();
        constants.textureIndices[1] =
            m_textures[m_metallicRoughnessIndices[i]]->DescriptorIndex();
        constants.textureIndices[2] =
            m_textures[m_normalMapIndices[i]]->DescriptorIndex();
        constants.textureIndices[3] = 0;

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


/// <summary>
/// 指定した材質の、金属らしさと粗さのテクスチャを返します。
/// </summary>
D3D12_GPU_DESCRIPTOR_HANDLE MaterialSet::MetallicRoughnessView(
    uint32_t materialIndex) const
{
    assert(materialIndex < Count());
    return m_textures[m_metallicRoughnessIndices[materialIndex]]->ShaderResourceView();
}


/// <summary>
/// 指定した材質の法線マップを返します。
/// </summary>
D3D12_GPU_DESCRIPTOR_HANDLE MaterialSet::NormalMapView(uint32_t materialIndex) const
{
    assert(materialIndex < Count());
    return m_textures[m_normalMapIndices[materialIndex]]->ShaderResourceView();
}

} // namespace dx12
