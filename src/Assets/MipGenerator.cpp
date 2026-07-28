//=============================================================================
// MipGenerator.cpp
//   MipGenerator の実装。
//=============================================================================
#include "MipGenerator.h"

#include <algorithm>
#include <cmath>

namespace dx12::assets
{
namespace
{
/// <summary>sRGB で記録された 1 成分を、リニアの明るさへ戻します。</summary>
float SrgbToLinear(float value)
{
    return (value <= 0.04045f) ? (value / 12.92f)
                               : std::pow((value + 0.055f) / 1.055f, 2.4f);
}


/// <summary>リニアの明るさを sRGB の記録値へ戻します。</summary>
float LinearToSrgb(float value)
{
    return (value <= 0.0031308f) ? (12.92f * value)
                                 : (1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f);
}


/// <summary>0〜255 を 0〜1 にします。</summary>
float Normalized(uint8_t value)
{
    return value / 255.0f;
}


/// <summary>0〜1 を 0〜255 に丸めます。</summary>
uint8_t Quantized(float value)
{
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}


/// <summary>
/// 4 テクセルを 1 つにまとめて、縦横が半分の画像を作ります。
/// </summary>
/// <param name="source">元の画像。</param>
/// <param name="filter">平均の取り方。</param>
/// <returns>縦横が半分（最小 1）の画像。</returns>
ImageData Halve(const ImageData& source, MipFilter filter)
{
    ImageData result;
    result.width  = std::max(source.width / 2, 1u);
    result.height = std::max(source.height / 2, 1u);
    result.pixels.assign(static_cast<size_t>(result.width) * result.height * 4, 0);

    for (uint32_t y = 0; y < result.height; ++y)
    {
        for (uint32_t x = 0; x < result.width; ++x)
        {
            // 元画像で対応する 2 x 2 の範囲。端では同じテクセルを 2 度読む。
            const uint32_t x0 = std::min(x * 2, source.width - 1);
            const uint32_t x1 = std::min(x * 2 + 1, source.width - 1);
            const uint32_t y0 = std::min(y * 2, source.height - 1);
            const uint32_t y1 = std::min(y * 2 + 1, source.height - 1);

            const uint32_t xs[2] = { x0, x1 };
            const uint32_t ys[2] = { y0, y1 };

            float sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

            for (uint32_t sy : ys)
            {
                for (uint32_t sx : xs)
                {
                    const size_t index =
                        (static_cast<size_t>(sy) * source.width + sx) * 4;

                    for (int c = 0; c < 3; ++c)
                    {
                        const float raw = Normalized(source.pixels[index + c]);

                        switch (filter)
                        {
                        case MipFilter::Color:
                            // ★ 色は sRGB のまま平均してはいけない。暗く沈む。
                            sum[c] += SrgbToLinear(raw);
                            break;

                        case MipFilter::Normal:
                            // ★ 0〜1 に詰められたベクトル。-1〜1 へ戻してから足す。
                            sum[c] += raw * 2.0f - 1.0f;
                            break;

                        case MipFilter::Linear:
                            sum[c] += raw;
                            break;
                        }
                    }

                    // アルファは常にそのまま平均する。
                    sum[3] += Normalized(source.pixels[index + 3]);
                }
            }

            for (int c = 0; c < 4; ++c)
            {
                sum[c] *= 0.25f;
            }

            const size_t destination =
                (static_cast<size_t>(y) * result.width + x) * 4;

            if (filter == MipFilter::Normal)
            {
                // ★ 平均したベクトルは長さが 1 でなくなる。戻さないと
                //   陰影が弱くなったり、逆に暴れたりする。
                const float length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1]
                                             + sum[2] * sum[2]);
                const float inverse = (length > 1e-8f) ? (1.0f / length) : 0.0f;

                for (int c = 0; c < 3; ++c)
                {
                    const float normal = (length > 1e-8f) ? (sum[c] * inverse)
                                                          : ((c == 2) ? 1.0f : 0.0f);
                    result.pixels[destination + c] = Quantized(normal * 0.5f + 0.5f);
                }
            }
            else
            {
                for (int c = 0; c < 3; ++c)
                {
                    const float value = (filter == MipFilter::Color)
                                      ? LinearToSrgb(sum[c])
                                      : sum[c];
                    result.pixels[destination + c] = Quantized(value);
                }
            }

            result.pixels[destination + 3] = Quantized(sum[3]);
        }
    }

    return result;
}
} // namespace


/// <summary>
/// 画像を半分ずつ縮小して、1 x 1 になるまでの列を作ります。
/// </summary>
std::vector<ImageData> GenerateMipChain(const ImageData& source, MipFilter filter)
{
    std::vector<ImageData> chain;
    chain.push_back(source);

    while (chain.back().width > 1 || chain.back().height > 1)
    {
        chain.push_back(Halve(chain.back(), filter));
    }

    return chain;
}

} // namespace dx12::assets
