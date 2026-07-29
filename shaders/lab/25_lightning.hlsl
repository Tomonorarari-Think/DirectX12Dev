//=============================================================================
// 25_lightning.hlsl
//   稲妻 : まっすぐな線を、ノイズで折り曲げる。
//
//   稲妻は「線分までの距離」を、fBm で歪ませたものにすぎない。
//   細かい枝は、同じ処理を小さくして重ねる。
//   解説 : docs/shader-lab/25_稲妻.md
//=============================================================================
#include "LabCommon.hlsli"

/// 1 本ぶんの稲妻。始点と終点を結ぶ線を、ノイズで横へずらす。
///   戻り値は「明るさ」。距離の逆数なので、中心が鋭く裾が長い。
float Bolt(float2 p, float2 a, float2 b, float seed, float time,
           float wobble, float thickness)
{
    float2 dir = b - a;
    float  len = length(dir);
    dir /= len;

    // 線に沿った座標（0〜1）と、線から離れた距離
    float2 rel = p - a;
    float  along = dot(rel, dir) / len;
    float  side  = dot(rel, float2(-dir.y, dir.x));

    // ★ 線の「横へのずれ」をノイズで作る。
    //   along（線に沿った位置）だけを入力にするので、
    //   線全体が 1 本の折れ線として歪む。
    float n = 0.0f;
    n += (Fbm(float2(along * 3.0f, seed) + time * 0.9f, 4) - 0.5f) * 1.00f;
    n += (Fbm(float2(along * 9.0f, seed + 5.0f) + time * 1.7f, 3) - 0.5f) * 0.35f;
    n += (Fbm(float2(along * 26.0f, seed + 9.0f) + time * 3.1f, 2) - 0.5f) * 0.12f;

    // 両端は固定したいので、中央ほど大きく揺らす
    float envelope = sin(saturate(along) * kPi);
    side -= n * wobble * envelope;

    // 線分の外側は減衰させる
    float outside = max(max(-along, along - 1.0f), 0.0f);
    float d = sqrt(side * side + outside * outside * 4.0f);

    return thickness / max(d, 0.0008f);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv);

    // --- 明滅 ---------------------------------------------------------------
    //   稲妻は一瞬だけ光る。時間を階段状にして、たまに強く光らせる。
    float period = floor(t * 1.3f);
    float local  = frac(t * 1.3f);

    float strike = Hash21(float2(period, 2.7f));
    float flash  = (strike > 0.18f)
                 ? exp(-local * 4.0f) * (0.85f + 0.55f * Hash21(float2(period, 9.1f)))
                 : 0.18f;

    // 一度光ってから、少し遅れてもう一度光る（実際の落雷に近い）
    flash += (strike > 0.55f) ? exp(-abs(local - 0.22f) * 18.0f) * 0.75f : 0.0f;

    // ★ 完全に消さない。資料用に、いつ撮っても放電の形が見えるようにする。
    //   実際の落雷らしさを優先するなら下限を 0 にしてよい。
    flash = max(flash, 0.55f);

    float3 color = float3(0.010f, 0.012f, 0.030f);

    // --- 主放電 -------------------------------------------------------------
    float2 top    = float2(-0.35f + 0.5f * (Hash21(float2(period, 4.4f)) - 0.5f), 1.05f);
    float2 bottom = float2( 0.25f + 0.7f * (Hash21(float2(period, 6.6f)) - 0.5f), -0.95f);

    float main1 = Bolt(p, top, bottom, period, t, 0.42f, 0.0090f);

    // --- 枝 -----------------------------------------------------------------
    //   主放電の途中から分かれる。短く、細く、暗く。
    float branches = 0.0f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float fi = (float)i;
        float u  = 0.22f + fi * 0.19f;              // 分岐する位置
        float2 start = lerp(top, bottom, u);

        float angle = (Hash21(float2(period, fi * 3.3f)) - 0.5f) * 2.2f;
        float length1 = 0.35f + 0.30f * Hash21(float2(period, fi * 7.1f));

        float2 end = start + float2(sin(angle), -cos(angle) * 0.6f) * length1;

        branches += Bolt(p, start, end, period + fi * 13.0f, t,
                         0.18f, 0.0040f) * 0.7f;
    }

    float glow = main1 + branches;

    // --- 色 -----------------------------------------------------------------
    //   芯は白、まわりは青紫。距離の逆数を 2 段階で使う。
    float3 core = float3(1.00f, 0.98f, 0.95f) * pow(saturate(glow * 0.55f), 2.2f);
    float3 halo = float3(0.35f, 0.45f, 1.00f) * saturate(glow * 0.28f);

    color += (core + halo) * flash;

    // --- 空が一瞬明るくなる -------------------------------------------------
    color += float3(0.10f, 0.13f, 0.26f) * flash * 0.45f;

    // 雲（背景）
    float clouds = Fbm(p * 1.5f + float2(t * 0.02f, 0.0f), 5);
    color += float3(0.06f, 0.07f, 0.12f) * smoothstep(0.45f, 0.85f, clouds)
           * (0.25f + flash * 1.6f);

    color = color / (1.0f + color);

    return LabOutput(color);
}
