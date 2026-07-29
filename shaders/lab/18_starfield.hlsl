//=============================================================================
// 18_starfield.hlsl
//   星空と星雲 : 点を「マスに 1 個」置いて、にじませる。
//
//   星は 03 番の繰り返しで作り、星雲は 07 番のドメインワープで作る。
//   これまでの部品の組み合わせで、1 枚の絵になる。
//   解説 : docs/shader-lab/18_星空.md
//=============================================================================
#include "LabCommon.hlsli"

/// 1 層ぶんの星。マスごとに 1 個、位置と明るさをハッシュで決める。
float3 StarLayer(float2 p, float time, float density, float brightness)
{
    float2 cell  = floor(p);
    float2 local = frac(p) - 0.5f;

    float3 sum = float3(0.0f, 0.0f, 0.0f);

    // 隣のマスの星も光をこぼすので、3x3 を見る。
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y);
            float2 seed = Hash22(cell + offset);

            // density より大きい種のマスには星を置かない
            if (Hash21(cell + offset + 7.7f) > density)
            {
                continue;
            }

            // マスの中の位置（端に寄りすぎないよう 0.7 倍）
            float2 position = offset + (seed - 0.5f) * 0.7f - local;

            float distance = length(position);

            // 明るさは 1/r。中心が鋭く、裾が長く伸びる。
            float star = brightness * 0.014f / max(distance, 0.001f);

            // またたき。星ごとに位相をずらす。
            float twinkle = 0.65f + 0.35f * sin(time * 2.2f + seed.x * kTau);
            star *= twinkle;

            // 色。青白い星と赤い星を混ぜる。
            float3 tint = lerp(float3(0.70f, 0.80f, 1.00f),
                               float3(1.00f, 0.78f, 0.62f), seed.y);

            sum += tint * star;
        }
    }

    return sum;
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv);

    // --- (1) 星雲 -----------------------------------------------------------
    //   ドメインワープした fBm を、2 色で塗る。
    float2 warp = float2(Fbm(p * 1.4f + t * 0.010f, 4),
                         Fbm(p * 1.4f + float2(3.1f, 8.2f) - t * 0.008f, 4));

    float nebula = Fbm(p * 1.8f + 2.2f * warp, 5);

    // しきい値より下は真っ黒（宇宙）にする
    float cloud = smoothstep(0.36f, 0.78f, nebula);

    float3 nebulaColor = lerp(float3(0.08f, 0.05f, 0.22f),
                              float3(0.55f, 0.18f, 0.42f), cloud);
    nebulaColor = lerp(nebulaColor, float3(0.25f, 0.55f, 0.85f),
                       smoothstep(0.60f, 0.95f, nebula) * 0.7f);

    float3 color = nebulaColor * cloud * 1.45f;

    // 下地の暗い青
    color += float3(0.015f, 0.018f, 0.045f);

    // --- (2) 星を 3 層 ------------------------------------------------------
    //   細かい層ほど数を多く・暗く。奥行きが出る。
    color += StarLayer(p * 12.0f + 3.0f,  t, 0.45f, 0.55f);
    color += StarLayer(p *  7.0f + 17.0f, t, 0.30f, 1.00f);
    color += StarLayer(p *  4.0f + 41.0f, t, 0.18f, 1.80f);

    // --- (3) いちばん明るい星に十字の光条を足す ----------------------------
    //   レンズの絞り羽根で起きる回折。縦横に細く伸ばすだけでそれらしい。
    {
        float2 q = p * 4.0f + 41.0f;
        float2 cell  = floor(q);
        float2 local = frac(q) - 0.5f;

        float2 seed = Hash22(cell);
        if (Hash21(cell + 7.7f) < 0.06f)
        {
            float2 d = local - (seed - 0.5f) * 0.7f;

            float cross1 = 0.0035f / max(abs(d.x) * 8.0f + abs(d.y) * 0.25f, 0.001f)
                         + 0.0035f / max(abs(d.y) * 8.0f + abs(d.x) * 0.25f, 0.001f);

            color += float3(0.85f, 0.90f, 1.0f) * cross1;
        }
    }

    // --- (4) 軽いトーンマッピング ------------------------------------------
    color = color / (1.0f + color);

    return LabOutput(color);
}
