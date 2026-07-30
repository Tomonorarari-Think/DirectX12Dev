//=============================================================================
// 33_trail.hlsl
//   軌跡 : 「過去の位置」を式で表す。
//
//   履歴を持たずに軌跡を描く。動きが式で書けるなら、
//   時間を巻き戻した位置も式で求まる。
//   解説 : docs/shader-lab/33_軌跡.md
//=============================================================================
#include "LabCommon.hlsli"

/// 時刻 time における、飛んでいる物の位置。
///   ★ ここが「式で書けている」ことが肝。
///     過去の位置を知りたければ、時刻を巻き戻して呼ぶだけでよい。
float2 PathPosition(float time, float seed)
{
    float a = time * 1.15f + seed * 2.4f;

    // リサージュ図形。周期の比が有理数でないほど、軌道が閉じにくい。
    return float2(sin(a * 1.00f) * 1.05f,
                  sin(a * 1.37f + seed) * 0.62f);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 p = ToCenteredUv(input.uv);
    float  t = g_time.x;

    float3 color = float3(0.02f, 0.025f, 0.04f);

    const int kTrailCount = 3;

    for (int k = 0; k < kTrailCount; ++k)
    {
        float seed = (float)k * 2.11f;
        float3 tint = PaletteWarm(0.15f + (float)k * 0.28f);

        // --- 尾を描く -------------------------------------------------------
        //   過去 N 個の位置に、だんだん小さく暗い点を置く。
        // ★ 点の間隔が空くと、尾が破線に見える。
        //   速く動くほど枚数が要る。ここは「見て決める」ところ。
        const int kSamples = 96;
        const float span = 1.05f;   // 何秒ぶんさかのぼるか

        for (int i = 0; i < kSamples; ++i)
        {
            float age = (float)i / kSamples;          // 0 = いま、1 = いちばん古い
            float2 past = PathPosition(t - age * span, seed);

            float d = length(p - past);

            // 古いほど細く、暗く。
            float size    = lerp(0.030f, 0.004f, age);
            float bright  = pow(1.0f - age, 2.2f);

            // ★ 枚数を増やしたら、1 枚ぶんの重みは同じだけ下げる。
            //   そうしないと尾の明るさが枚数に比例して増えてしまう。
            color += tint * (size / max(d, 0.003f)) * bright * 0.042f;
        }

        // --- 先頭の玉 -------------------------------------------------------
        float2 head = PathPosition(t, seed);
        float dh = length(p - head);

        color += float3(1.0f, 0.97f, 0.92f) * 0.014f / max(dh, 0.006f);
        color += tint * 0.030f / max(dh, 0.02f);
    }

    // --- 残像を薄く広げる（にじみ）-----------------------------------------
    //   実際のブルームの代わり。習作は後処理を通らないので自前で足す。
    color += pow(color, 1.6f) * 0.35f;

    color = color / (1.0f + color);

    return LabOutput(color);
}
