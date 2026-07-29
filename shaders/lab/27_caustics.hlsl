//=============================================================================
// 27_caustics.hlsl
//   コースティクス : 水面が光を集めて、底に網目を描く。
//
//   波打つ水面がレンズの働きをして、光が集まる場所と散る場所ができる。
//   「面積がどれだけ縮んだか」を計算すると、その明るさが求まる。
//   解説 : docs/shader-lab/27_コースティクス.md
//=============================================================================
#include "LabCommon.hlsli"

/// 水面の高さ。10 番と同じ作り方。
float WaterHeight(float2 p, float time)
{
    float h = 0.0f;
    h += sin(dot(p, normalize(float2( 1.00f,  0.31f))) * 3.1f + time * 1.10f) * 0.50f;
    h += sin(dot(p, normalize(float2(-0.42f,  1.00f))) * 4.3f + time * 1.45f) * 0.32f;
    h += sin(dot(p, normalize(float2( 0.77f, -0.63f))) * 6.7f + time * 1.85f) * 0.18f;
    h += Fbm(p * 1.1f + float2(time * 0.10f, time * 0.07f), 3) * 0.30f;
    return h;
}

/// 水面で屈折した光が、底のどこへ届くかを返す。
float2 RefractedPoint(float2 p, float time, float depth, float strength)
{
    // 高さの傾き ＝ 水面の法線の傾き
    const float e = 0.02f;
    float h  = WaterHeight(p, time);
    float hx = WaterHeight(p + float2(e, 0.0f), time);
    float hy = WaterHeight(p + float2(0.0f, e), time);

    float2 slope = float2(hx - h, hy - h) / e;

    // 傾いた面を通った光は、傾きの向きへずれて底に当たる。
    //   本来はスネルの法則で角度を求めるが、傾きが小さいうちは比例で足りる。
    return p + slope * depth * strength;
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv) * 1.5f;

    // --- コースティクスの強さ ----------------------------------------------
    //   ★ 「面積がどれだけ縮んだか」が明るさになる。
    //     隣り合う 2 点が屈折後にどれだけ近づいたかを調べればよい。
    const float e = 0.020f;
    const float depth = 1.0f;
    const float strength = 0.85f;

    float2 r00 = RefractedPoint(p, t, depth, strength);
    float2 r10 = RefractedPoint(p + float2(e, 0.0f), t, depth, strength);
    float2 r01 = RefractedPoint(p + float2(0.0f, e), t, depth, strength);

    // 変換のヤコビアン（面積の伸び縮み）
    float2 du = (r10 - r00) / e;
    float2 dv = (r01 - r00) / e;
    float  area = abs(du.x * dv.y - du.y * dv.x);

    // 面積が小さいほど光が集まっている
    float caustic = 1.0f / max(area, 0.02f);

    // 明るい部分を強調する
    caustic = pow(saturate(caustic * 0.30f), 1.8f);

    float3 color;

    if (uv.y < 0.76f)
    {
        // --- 底のタイル -----------------------------------------------------
        float2 tile = floor(p * 1.6f);
        float checker = fmod(tile.x + tile.y, 2.0f);

        float3 floorColor = lerp(float3(0.16f, 0.30f, 0.36f),
                                 float3(0.24f, 0.42f, 0.48f), checker);

        // 目地
        float2 edge = abs(frac(p * 1.6f) - 0.5f);
        floorColor *= 0.55f + 0.45f * smoothstep(0.46f, 0.48f, max(edge.x, edge.y));

        // --- 光の網 ---------------------------------------------------------
        //   ★ 波長ごとに屈折の量が少し違うので、縁が色づく。
        float causticR = caustic;
        float2 rr = RefractedPoint(p, t, depth, strength * 1.03f);
        float2 rb = RefractedPoint(p, t, depth, strength * 0.97f);

        color = floorColor * (0.55f + 0.35f * caustic);
        color += float3(1.00f, 0.97f, 0.88f) * caustic * 0.85f;

        // 水の色を全体に掛ける
        color *= float3(0.72f, 0.94f, 1.00f);

        // 水面の影（波の谷は暗い）
        float h = WaterHeight(p, t);
        color *= 0.88f + 0.12f * saturate(h);
    }
    else
    {
        // --- 下の帯 : 屈折した点の分布を見せる ------------------------------
        float2 q = float2(uv.x, (uv.y - 0.76f) / 0.24f);

        if (q.x < 0.5f)
        {
            // 左 : 面積の伸び縮みそのもの
            float v = saturate(area * 0.6f);
            color = float3(v, v, v);
        }
        else
        {
            // 右 : そこから作った明るさ
            color = float3(caustic, caustic, caustic);
        }

        color = lerp(color, float3(0.95f, 0.55f, 0.30f),
                     1.0f - smoothstep(0.0f, 0.004f, abs(q.x - 0.5f)));
        color = lerp(color, float3(0.95f, 0.55f, 0.30f), FrameMask(q, 0.012f));
    }

    color = color / (1.0f + color * 0.55f);

    return LabOutput(color);
}
