//=============================================================================
// EnvironmentPrefilter.cpp
//   EnvironmentPrefilter の実装。
//=============================================================================
#include "EnvironmentPrefilter.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dx12::assets
{
namespace
{
/// <summary>円周率。</summary>
constexpr float kPi = 3.14159265358979f;


/// <summary>
/// sRGB で記録された 1 成分を、リニアの明るさへ戻します。
/// </summary>
/// <param name="value">0〜1 に正規化した記録値。</param>
/// <returns>リニアの明るさ。</returns>
/// <remarks>
/// 畳み込み（足し合わせ）はリニアでなければ意味を持ちません
/// （[19 章](../../docs/tutorial/19_色を正しく扱う_sRGB.md)）。
/// </remarks>
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


/// <summary>画像から 1 ピクセル読み、リニアの RGB を返します。</summary>
void ReadLinear(const ImageData& image, uint32_t x, uint32_t y, float out[3])
{
    const size_t index = (static_cast<size_t>(y) * image.width + x) * 4;
    for (int c = 0; c < 3; ++c)
    {
        out[c] = SrgbToLinear(image.pixels[index + c] / 255.0f);
    }
}


/// <summary>リニアの RGB を書き込みます。</summary>
void WriteLinear(ImageData& image, uint32_t x, uint32_t y, const float value[3])
{
    const size_t index = (static_cast<size_t>(y) * image.width + x) * 4;
    for (int c = 0; c < 3; ++c)
    {
        const float encoded = LinearToSrgb(std::clamp(value[c], 0.0f, 1.0f));
        image.pixels[index + c] = static_cast<uint8_t>(encoded * 255.0f + 0.5f);
    }
    image.pixels[index + 3] = 255;
}


/// <summary>指定した大きさの空の画像を用意します。</summary>
ImageData CreateImage(uint32_t width, uint32_t height)
{
    ImageData image;
    image.width  = width;
    image.height = height;
    image.pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    return image;
}


/// <summary>
/// 画像を指定した大きさへ縮小します（面積平均）。
/// </summary>
/// <remarks>
/// リニア空間で平均します。sRGB のまま平均すると暗くなります。
/// </remarks>
ImageData Resize(const ImageData& source, uint32_t width, uint32_t height)
{
    ImageData result = CreateImage(width, height);

    for (uint32_t y = 0; y < height; ++y)
    {
        const uint32_t y0 = y * source.height / height;
        const uint32_t y1 = std::max(y0 + 1, (y + 1) * source.height / height);

        for (uint32_t x = 0; x < width; ++x)
        {
            const uint32_t x0 = x * source.width / width;
            const uint32_t x1 = std::max(x0 + 1, (x + 1) * source.width / width);

            float sum[3] = { 0.0f, 0.0f, 0.0f };
            uint32_t count = 0;

            for (uint32_t sy = y0; sy < y1 && sy < source.height; ++sy)
            {
                for (uint32_t sx = x0; sx < x1 && sx < source.width; ++sx)
                {
                    float texel[3];
                    ReadLinear(source, sx, sy, texel);
                    for (int c = 0; c < 3; ++c) { sum[c] += texel[c]; }
                    ++count;
                }
            }

            const float inverse = (count > 0) ? (1.0f / count) : 0.0f;
            const float averaged[3] = { sum[0] * inverse, sum[1] * inverse,
                                        sum[2] * inverse };
            WriteLinear(result, x, y, averaged);
        }
    }

    return result;
}


/// <summary>
/// 横方向はぐるりと繋がっている前提で、画像をぼかします。
/// </summary>
/// <param name="source">対象の画像。</param>
/// <param name="radius">ぼかす半径（ピクセル）。</param>
/// <returns>ぼかした画像。</returns>
/// <remarks>
/// 正距円筒図法では左端と右端が繋がっているため、横は巻き戻して参照します。
/// 縦は上下の極なので、端で打ち止めにします。
/// </remarks>
ImageData Blur(const ImageData& source, int radius)
{
    ImageData result = CreateImage(source.width, source.height);

    for (uint32_t y = 0; y < source.height; ++y)
    {
        for (uint32_t x = 0; x < source.width; ++x)
        {
            float sum[3] = { 0.0f, 0.0f, 0.0f };
            float weightSum = 0.0f;

            for (int dy = -radius; dy <= radius; ++dy)
            {
                const int sy = std::clamp(static_cast<int>(y) + dy, 0,
                                          static_cast<int>(source.height) - 1);

                for (int dx = -radius; dx <= radius; ++dx)
                {
                    // 横は一周して繋がる。
                    int sx = (static_cast<int>(x) + dx) % static_cast<int>(source.width);
                    if (sx < 0) { sx += static_cast<int>(source.width); }

                    // 距離が近いほど重い、素朴な重み付け。
                    const float distance = std::sqrt(
                        static_cast<float>(dx * dx + dy * dy));
                    const float weight = 1.0f / (1.0f + distance);

                    float texel[3];
                    ReadLinear(source, static_cast<uint32_t>(sx),
                               static_cast<uint32_t>(sy), texel);

                    for (int c = 0; c < 3; ++c) { sum[c] += texel[c] * weight; }
                    weightSum += weight;
                }
            }

            const float inverse = (weightSum > 0.0f) ? (1.0f / weightSum) : 0.0f;
            const float blurred[3] = { sum[0] * inverse, sum[1] * inverse,
                                       sum[2] * inverse };
            WriteLinear(result, x, y, blurred);
        }
    }

    return result;
}
} // namespace


/// <summary>
/// 鏡面反射用に、粗さの段階ごとにぼかした画像の列を作ります。
/// </summary>
std::vector<ImageData> PrefilterEnvironment(const ImageData& source,
                                            uint32_t baseWidth,
                                            uint32_t mipCount)
{
    std::vector<ImageData> mips;
    mips.reserve(mipCount);

    // 0 段目は縮小するだけ。粗さ 0 の材質はここを読む。
    mips.push_back(Resize(source, baseWidth, baseWidth / 2));

    for (uint32_t level = 1; level < mipCount; ++level)
    {
        const uint32_t width  = std::max(baseWidth >> level, 4u);
        const uint32_t height = std::max(width / 2, 2u);

        // 前の段を縮めてから、さらにぼかす。
        //   縮小そのものにも平均化の効果があるので、少ない半径で足りる。
        ImageData reduced = Resize(mips.back(), width, height);
        mips.push_back(Blur(reduced, 2));
    }

    return mips;
}


/// <summary>
/// 拡散反射用に、あらゆる方向から届く光を積分した画像を作ります。
/// </summary>
ImageData ComputeIrradiance(const ImageData& source, uint32_t width)
{
    const uint32_t height = std::max(width / 2, 1u);

    // 積分の元になる画像。細かいままだと計算量が跳ね上がるので粗くする。
    //   結果はなだらかなので、これで十分な精度が出る。
    const ImageData small = Resize(source, 64, 32);

    ImageData result = CreateImage(width, height);

    for (uint32_t y = 0; y < height; ++y)
    {
        // 出力の 1 ピクセルが表す「面の向き」。
        const float theta = kPi * (y + 0.5f) / height;

        for (uint32_t x = 0; x < width; ++x)
        {
            const float phi = 2.0f * kPi * ((x + 0.5f) / width - 0.5f);

            const float normal[3] = { std::sin(theta) * std::cos(phi),
                                      std::cos(theta),
                                      std::sin(theta) * std::sin(phi) };

            float sum[3] = { 0.0f, 0.0f, 0.0f };
            float weightSum = 0.0f;

            // 環境画像の全ピクセルを「光がやってくる方向」とみなして足し込む。
            for (uint32_t sy = 0; sy < small.height; ++sy)
            {
                const float lightTheta = kPi * (sy + 0.5f) / small.height;

                // 正距円筒図法では、極に近い行ほど狭い立体角しか表していない。
                //   その補正が sin。忘れると上下が過剰に効く。
                const float solidAngle = std::sin(lightTheta);

                for (uint32_t sx = 0; sx < small.width; ++sx)
                {
                    const float lightPhi =
                        2.0f * kPi * ((sx + 0.5f) / small.width - 0.5f);

                    const float direction[3] = {
                        std::sin(lightTheta) * std::cos(lightPhi),
                        std::cos(lightTheta),
                        std::sin(lightTheta) * std::sin(lightPhi) };

                    // 面に対して斜めから来る光ほど弱く効く（ランバートの余弦則）。
                    const float cosine = normal[0] * direction[0]
                                       + normal[1] * direction[1]
                                       + normal[2] * direction[2];

                    if (cosine <= 0.0f)
                    {
                        continue;   // 面の裏側から来る光は届かない
                    }

                    const float weight = cosine * solidAngle;

                    float texel[3];
                    ReadLinear(small, sx, sy, texel);

                    for (int c = 0; c < 3; ++c) { sum[c] += texel[c] * weight; }
                    weightSum += weight;
                }
            }

            // 重みの合計で割ることで「平均的な明るさ」になる。
            //   このあとシェーダーでアルベドを掛けるだけで拡散反射になる。
            const float inverse = (weightSum > 0.0f) ? (1.0f / weightSum) : 0.0f;
            const float irradiance[3] = { sum[0] * inverse, sum[1] * inverse,
                                          sum[2] * inverse };

            WriteLinear(result, x, y, irradiance);
        }
    }

    return result;
}

} // namespace dx12::assets
