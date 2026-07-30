//=============================================================================
// 31_shield.hlsl
//   エネルギーシールド : 六角セル ＋ フレネル ＋ 着弾の波紋。
//
//   「縁が光る」だけで、そこに透明な膜があるように見える。
//   フレネルは 20 章で使った式そのもの。
//   解説 : docs/shader-lab/31_シールド.md
//=============================================================================
#include "LabCommon.hlsli"

/// 六角形の格子。戻り値 : x = セルの中心からの距離、yz = セル番号。
///   六角格子は「2 つのずれた四角格子」を重ねて、近いほうを採ると作れる。
float3 HexGrid(float2 p)
{
    const float2 kSize = float2(1.0f, 1.7320508f);   // 1, sqrt(3)

    // ★ fmod は負の入力で負を返す。frac を使うと必ず 0〜1 に収まる。
    //   これを間違えると、画面の左半分・下半分だけ格子が崩れる。
    float2 a = (frac(p / kSize) - 0.5f) * kSize;
    float2 b = (frac(p / kSize + 0.5f) - 0.5f) * kSize;

    float2 local = (dot(a, a) < dot(b, b)) ? a : b;
    float2 cell  = p - local;

    // セルの縁までの距離（六角形の 3 方向で測る）。
    //   ★ 戻り値は 0（中心）〜 0.5（縁）の範囲。1 まで行かない。
    //     しきい値を 1 付近に置くと、線が 1 本も出ないので注意。
    float2 q = abs(local);
    float edge = max(dot(q, float2(1.0f, 1.7320508f) * 0.5f), q.x);

    return float3(edge, cell);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 p = ToCenteredUv(input.uv);
    float  t = g_time.x;

    // --- 球としての法線を作る（正射影の球）--------------------------------
    const float radius = 0.78f;
    float r = length(p);

    float3 color = float3(0.02f, 0.03f, 0.05f);

    // 背景（うっすら星）
    {
        float2 q = p * 11.0f;
        float2 cell  = floor(q);
        float2 local = frac(q) - 0.5f;

        float2 seed = Hash22(cell);
        if (Hash21(cell + 3.3f) > 0.86f)
        {
            float d = length(local - (seed - 0.5f) * 0.7f);
            color += float3(0.75f, 0.85f, 1.0f) * 0.004f / max(d, 0.004f);
        }
    }

    if (r < radius)
    {
        float z = sqrt(max(radius * radius - r * r, 0.0f)) / radius;
        float3 normal = normalize(float3(p / radius, z));

        float3 toEye = float3(0.0f, 0.0f, 1.0f);

        // --- フレネル : 縁ほど強く光る -------------------------------------
        //   これが「透明な膜がある」ことを見せる、いちばん効く要素。
        float fresnel = pow(1.0f - saturate(dot(normal, toEye)), 3.0f);

        // --- 六角セル -------------------------------------------------------
        //   法線から球面座標を作って貼ると、極でつぶれる。
        //   ここでは正射影の座標をそのまま使い、縁で伸びるのを味とする。
        float3 hex = HexGrid(p * 7.0f + float2(0.0f, t * 0.10f));

        float cellEdge = smoothstep(0.40f, 0.485f, hex.x);

        // セルごとに明滅
        float seed = Hash21(hex.yz);
        float pulse = 0.45f + 0.55f * sin(t * 1.6f + seed * kTau);

        // --- 着弾の波紋 -----------------------------------------------------
        //   1.4 秒ごとに 1 発。当たった点から輪が広がる。
        const float period = 1.4f;
        float shot  = floor(t / period);
        float local = frac(t / period) * period;

        float2 hit = (Hash22(float2(shot, 5.5f)) - 0.5f) * 1.15f;

        float distance = length(p - hit);
        float ringRadius = local * 1.10f;

        float ripple = 1.0f - saturate(abs(distance - ringRadius) / 0.13f);
        ripple = pow(ripple, 2.5f) * exp(-local * 1.8f);

        // --- 合成 -----------------------------------------------------------
        float3 shieldColor = float3(0.28f, 0.62f, 1.00f);

        color += shieldColor * fresnel * 1.35f;                    // 縁
        color += shieldColor * cellEdge * (0.30f + 0.45f * pulse); // セルの線
        color += shieldColor * cellEdge * ripple * 2.6f;           // 波紋（線が強く光る）
        color += float3(0.75f, 0.90f, 1.00f) * ripple * 0.85f;     // 波紋そのもの

        // 着弾点の閃光
        color += float3(1.0f, 0.95f, 0.85f)
               * exp(-local * 7.0f) * 0.008f / max(distance, 0.02f);

        // 内側はうっすら曇る
        color += shieldColor * 0.05f;
    }

    color = color / (1.0f + color);

    return LabOutput(color);
}
