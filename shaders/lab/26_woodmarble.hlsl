//=============================================================================
// 26_woodmarble.hlsl
//   木目と大理石 : sin の中にノイズを入れる。
//
//   どちらも「規則正しい縞」を「ノイズで乱した」もの。
//   縞の形（同心円か直線か）と乱し方の強さだけが違う。
//   解説 : docs/shader-lab/26_木目と大理石.md
//=============================================================================
#include "LabCommon.hlsli"

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv);

    float3 color;

    if (uv.x < 0.5f)
    {
        // ================= 木目 =============================================
        //   年輪は「中心からの同心円」。それをノイズで少しだけ乱す。

        // 左半分の中で座標を取り直す
        float2 q = float2((uv.x / 0.5f) * 2.0f - 1.0f, -(uv.y * 2.0f - 1.0f));
        q.x *= (g_resolution.x * 0.5f) * g_resolution.w;

        // 年輪の中心は画面の外に置く。板は幹の一部を切り出したものなので、
        // 中心が画面内にあることは少ない。
        float2 center = float2(-1.15f, 0.10f);
        float2 r = q - center;

        // 幹は縦に長いので、縦方向を縮めて楕円にする
        r.y *= 0.62f;

        float radius = length(r);

        // ★ 同心円をノイズで乱す。ここが木目の肝。
        //   乱しすぎると大理石になってしまうので、控えめにする。
        float distortion = Fbm(q * 1.9f + float2(0.0f, t * 0.02f), 5) * 0.95f;

        float rings = radius * 7.5f + distortion;

        // 年輪は「急に濃くなり、ゆっくり薄くなる」。frac を累乗すると近づく。
        float grain = frac(rings);
        grain = pow(grain, 0.45f);

        // 細かい導管（縦に走る筋）
        float pores = Fbm(float2(q.x * 40.0f, q.y * 2.0f), 3);
        grain *= 0.88f + 0.12f * pores;

        float3 light = float3(0.72f, 0.50f, 0.30f);
        float3 dark  = float3(0.32f, 0.17f, 0.08f);

        color = lerp(dark, light, grain);

        // 表面のつや
        color *= 0.90f + 0.20f * Fbm(q * 8.0f, 3);
    }
    else
    {
        // ================= 大理石 ===========================================
        //   縞は「直線」。こちらは大きく乱す。

        float2 q = float2((frac(uv.x * 2.0f)) * 2.0f - 1.0f, -(uv.y * 2.0f - 1.0f));
        q.x *= (g_resolution.x * 0.5f) * g_resolution.w;

        // ★ sin の中にノイズを入れる、が基本形。
        //   turbulence の強さで、縞から渦へ連続的に変わる。
        float turbulence = Fbm(q * 1.2f + float2(t * 0.015f, 0.0f), 6);

        float veins = sin((q.x + q.y * 0.35f) * 2.6f + turbulence * 6.5f);

        // 脈を細く鋭くする。abs して累乗すると、線が締まる。
        float vein = pow(1.0f - abs(veins), 3.0f);

        float3 stone = float3(0.86f, 0.85f, 0.82f);
        float3 veinDark = float3(0.28f, 0.30f, 0.34f);

        color = lerp(stone, veinDark, saturate(vein * 0.95f));

        // 2 本目の脈（細く、色違い）
        float veins2 = sin((q.x * 1.7f - q.y * 0.9f) * 4.1f + turbulence * 9.0f);
        float vein2  = pow(1.0f - abs(veins2), 8.0f);
        color = lerp(color, float3(0.55f, 0.42f, 0.35f), saturate(vein2 * 0.7f));

        // 石の地の細かいむら
        color *= 0.94f + 0.10f * Fbm(q * 14.0f, 3);
    }

    // 左右の境目
    color = lerp(color, float3(0.20f, 0.21f, 0.25f),
                 1.0f - smoothstep(0.0f, 0.002f, abs(uv.x - 0.5f)));

    return LabOutput(color);
}
