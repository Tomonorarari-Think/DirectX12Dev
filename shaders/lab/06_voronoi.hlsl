//=============================================================================
// 06_voronoi.hlsl
//   ボロノイ図 : 「いちばん近い種はどれか」で塗り分ける。
//
//   細胞、ひび割れ、石畳、鱗。自然の「区画」はだいたいこれで作れる。
//   解説 : docs/shader-lab/06_ボロノイ.md
//=============================================================================
#include "LabCommon.hlsli"

/// いちばん近い種までの距離（f1）と、2 番目（f2）、そして種の番号を返す。
///   周囲 3x3 のマスだけ調べれば足りる。種はマスの中に 1 個しか置かないため。
float3 Voronoi(float2 p, float jitter, float time)
{
    float2 cell  = floor(p);
    float2 local = frac(p);

    float f1 = 8.0f;
    float f2 = 8.0f;
    float id = 0.0f;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y);

            // マスごとに決まった位置へ種を置く。jitter で乱れ具合を変える。
            float2 seed = Hash22(cell + offset);

            // 時間で種を動かすと、区画がぬるぬる変形する。
            seed = 0.5f + jitter * 0.5f * sin(time + kTau * seed);

            float2 diff = offset + seed - local;
            float  d    = length(diff);

            if (d < f1)
            {
                f2 = f1;
                f1 = d;
                id = Hash21(cell + offset);
            }
            else if (d < f2)
            {
                f2 = d;
            }
        }
    }

    return float3(f1, f2, id);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv) * 5.0f;

    float3 v  = Voronoi(p, 1.0f, t * 0.6f);
    float  f1 = v.x;
    float  f2 = v.y;
    float  id = v.z;

    float3 color;

    if (uv.x < 0.34f)
    {
        // --- 左 : 区画ごとに色を塗る（f1 の「番号」を使う）------------------
        color = PaletteWarm(id);

        // 中心へ向かって少し暗くする。立体感が出る。
        color *= 0.55f + 0.65f * (1.0f - f1);
    }
    else if (uv.x < 0.67f)
    {
        // --- 中 : いちばん近い種までの距離をそのまま明るさに ----------------
        //   種のまわりが暗く、境界へ向かって明るくなる。石のような見え方。
        color = float3(f1, f1, f1);
    }
    else
    {
        // --- 右 : f2 - f1 で「境界線」を描く --------------------------------
        //   1 番目と 2 番目の距離が近い所 ＝ ちょうど真ん中 ＝ 境界。
        //   ひび割れや細胞壁はこれで作れる。
        float edge = smoothstep(0.0f, 0.12f, f2 - f1);

        float3 wall = float3(0.10f, 0.11f, 0.14f);
        float3 face = PaletteWarm(id) * (0.5f + 0.5f * f1);

        color = lerp(wall, face, edge);
    }

    // 仕切り
    float sep = 1.0f - smoothstep(0.0f, 0.0015f,
                                  min(abs(uv.x - 0.34f), abs(uv.x - 0.67f)));
    color = lerp(color, float3(0.95f, 0.55f, 0.30f), sep);

    return LabOutput(color);
}
