//=============================================================================
// 17_fire.hlsl
//   炎 : ノイズを上へ流し、色温度で塗る。
//
//   「下ほど熱い」という 1 つの前提と、上へ流れるノイズ。
//   それだけで炎に見える。色は黒体放射をなぞった段階的な補間で作る。
//   解説 : docs/shader-lab/17_炎.md
//=============================================================================
#include "LabCommon.hlsli"

/// 温度（0〜1）を色に変える。黒 → 赤 → 橙 → 黄 → 白。
///   実際の黒体放射をなぞった、段階的な補間。
float3 FireColor(float temperature)
{
    float t = saturate(temperature);

    float3 color = lerp(float3(0.00f, 0.00f, 0.00f),
                        float3(0.65f, 0.06f, 0.01f), smoothstep(0.00f, 0.25f, t));
    color = lerp(color, float3(1.00f, 0.32f, 0.03f), smoothstep(0.25f, 0.50f, t));
    color = lerp(color, float3(1.00f, 0.75f, 0.15f), smoothstep(0.50f, 0.72f, t));
    color = lerp(color, float3(1.00f, 0.96f, 0.80f), smoothstep(0.72f, 0.92f, t));

    return color;
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    // ★ 左右で別々の絵を出すので、それぞれの中で座標を取り直す。
    //   画面全体の座標をそのまま使うと、炎が半分の端に寄ってしまう。
    float2 local = float2(frac(uv.x * 2.0f), uv.y);

    float2 p;
    p.x = local.x * 2.0f - 1.0f;
    p.y = -(local.y * 2.0f - 1.0f);
    p.x *= (g_resolution.x * 0.5f) * g_resolution.w;   // 半分の幅 / 高さ

    // 画面下を炎の根元にする
    float2 q = float2(p.x, p.y + 0.95f);

    float3 color;

    if (uv.x < 0.5f)
    {
        // --- 左 : 炎 --------------------------------------------------------

        // (1) 上へ流れるノイズ。座標から時間を引くと上へ動く。
        //     細かい成分ほど速く流すと、揺らぎが自然になる。
        float2 flow = float2(q.x * 1.6f, q.y * 1.1f - t * 1.30f);
        float n1 = Fbm(flow, 5);

        float2 flow2 = float2(q.x * 3.1f + 12.0f, q.y * 2.2f - t * 2.10f);
        float n2 = Fbm(flow2, 4);

        float turbulence = n1 * 0.65f + n2 * 0.35f;

        // (2) 「下ほど熱い」という形。上へ行くほど細く、弱くなる。
        //     横方向は中心から離れるほど冷たい。
        float height = saturate(q.y * 0.75f);
        float width  = 0.55f * (1.0f - height * 0.75f);
        float shape  = smoothstep(width, 0.0f, abs(q.x));

        //     根元は明るく、先端へ向かって落とす
        float falloff = 1.0f - smoothstep(0.05f, 1.35f, q.y);

        // (3) 温度 = 形 × 減衰 × 揺らぎ
        float temperature = shape * falloff * (0.55f + 0.95f * turbulence);

        // 先端をちぎるため、揺らぎでしきい値を上げる
        temperature -= height * height * 0.35f;

        color = FireColor(temperature * 1.35f);

        // (4) まわりに熱の光をにじませる
        float glow = shape * falloff * 0.35f;
        color += float3(0.55f, 0.18f, 0.04f) * glow * glow;
    }
    else
    {
        // --- 右 : 中身を分解して見せる --------------------------------------
        float2 r = local;
        int    band = min((int)(r.y * 3.0f), 2);

        float2 flow = float2(q.x * 1.6f, q.y * 1.1f - t * 1.30f);

        if (band == 0)
        {
            // 上 : 流れるノイズそのもの
            float n = Fbm(flow, 5);
            color = float3(n, n, n);
        }
        else if (band == 1)
        {
            // 中 : 炎の「形」だけ
            float height = saturate(q.y * 0.75f);
            float width  = 0.55f * (1.0f - height * 0.75f);
            float shape  = smoothstep(width, 0.0f, abs(q.x));
            float falloff = 1.0f - smoothstep(0.05f, 1.35f, q.y);
            float v = shape * falloff;
            color = float3(v, v, v);
        }
        else
        {
            // 下 : 色の対応表（温度 0〜1）
            color = FireColor(frac(r.x));
        }

        color = lerp(color, float3(0.95f, 0.55f, 0.30f),
                     DividerMask(r.y, 3.0f, 0.006f));
    }

    // 左右の境目
    color = lerp(color, float3(0.95f, 0.55f, 0.30f),
                 1.0f - smoothstep(0.0f, 0.002f, abs(uv.x - 0.5f)));

    return LabOutput(color);
}
