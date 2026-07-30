//=============================================================================
// 30_portal.hlsl
//   魔法陣とポータル : 回る輪を重ねる。
//
//   極座標の繰り返し（03 番）と距離関数（02 番）の組み合わせ。
//   輪ごとに速さと向きを変えるだけで、「動いている装置」に見える。
//   解説 : docs/shader-lab/30_魔法陣とポータル.md
//=============================================================================
#include "LabCommon.hlsli"

/// 輪 1 本ぶん。刻みの数と速さを変えて重ねる。
///   戻り値 : 光の量。
float Ring(float2 p, float radius, float thickness, float teeth,
           float speed, float duty, float time)
{
    float r = length(p);

    // 半径方向の帯
    float band = 1.0f - saturate(abs(r - radius) / thickness);
    band = pow(band, 1.6f);

    if (teeth < 0.5f)
    {
        return band;   // 刻みなしの実線
    }

    // 角度方向の刻み。回転は角度に時間を足すだけ。
    float angle = atan2(p.y, p.x) / kTau + 0.5f + time * speed;
    float cell  = frac(angle * teeth);

    // duty : 刻みの太さ（0.5 で半分が実線）
    float tooth = smoothstep(duty + 0.06f, duty - 0.06f, abs(cell - 0.5f));

    return band * tooth;
}

/// ルーン風の記号。円周上に小さな図形を並べる。
float Runes(float2 p, float radius, float count, float speed, float time)
{
    float angle = atan2(p.y, p.x) / kTau + 0.5f + time * speed;

    float index = floor(angle * count);
    float local = frac(angle * count) - 0.5f;

    float r = length(p);

    // 円周に沿った小さな枠
    float2 q = float2(local * (kTau * radius / count), r - radius);

    float seed = Hash21(float2(index, 7.0f));

    // 記号は 3 種類を切り替える
    float d;
    if (seed < 0.33f)
    {
        d = abs(SdCircle(q, 0.035f)) - 0.008f;
    }
    else if (seed < 0.66f)
    {
        d = abs(SdRoundedBox(q, float2(0.030f, 0.030f), 0.006f)) - 0.008f;
    }
    else
    {
        d = min(SdSegment(q, float2(-0.03f, -0.03f), float2(0.03f, 0.03f)),
                SdSegment(q, float2(-0.03f, 0.03f), float2(0.03f, -0.03f))) - 0.008f;
    }

    return smoothstep(0.006f, 0.0f, d);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 p = ToCenteredUv(input.uv);
    float  t = g_time.x;

    // --- 背景 : 渦を巻く空間 ------------------------------------------------
    float r = length(p);
    float angle = atan2(p.y, p.x);

    // ★ 角度に半径を足すと渦になる。吸い込まれるように見える。
    float swirl = angle + r * 2.6f - t * 0.9f;
    float2 swirled = float2(cos(swirl), sin(swirl)) * r;

    float depth = Fbm(swirled * 2.2f + t * 0.12f, 5);

    float3 inside = PaletteWarm(depth * 0.9f + 0.55f) * 0.9f;
    inside *= smoothstep(0.62f, 0.30f, r);   // 中心ほど明るい

    float3 color = float3(0.02f, 0.02f, 0.04f);
    color = lerp(color, inside, smoothstep(0.66f, 0.58f, r));

    // --- 輪 -----------------------------------------------------------------
    float rings = 0.0f;
    rings += Ring(p, 0.64f, 0.012f, 0.0f,  0.00f, 0.0f, t) * 1.0f;   // 外周の実線
    rings += Ring(p, 0.60f, 0.020f, 48.0f, 0.03f, 0.30f, t) * 0.8f;  // 細かい刻み
    rings += Ring(p, 0.50f, 0.030f, 12.0f, -0.06f, 0.34f, t) * 0.9f; // 逆回り
    rings += Ring(p, 0.34f, 0.014f, 3.0f,  0.11f, 0.16f, t) * 1.1f;  // 3 本の弧
    rings += Ring(p, 0.26f, 0.010f, 0.0f,  0.00f, 0.0f, t) * 0.7f;   // 内周の実線

    color += PaletteWarm(0.62f) * rings * 1.5f;

    // --- ルーン -------------------------------------------------------------
    float runes = Runes(p, 0.565f, 24.0f, 0.02f, t);
    color += float3(1.00f, 0.85f, 0.55f) * runes * 1.8f;

    // --- 中心の光 -----------------------------------------------------------
    float core = 0.012f / max(r, 0.02f);
    color += float3(0.55f, 0.80f, 1.00f) * core * (0.8f + 0.2f * sin(t * 3.0f));

    color = color / (1.0f + color);

    return LabOutput(color);
}
