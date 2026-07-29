//=============================================================================
// 03_tiling.hlsl
//   座標そのものを作り替える : 繰り返しと極座標。
//
//   図形を何個も並べるのではなく、「座標を折りたたむ」。
//   1 個ぶんの計算で無限に並べられる。
//   解説 : docs/shader-lab/03_繰り返しと極座標.md
//=============================================================================
#include "LabCommon.hlsli"

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 p = ToCenteredUv(input.uv);
    float  t = g_time.x;

    float3 color;

    if (input.uv.x < 0.5f)
    {
        // --- 左 : 格子状に繰り返す -----------------------------------------
        //   frac(p) - 0.5 で、どのマスにいても座標が -0.5〜0.5 になる。
        //   1 個ぶんの図形を描くコードが、そのまま全マスに効く。
        float2 scaled = p * 3.0f;
        float2 cell   = floor(scaled);          // 何番目のマスか
        float2 local  = frac(scaled) - 0.5f;    // マスの中での位置

        // マスごとに違う値が欲しいときは、マス番号をハッシュに通す。
        float seed  = Hash21(cell);
        float phase = seed * kTau;

        // 大きさをマスごとに変え、時間で脈動させる。
        float radius = 0.16f + 0.14f * (0.5f + 0.5f * sin(t * 1.4f + phase));

        float d = SdCircle(local, radius);

        float3 back = float3(0.08f, 0.09f, 0.12f);
        float3 dot1 = PaletteWarm(seed);
        color = lerp(back, dot1, FillMask(d));

        // マスの境目を薄く見せる
        float2 edge = abs(local);
        float grid = 1.0f - smoothstep(0.47f, 0.49f, max(edge.x, edge.y));
        color = lerp(color * 0.55f, color, grid);
    }
    else
    {
        // --- 右 : 極座標にしてから繰り返す ---------------------------------
        //   直交座標を (角度, 半径) に置き換えると、
        //   「角度方向の繰り返し」＝ 放射状の模様になる。
        float2 q = p - float2(1.0f, 0.0f);   // 右半分の中心へ寄せる

        float radius = length(q);
        float angle  = atan2(q.y, q.x);

        // 角度を 12 等分し、1 区画ぶんの座標に折りたたむ。
        const float kSectors = 12.0f;
        float sector = floor((angle + kPi) / kTau * kSectors);
        float local  = frac((angle + kPi) / kTau * kSectors) - 0.5f;

        // 花びら : 区画の中央からの距離と、半径方向の窓で形を作る。
        float petal = abs(local) * 2.0f;
        float shape = smoothstep(0.9f, 0.2f, petal)
                    * smoothstep(0.62f, 0.55f, radius)
                    * smoothstep(0.10f, 0.16f, radius);

        // 半径方向の縞。時間で外へ流れる。
        float rings = 0.5f + 0.5f * cos(radius * 40.0f - t * 3.0f);

        float3 back = float3(0.06f, 0.07f, 0.10f);
        float3 tint = PaletteWarm(sector / kSectors + t * 0.05f);

        color = back + tint * shape * (0.55f + 0.45f * rings);

        // 中心の円
        color = lerp(color, float3(0.98f, 0.92f, 0.75f),
                     FillMask(SdCircle(q, 0.09f)));
    }

    // 左右の境目
    float border = 1.0f - smoothstep(0.0f, 0.0015f, abs(input.uv.x - 0.5f));
    color = lerp(color, float3(0.05f, 0.06f, 0.08f), border);

    return LabOutput(color);
}
