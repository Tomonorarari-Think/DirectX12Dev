//=============================================================================
// 21_glitch.hlsl
//   グリッチ : わざと壊す。ずらす・量子化する・入れ替える。
//
//   「時間を階段状にする」のが肝。毎フレーム変えると、ただのノイズになる。
//   一定時間おきに切り替えると、機械が壊れているように見える。
//   解説 : docs/shader-lab/21_グリッチ.md
//=============================================================================
#include "LabCommon.hlsli"

/// 加工の元になる絵。
float3 SourceImage(float2 uv, float time)
{
    float2 p = ToCenteredUv(uv);

    // 同心円と放射
    float radius = length(p);
    float angle  = atan2(p.y, p.x) / kTau + 0.5f;

    float3 color = PaletteWarm(radius * 0.6f - time * 0.03f);
    color *= 0.65f + 0.35f * cos(angle * kTau * 8.0f);

    // 中央の四角
    color = lerp(color, float3(0.06f, 0.07f, 0.10f),
                 FillMask(SdRoundedBox(p, float2(0.55f, 0.30f), 0.08f)));
    color = lerp(color, float3(0.95f, 0.92f, 0.85f),
                 StrokeMask(SdRoundedBox(p, float2(0.55f, 0.30f), 0.08f), 0.012f));

    return color;
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    // --- (1) 時間を階段状にする ---------------------------------------------
    //   ★ ここが肝。毎フレーム乱数を引くと「砂嵐」にしかならない。
    //     一定時間おきに値を切り替えると、壊れた機械らしく見える。
    float step1 = floor(t * 8.0f);        // 1/8 秒ごと
    float step2 = floor(t * 1.7f);        // 約 0.6 秒ごと

    // 「壊れている時間帯」と「正常な時間帯」を作る
    //   常に少しだけ効かせ、ときどき強くする。
    //   完全に 0 にすると「たまたま正常な瞬間」ばかり撮れてしまう。
    float burst = 0.35f + 0.65f * step(0.45f, Hash21(float2(step2, 3.7f)));

    float2 warped = uv;

    // --- (2) 横帯ごとに UV をずらす ----------------------------------------
    {
        // 帯の高さもランダムに変える
        float bandCount = lerp(12.0f, 44.0f, Hash21(float2(step2, 8.1f)));
        float band = floor(uv.y * bandCount);

        float seed = Hash21(float2(band, step1));

        // ほとんどの帯はずらさない。まれに大きくずらす。
        float amount = (seed > 0.62f) ? (Hash21(float2(band, step1 + 5.0f)) - 0.5f)
                                      : 0.0f;

        warped.x += amount * 0.20f * burst;
    }

    // --- (3) ブロック単位で入れ替える --------------------------------------
    {
        float2 blockSize = float2(0.10f, 0.055f);
        float2 block = floor(uv / blockSize);

        float seed = Hash21(block + step1 * 13.0f);

        if (seed > 0.86f && burst > 0.5f)
        {
            float2 shift = (Hash22(block + step1) - 0.5f) * 0.35f;
            warped += shift;
        }
    }

    warped = frac(warped);   // はみ出したら反対側から読む

    // --- (4) チャンネルをずらす（色収差の強い版）---------------------------
    float shift = 0.010f * burst * (0.4f + Hash21(float2(step1, 1.9f)));

    float r = SourceImage(frac(warped + float2( shift, 0.0f)), t).r;
    float g = SourceImage(warped, t).g;
    float b = SourceImage(frac(warped + float2(-shift, 0.0f)), t).b;

    float3 color = float3(r, g, b);

    // --- (5) 色を段階に丸める（ポスタリゼーション）------------------------
    if (burst > 0.5f && Hash21(float2(step1, 4.4f)) > 0.45f)
    {
        float levels = 5.0f;
        color = floor(color * levels + 0.5f) / levels;
    }

    // --- (6) 走査線と、たまに走る白い帯 ------------------------------------
    color *= 0.90f + 0.10f * sin(uv.y * g_resolution.y * 1.5f);

    float lineY = Hash21(float2(step1, 6.3f));
    float flash = smoothstep(0.012f, 0.0f, abs(uv.y - lineY)) * burst;
    color = lerp(color, float3(0.95f, 0.97f, 1.0f), flash * 0.75f);

    return LabOutput(color);
}
