//=============================================================================
// 14_effects.hlsl
//   画面効果の詰め合わせ : 走査線・色収差・樽型歪み・ノイズ。
//
//   25 章のポストプロセスで使う道具を、絵を作りながら並べて確かめる。
//   どれも「読む位置をずらす」「明るさを掛ける」だけでできている。
//   解説 : docs/shader-lab/14_画面効果.md
//=============================================================================
#include "LabCommon.hlsli"

/// 加工の元になる絵。ここでは市松とリングを合成して作る。
float3 SourceImage(float2 uv, float time)
{
    float2 p = ToCenteredUv(uv);

    // 市松
    float2 grid = floor(uv * 12.0f);
    float checker = fmod(grid.x + grid.y, 2.0f);
    float3 color = lerp(float3(0.12f, 0.14f, 0.18f),
                        float3(0.55f, 0.60f, 0.68f), checker);

    // 回るリング
    float ring = abs(length(p) - (0.45f + 0.06f * sin(time * 1.5f)));
    color = lerp(color, float3(0.98f, 0.72f, 0.25f),
                 smoothstep(0.055f, 0.02f, ring));

    // 中心の円
    color = lerp(color, float3(0.30f, 0.85f, 0.95f),
                 smoothstep(0.20f, 0.185f, length(p)));

    return color;
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    // 画面を 4 つに割り、効果を 1 つずつ見せる。
    bool right  = uv.x >= 0.5f;
    bool bottom = uv.y >= 0.5f;

    float3 color;

    if (!right && !bottom)
    {
        // --- 左上 : 元の絵 ---------------------------------------------------
        color = SourceImage(uv, t);
    }
    else if (right && !bottom)
    {
        // --- 右上 : 色収差 ---------------------------------------------------
        //   レンズは波長ごとに曲がり方が違う。R と B を中心から
        //   逆向きにずらすだけで、それらしく見える。
        float2 center = uv - 0.5f;
        float  amount = 0.006f * (0.4f + length(center) * 2.0f);

        float r = SourceImage(uv + center * amount, t).r;
        float g = SourceImage(uv, t).g;
        float b = SourceImage(uv - center * amount, t).b;

        color = float3(r, g, b);
    }
    else if (!right && bottom)
    {
        // --- 左下 : 樽型歪み ＋ 走査線 ---------------------------------------
        //   歪み : 中心からの距離の 2 乗で外へ押し出すと、樽型になる。
        float2 center = (uv - 0.5f) * 2.0f;
        float  r2 = dot(center, center);
        float2 warped = center * (1.0f + 0.14f * r2) * 0.5f + 0.5f;

        if (warped.x < 0.0f || warped.x > 1.0f ||
            warped.y < 0.0f || warped.y > 1.0f)
        {
            color = float3(0.02f, 0.02f, 0.03f);
        }
        else
        {
            color = SourceImage(warped, t);

            // 走査線 : 縦方向に細かい明暗を掛けるだけ。
            float scan = 0.88f + 0.12f * sin(uv.y * g_resolution.y * 1.6f);
            color *= scan;

            // 画面の湾曲に合わせて四隅を落とす
            color *= 1.0f - 0.35f * saturate(r2 - 0.35f);
        }
    }
    else
    {
        // --- 右下 : 時間で変わるノイズ（フィルムグレイン）--------------------
        color = SourceImage(uv, t);

        // 時間を足すと毎フレーム違う値になり、ざらつきが動く。
        float grain = Hash21(uv * g_resolution.xy + frac(t) * 917.0f);

        // 暗い所ほどノイズが目立つ、という実際のフィルムの性質に寄せる。
        float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        float amount = 0.16f * (1.0f - luminance);

        color += (grain - 0.5f) * amount;

        // 一定間隔で走る横帯（トラッキングのずれ）
        float bandY = frac(t * 0.35f);
        float band = smoothstep(0.02f, 0.0f, abs(uv.y - bandY));
        color = lerp(color, color * 1.6f + 0.05f, band);
    }

    // 仕切り
    float2 border = abs(uv - 0.5f);
    float sep = 1.0f - smoothstep(0.0f, 0.0015f, min(border.x, border.y));
    color = lerp(color, float3(0.95f, 0.55f, 0.30f), sep);

    return LabOutput(color);
}
