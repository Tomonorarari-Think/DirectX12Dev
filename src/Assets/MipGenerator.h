//=============================================================================
// MipGenerator.h
//   1 枚の画像から、段階的に小さくした画像の列（ミップ列）を作る。
//
//   DirectX は使わず、画像を計算で作り替えるだけの層。
//   詳しい解説は docs/tutorial/24_ミップマップ.md を参照。
//=============================================================================
#pragma once

#include "ImageLoader.h"

#include <vector>

namespace dx12::assets
{

/// <summary>
/// ミップを作るときの平均の取り方。中身の意味によって変わります。
/// </summary>
/// <remarks>
/// **同じ「平均」でも、値の意味が違えば正しい手順が違います。**
/// ここを取り違えると、縮小するほど暗くなったり、凹凸が消えたりします。
/// </remarks>
enum class MipFilter
{
    /// <summary>見た目の色（sRGB で記録されている）。リニアに戻してから平均します。</summary>
    Color,

    /// <summary>粗さや金属らしさなどの数値。そのまま平均します。</summary>
    Linear,

    /// <summary>接線空間の法線。ベクトルとして平均し、長さを 1 に戻します。</summary>
    Normal,
};


/// <summary>
/// 画像を半分ずつ縮小して、1 x 1 になるまでの列を作ります。
/// </summary>
/// <param name="source">元の画像。先頭の段としてそのまま使われます。</param>
/// <param name="filter">平均の取り方。中身の意味に合わせて選びます。</param>
/// <returns>鮮明な順に並んだ画像の列。先頭が原寸です。</returns>
/// <remarks>
/// 4 テクセルを 1 つにまとめる、いちばん素直な縮小です。
/// 幅か高さが奇数のときは切り捨てるので、わずかにずれます。
/// 2 の冪の画像を使えば起きません。
/// </remarks>
std::vector<ImageData> GenerateMipChain(const ImageData& source, MipFilter filter);

} // namespace dx12::assets
