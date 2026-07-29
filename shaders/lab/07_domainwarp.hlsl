//=============================================================================
// 07_domainwarp.hlsl
//   ドメインワープ : 「模様」ではなく「座標」を歪ませる。
//
//   fbm(p) の代わりに fbm(p + fbm(p)) を計算するだけで、
//   渦を巻いた有機的な模様になる。大理石や煙はこれで作る。
//   解説 : docs/shader-lab/07_ドメインワープ.md
//=============================================================================
#include "LabCommon.hlsli"

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x * 0.12f;

    float2 p = ToCenteredUv(uv) * 2.4f;

    float3 color;

    if (uv.x < 0.5f)
    {
        // --- 左 : 歪ませない fBm --------------------------------------------
        float value = Fbm(p + float2(t, 0.0f), 5);
        color = PaletteWarm(value * 1.4f);
    }
    else
    {
        // --- 右 : 2 段階のドメインワープ ------------------------------------
        //   1 段目 : ずらす量そのものをノイズで作る
        float2 q = float2(Fbm(p + float2(0.0f, 0.0f) + t, 5),
                          Fbm(p + float2(5.2f, 1.3f) + t, 5));

        //   2 段目 : ずらした座標で、さらにずらす量を作る
        float2 r = float2(Fbm(p + 4.0f * q + float2(1.7f, 9.2f) + t * 1.4f, 5),
                          Fbm(p + 4.0f * q + float2(8.3f, 2.8f) + t * 1.1f, 5));

        //   最後に、2 段ぶんずらした座標でノイズを引く
        float value = Fbm(p + 4.0f * r, 5);

        color = PaletteWarm(value * 1.6f);

        // ずらした量そのものを色に混ぜると、流れの向きが見えてくる。
        color = lerp(color, color * (0.55f + 0.85f * r.x), 0.55f);
    }

    // 仕切り
    float sep = 1.0f - smoothstep(0.0f, 0.0015f, abs(uv.x - 0.5f));
    color = lerp(color, float3(0.05f, 0.06f, 0.08f), sep);

    return LabOutput(color);
}
