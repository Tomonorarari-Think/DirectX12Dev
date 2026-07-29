//=============================================================================
// 05_fbm.hlsl
//   細かさを変えたノイズを重ねる（fBm）。
//
//   自然物は「大きなうねりの上に細かいざらつきが乗っている」。
//   2 倍細かく・半分の強さで、を繰り返すだけでそれらしくなる。
//   解説 : docs/shader-lab/05_fBmと雲.md
//=============================================================================
#include "LabCommon.hlsli"

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 centered = ToCenteredUv(uv);

    // 空のグラデーション（上ほど濃い青）
    float3 sky = lerp(float3(0.62f, 0.74f, 0.90f),
                      float3(0.14f, 0.32f, 0.68f),
                      saturate(centered.y * 0.7f + 0.35f));

    // 雲は横へ流れる。座標をずらすだけで動く。
    float2 p = centered * 2.0f + float2(t * 0.05f, 0.0f);

    // --- fBm ----------------------------------------------------------------
    //   1 段ごとに周波数 2 倍・振幅 1/2。合計は 1 に収束する。
    float density = Fbm(p, 6);

    // 上のほうほど雲が多い、という重み付け。
    density *= 0.55f + 0.75f * smoothstep(-0.5f, 0.9f, centered.y);

    // しきい値で「雲がある / ない」を切り分ける。
    //   境目を smoothstep でぼかすと、ふわっとした縁になる。
    float coverage = smoothstep(0.38f, 0.66f, density);

    // --- 雲の陰影 -----------------------------------------------------------
    //   密度の勾配を「傾き」とみなして光を当てる。
    //   ★ 勾配を取るときは段数を減らし、間隔も広めにする。
    //     細かい段まで差分に入れると、格子状の筋が出る。
    const float e = 0.06f;
    float dx = Fbm(p + float2(e, 0.0f), 3) - Fbm(p - float2(e, 0.0f), 3);
    float dy = Fbm(p + float2(0.0f, e), 3) - Fbm(p - float2(0.0f, e), 3);

    float3 normal  = normalize(float3(-dx, -dy, 0.45f));
    float3 toLight = normalize(float3(-0.50f, 0.60f, 0.62f));
    float  lighting = saturate(dot(normal, toLight)) * 0.55f + 0.60f;

    float3 cloud = float3(1.0f, 0.99f, 0.97f) * lighting;

    float3 color = lerp(sky, cloud, coverage);

    // --- 下の帯 : 段ごとの中身を並べて見せる -------------------------------
    if (uv.y > 0.76f)
    {
        float2 q = float2(uv.x, (uv.y - 0.76f) / 0.24f);

        int   index = min((int)(q.x * 5.0f), 4);
        float scale     = pow(2.0f, (float)index);
        float amplitude = pow(0.5f, (float)index + 1.0f);

        // その段だけを取り出す（周波数 2^i、振幅 1/2^(i+1)）。
        //   見やすいよう振幅で割り戻して 0〜1 に伸ばす。
        float single = ValueNoise(p * scale);

        // 左 4 つが「各段」、いちばん右が「全部を足した結果」。
        float value = (index < 4) ? single : Fbm(p, 6);

        color = float3(value, value, value);
        color = lerp(color, PaletteWarm(0.62f) * value, (index == 4) ? 0.55f : 0.0f);

        color = lerp(color, float3(0.95f, 0.55f, 0.30f),
                     DividerMask(q.x, 5.0f, 0.006f));
        color = lerp(color, float3(0.95f, 0.55f, 0.30f), FrameMask(q, 0.012f));
    }

    return LabOutput(color);
}
