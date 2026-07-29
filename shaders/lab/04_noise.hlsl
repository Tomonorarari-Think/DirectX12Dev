//=============================================================================
// 04_noise.hlsl
//   乱数となめらかなノイズ。
//
//   GPU には乱数の種が無い。「座標から必ず同じ値を作る関数」を
//   自分で用意し、それをなめらかに繋いでノイズにする。
//   解説 : docs/shader-lab/04_ノイズ.md
//=============================================================================
#include "LabCommon.hlsli"

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    // 画面を横に 3 分割し、作り方の違いを並べる。
    float band = uv.x * 3.0f;
    int   which = (int)band;

    float2 p = ToCenteredUv(uv) * 6.0f;
    float value;

    if (which == 0)
    {
        // --- (1) そのままのハッシュ -----------------------------------------
        //   ピクセル 1 個ごとにバラバラの値。砂嵐にしかならない。
        value = Hash21(floor(p * 12.0f));
    }
    else if (which == 1)
    {
        // --- (2) 格子ごとの乱数を「直線で」繋ぐ ------------------------------
        //   繋がりはするが、格子の線がはっきり見える。
        float2 cell  = floor(p);
        float2 local = frac(p);

        float a = Hash21(cell + float2(0.0f, 0.0f));
        float b = Hash21(cell + float2(1.0f, 0.0f));
        float c = Hash21(cell + float2(0.0f, 1.0f));
        float d = Hash21(cell + float2(1.0f, 1.0f));

        value = lerp(lerp(a, b, local.x), lerp(c, d, local.x), local.y);
    }
    else
    {
        // --- (3) 両端で傾きが 0 になる曲線で繋ぐ ----------------------------
        //   3t² - 2t³。格子の線が消え、自然なうねりになる。
        value = ValueNoise(p);
    }

    float3 color = float3(value, value, value);

    // 下の帯 : 補間曲線そのものを描いて見比べる。
    if (uv.y > 0.80f)
    {
        float x = frac(uv.x * 3.0f);
        float y = (uv.y - 0.80f) / 0.20f;
        y = 1.0f - y;   // 上が 1

        float curve = (which == 2) ? (x * x * (3.0f - 2.0f * x)) : x;
        if (which == 0) { curve = step(0.5f, x); }

        float lineMask = 1.0f - smoothstep(0.0f, 0.035f, abs(y - curve));

        color = float3(0.10f, 0.11f, 0.14f);
        color = lerp(color, float3(0.35f, 0.75f, 0.95f), lineMask);

        // 目盛り
        color = lerp(color, float3(0.25f, 0.27f, 0.32f),
                     DividerMask(x, 4.0f, 0.01f) * 0.6f);
    }

    // 帯の境目
    color = lerp(color, float3(0.95f, 0.55f, 0.30f),
                 DividerMask(uv.x, 3.0f, 0.004f));

    return LabOutput(color);
}
