//=============================================================================
// 09_plasma.hlsl
//   波の重ね合わせ（プラズマ・干渉縞）。
//
//   sin を向きと速さを変えて何本か足すだけで、複雑に見える模様ができる。
//   1970 年代のデモから続く、いちばん古典的な手口。
//   解説 : docs/shader-lab/09_プラズマ.md
//=============================================================================
#include "LabCommon.hlsli"

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv) * 3.0f;

    float3 color;

    if (uv.y < 0.5f)
    {
        // --- 下 : 4 本の波を足す --------------------------------------------
        //   ① 横方向の波
        float v = sin(p.x * 2.0f + t);

        //   ② 斜め方向の波（向きベクトルとの内積が「その向きの座標」）
        v += sin(dot(p, normalize(float2(0.7f, 0.5f))) * 3.0f - t * 1.3f);

        //   ③ 同心円の波（中心からの距離で振動させる）
        v += sin(length(p - float2(1.2f, 0.4f)) * 4.0f - t * 2.0f);

        //   ④ 回転する中心を持つ波
        float2 center = float2(cos(t * 0.7f), sin(t * 0.5f)) * 1.2f;
        v += sin(length(p - center) * 3.5f + t);

        // 4 本ぶんなので -4〜4。0〜1 に直す。
        v = v * 0.125f + 0.5f;

        color = PaletteWarm(v);
    }
    else
    {
        // --- 上 : 何本足すと何ができるかを並べる ----------------------------
        int count = 1 + (int)(uv.x * 4.0f);

        float v = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            float angle = (float)i * 1.1f;
            float2 dir  = float2(cos(angle), sin(angle));
            v += sin(dot(p, dir) * (2.0f + (float)i * 0.7f) - t * (1.0f + 0.3f * i));
        }
        v = v / (float)count * 0.5f + 0.5f;

        color = PaletteWarm(v);

        // 仕切り
        color = lerp(color, float3(0.05f, 0.06f, 0.08f),
                     DividerMask(uv.x, 4.0f, 0.006f));
    }

    float half1 = 1.0f - smoothstep(0.0f, 0.0015f, abs(uv.y - 0.5f));
    color = lerp(color, float3(0.05f, 0.06f, 0.08f), half1);

    return LabOutput(color);
}
