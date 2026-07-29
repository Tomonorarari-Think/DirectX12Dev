//=============================================================================
// 15_hash.hlsl
//   ハッシュ関数の精度 : 有名な書き方が壊れるところを見る。
//
//   この習作を作っている途中で、雲に縦縞が出た。原因はノイズではなく
//   その下にあるハッシュ関数の桁溢れだった。並べて確かめる。
//   解説 : docs/shader-lab/15_ハッシュの精度.md
//=============================================================================
#include "LabCommon.hlsli"

/// よく見かける書き方。**32 bit 浮動小数点では途中で桁が溢れる。**
///   sin の結果（-1〜1）を 43758.5 倍してから小数部を取るので、
///   有効桁が足りず、値が飛び飛びになる。
float HashBad(float2 p)
{
    return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f);
}

/// 途中の値を 0〜1 付近に保つ書き方（Dave Hoskins）。
float HashGood(float2 p)
{
    float3 p3 = frac(p.xyx * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

/// 指定したハッシュで値ノイズを作る。
float NoiseWith(float2 p, bool useBad)
{
    float2 cell  = floor(p);
    float2 local = frac(p);
    float2 w = local * local * (3.0f - 2.0f * local);

    float a = useBad ? HashBad(cell + float2(0, 0)) : HashGood(cell + float2(0, 0));
    float b = useBad ? HashBad(cell + float2(1, 0)) : HashGood(cell + float2(1, 0));
    float c = useBad ? HashBad(cell + float2(0, 1)) : HashGood(cell + float2(0, 1));
    float d = useBad ? HashBad(cell + float2(1, 1)) : HashGood(cell + float2(1, 1));

    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}

/// 指定したハッシュで fBm を作る。
float FbmWith(float2 p, bool useBad)
{
    float sum = 0.0f;
    float amp = 0.5f;

    for (int i = 0; i < 6; ++i)
    {
        sum += NoiseWith(p, useBad) * amp;
        p *= 2.0f;
        amp *= 0.5f;
    }

    return sum;
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;

    bool useBad = uv.x < 0.5f;

    // ★ 座標を大きくずらすと、桁溢れがはっきり出る。
    //   実際のシーンでも、原点から離れた場所ほど壊れやすい。
    float2 p = ToCenteredUv(uv) * 2.0f + float2(useBad ? 60.0f : 60.0f, 40.0f);

    float3 color;

    if (uv.y < 0.62f)
    {
        // --- 上 : fBm で作った雲 -------------------------------------------
        float density = FbmWith(p, useBad);
        float coverage = smoothstep(0.40f, 0.68f, density);

        float3 sky = lerp(float3(0.60f, 0.72f, 0.90f),
                          float3(0.16f, 0.34f, 0.70f),
                          saturate(1.0f - uv.y * 1.6f));

        color = lerp(sky, float3(1.0f, 0.99f, 0.97f), coverage);
    }
    else
    {
        // --- 下 : ハッシュそのものを点で見る -------------------------------
        //   よい乱数なら、灰色の砂嵐になるはず。
        //   壊れていると、縞・格子・偏りが見える。
        float2 q = ToPixel(uv) + float2(useBad ? 0.0f : 4096.0f, 8192.0f);
        float v = useBad ? HashBad(floor(q)) : HashGood(floor(q));
        color = float3(v, v, v);
    }

    // 仕切りと見出しの帯
    color = lerp(color, float3(0.95f, 0.55f, 0.30f),
                 1.0f - smoothstep(0.0f, 0.002f, abs(uv.x - 0.5f)));
    color = lerp(color, float3(0.95f, 0.55f, 0.30f),
                 1.0f - smoothstep(0.0f, 0.002f, abs(uv.y - 0.62f)));

    return LabOutput(color);
}
