//=============================================================================
// 32_explosion.hlsl
//   爆発 : 火球・煙・破片・衝撃波を時間差で重ねる。
//
//   VFX は「1 つの表現」ではなく「時間差で重なる複数の層」でできている。
//   どの層をいつ出すか、を決めるのが設計のほとんど。
//   解説 : docs/shader-lab/32_爆発.md
//=============================================================================
#include "LabCommon.hlsli"

/// 温度から色（17 番と同じ考え方）。
float3 FireColor(float temperature)
{
    float t = saturate(temperature);

    float3 color = lerp(float3(0.0f, 0.0f, 0.0f),
                        float3(0.75f, 0.08f, 0.01f), smoothstep(0.00f, 0.22f, t));
    color = lerp(color, float3(1.00f, 0.34f, 0.03f), smoothstep(0.22f, 0.48f, t));
    color = lerp(color, float3(1.00f, 0.78f, 0.16f), smoothstep(0.48f, 0.72f, t));
    color = lerp(color, float3(1.00f, 0.97f, 0.85f), smoothstep(0.72f, 0.94f, t));

    return color;
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 p = ToCenteredUv(input.uv);
    float  t = g_time.x;

    // 2.4 秒ごとに 1 発。
    const float period = 2.4f;
    float age = frac(t / period) * period;

    float3 color = float3(0.015f, 0.016f, 0.022f);

    float r = length(p);
    float angle = atan2(p.y, p.x);

    // ================= 層 1 : 閃光（0.00〜0.12 秒）=======================
    //   いちばん最初に、いちばん明るく、いちばん短く。
    {
        float flash = exp(-age * 22.0f);
        color += float3(1.0f, 0.95f, 0.85f) * flash * 0.022f / max(r, 0.02f);
    }

    // ================= 層 2 : 火球（0.00〜0.9 秒）=========================
    //   広がりながら冷めていく。輪郭はノイズでちぎる。
    {
        float radius = 0.10f + 0.62f * (1.0f - exp(-age * 3.4f));

        // 輪郭を乱す。角度を入力にすると、放射状にちぎれる。
        float wobble = Fbm(float2(angle * 2.2f, age * 0.8f) * 2.0f, 4) - 0.5f;
        float edge = radius * (1.0f + wobble * 0.55f);

        float core = smoothstep(edge, edge * 0.35f, r);

        // 内側ほど熱い。時間とともに全体が冷める。
        float temperature = core * (1.35f - age * 1.15f);

        // 内部の乱れ
        temperature *= 0.70f + 0.60f * Fbm(p * 5.0f + float2(0.0f, -age * 1.6f), 4);

        color += FireColor(temperature) * saturate(1.4f - age * 1.5f);
    }

    // ================= 層 3 : 衝撃波（0.05〜0.7 秒）=======================
    {
        float radius = age * 1.9f;
        float ring = 1.0f - saturate(abs(r - radius) / 0.05f);
        ring = pow(ring, 2.0f) * exp(-age * 3.2f);

        color += float3(0.95f, 0.97f, 1.00f) * ring * 0.75f;
    }

    // ================= 層 4 : 破片（0.05〜1.6 秒）=========================
    //   火球より速く飛び、重力で落ちる。
    {
        const int kDebrisCount = 26;

        for (int i = 0; i < kDebrisCount; ++i)
        {
            float fi = (float)i;
            float2 seed = Hash22(float2(fi, 1.0f));

            float a = seed.x * kTau;
            float speed = 0.9f + seed.y * 1.5f;

            // 位置 = 速度 × 時間 − 重力 × 時間²
            float2 velocity = float2(cos(a), sin(a)) * speed;
            float2 position = velocity * age
                            - float2(0.0f, 1.35f) * age * age;

            float size = 0.008f + 0.010f * seed.y;
            float d = length(p - position);

            float spark = size / max(d, 0.002f);
            spark *= exp(-age * 2.4f);

            color += FireColor(0.85f - age * 0.4f) * spark * 0.55f;
        }
    }

    // ================= 層 5 : 煙（0.3〜2.6 秒）============================
    //   いちばん遅く出て、いちばん長く残る。上へ昇る。
    {
        float smokeAge = max(age - 0.25f, 0.0f);
        float rise = smokeAge * 0.30f;

        float2 q = (p - float2(0.0f, rise)) * (1.6f - smokeAge * 0.35f);

        float density = Fbm(q * 2.2f + float2(0.0f, -smokeAge * 0.5f), 5);

        // ★ 減衰を掛けると平均 0.5 の値がさらに下がる。
        //   後段のしきい値をそのままにすると、煙がまったく出ない。
        density *= smoothstep(1.30f, 0.20f, length(q));

        float smoke = smoothstep(0.30f, 0.62f, density);

        // 出て、残って、薄れる
        float fade = smoothstep(0.0f, 0.30f, smokeAge)
                   * smoothstep(period - 0.15f, period * 0.60f, age);

        // 下側ほど熱の名残で明るい。
        //   ★ 煙は「暗い色」だと思って値を下げすぎると、暗い背景に埋もれる。
        //     見せたいのは形なので、背景よりはっきり明るくする。
        float warm = saturate(0.6f - smokeAge * 0.7f);
        float3 smokeColor = lerp(float3(0.42f, 0.41f, 0.45f),
                                 float3(0.95f, 0.48f, 0.18f), warm);

        color = lerp(color, smokeColor, smoke * fade * 0.95f);
    }

    color = color / (1.0f + color);

    return LabOutput(color);
}
