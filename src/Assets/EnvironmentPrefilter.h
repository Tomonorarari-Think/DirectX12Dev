//=============================================================================
// EnvironmentPrefilter.h
//   環境マップ（周囲の景色）を、映り込みに使える形へ下ごしらえする。
//
//   DirectX は使わず、画像を計算で作り替えるだけの層。
//   詳しい解説は docs/tutorial/21_IBL_環境マップ.md を参照。
//=============================================================================
#pragma once

#include "ImageLoader.h"

#include <vector>

namespace dx12::assets
{

/// <summary>
/// 鏡面反射用に、粗さの段階ごとにぼかした画像の列を作ります。
/// </summary>
/// <param name="source">正距円筒図法の環境画像。</param>
/// <param name="baseWidth">いちばん鮮明な段の横幅。縦はその半分になります。</param>
/// <param name="mipCount">作る段数。</param>
/// <returns>鮮明な順に並んだ画像の列（ミップ列）。</returns>
/// <remarks>
/// 段が進むほど強くぼかします。粗い材質ほど後ろの段を読むことで、
/// 「ざらざらした表面は映り込みがぼやける」という見え方を作ります。
///
/// 本来は GGX の分布に沿って重み付けしながら畳み込みます。ここでは
/// 段階的なぼかしで近似しており、**物理的に正確ではありません**。
/// </remarks>
std::vector<ImageData> PrefilterEnvironment(const ImageData& source,
                                            uint32_t baseWidth = 256,
                                            uint32_t mipCount = 6);

/// <summary>
/// 拡散反射用に、あらゆる方向から届く光を積分した画像を作ります。
/// </summary>
/// <param name="source">正距円筒図法の環境画像。</param>
/// <param name="width">出力の横幅。縦はその半分になります。</param>
/// <returns>方向ごとの「入ってくる光の量」を表す画像。</returns>
/// <remarks>
/// 拡散反射は入射方向を区別せず、半球すべてから来る光を
/// 面の傾きで重み付けして足したものになります。
/// その積分をあらかじめ済ませておくのがこの画像です。
/// 結果はなだらかなので、小さな解像度で足ります。
/// </remarks>
ImageData ComputeIrradiance(const ImageData& source, uint32_t width = 32);

} // namespace dx12::assets
