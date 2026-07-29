//=============================================================================
// 19_metaball.hlsl
//   メタボール : 「場」を足してからしきい値で切る。
//
//   図形を足すのではなく、点のまわりに広がる「影響力」を足す。
//   近づいた玉どうしが、水銀のように融合する。
//   解説 : docs/shader-lab/19_メタボール.md
//=============================================================================
#include "LabCommon.hlsli"

/// 1 個ぶんの「場」。距離が近いほど大きい。
///   1/r² を使うのが古典的。裾が長く伸びるので、離れていても影響が残る。
float Field(float2 p, float2 center, float strength)
{
    float2 d = p - center;
    return strength / (dot(d, d) + 0.02f);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv);

    // --- 玉の位置。それぞれ違う速さで回る ----------------------------------
    const int kBallCount = 6;

    float2 centers[kBallCount];
    float  strengths[kBallCount];

    [unroll]
    for (int i = 0; i < kBallCount; ++i)
    {
        float fi = (float)i;
        float speed  = 0.35f + fi * 0.11f;
        float radius = 0.30f + 0.42f * frac(fi * 0.37f);
        float phase  = fi * 2.39f;   // 黄金角に近い値。重なりにくい

        centers[i] = float2(cos(t * speed + phase) * radius * 1.7f,
                            sin(t * speed * 1.3f + phase) * radius);
        strengths[i] = 0.045f + 0.030f * frac(fi * 0.61f);
    }

    // --- (1) 場を足し合わせる ----------------------------------------------
    float field = 0.0f;

    [unroll]
    for (int j = 0; j < kBallCount; ++j)
    {
        field += Field(p, centers[j], strengths[j]);
    }

    float3 color;

    if (uv.y < 0.72f)
    {
        // --- (2) しきい値で切る --------------------------------------------
        //   ★ ここが「足してから切る」の肝。
        //     図形どうしを min で繋ぐと接触するまで別々だが、
        //     場を足すと、近づいただけで境界が引き合う。
        const float threshold = 1.0f;

        // 境目の傾きを使って、太さの揃った縁を作る
        float edge = fwidth(field);
        float mask = smoothstep(threshold - edge, threshold + edge, field);

        // 場の値そのものを色に使うと、内部に濃淡が出る
        float3 inside = PaletteWarm(saturate(field * 0.22f) + 0.35f);

        color = lerp(float3(0.05f, 0.06f, 0.09f), inside, mask);

        // 輪郭線
        float outline = 1.0f - smoothstep(0.0f, edge * 2.5f,
                                          abs(field - threshold));
        color = lerp(color, float3(0.98f, 0.96f, 0.90f), outline * 0.65f);
    }
    else
    {
        // --- 下の帯 : 場そのものを等高線で見せる ----------------------------
        float bands = frac(field * 2.5f);
        float shade = saturate(field * 0.55f);

        color = lerp(float3(0.05f, 0.06f, 0.09f),
                     float3(0.30f, 0.55f, 0.85f), shade);
        color *= 0.55f + 0.45f * bands;

        // しきい値の等高線を強調
        float edge = fwidth(field);
        float level = 1.0f - smoothstep(0.0f, edge * 2.0f, abs(field - 1.0f));
        color = lerp(color, float3(0.98f, 0.70f, 0.25f), level);

        color = lerp(color, float3(0.95f, 0.55f, 0.30f),
                     1.0f - smoothstep(0.0f, 0.003f, abs(uv.y - 0.72f)));
    }

    return LabOutput(color);
}
