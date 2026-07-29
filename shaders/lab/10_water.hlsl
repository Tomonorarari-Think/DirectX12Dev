//=============================================================================
// 10_water.hlsl
//   波を高さとして作り、その傾きから法線を求めて光を当てる。
//
//   11 章でやったライティングを、モデルではなく「計算で作った面」に適用する。
//   高さ → 法線 → 反射、という流れは水面でも地形でも同じ。
//   解説 : docs/shader-lab/10_水面.md
//=============================================================================
#include "LabCommon.hlsli"

/// 進行方向 dir へ進む波の高さ。
///   sin をそのまま使うと丸い波になる。0〜1 に直して累乗すると
///   山が尖り谷が広がり、実際の水面に近づく（ジェルストナー波の簡易版）。
float WaveHeight(float2 p, float2 dir, float frequency, float speed,
                 float amplitude, float sharpness, float time)
{
    float phase = dot(p, dir) * frequency + time * speed;
    float s = sin(phase) * 0.5f + 0.5f;
    return amplitude * pow(s, sharpness);
}

/// 何本かの波を重ねた高さ。
///   ★ 向きをわざと「割り切れない角度」にする。揃えると格子模様が出る。
float SurfaceHeight(float2 p, float time)
{
    float h = 0.0f;
    h += WaveHeight(p, normalize(float2( 1.00f,  0.28f)), 2.1f, 1.05f, 0.50f, 2.0f, time);
    h += WaveHeight(p, normalize(float2(-0.37f,  1.00f)), 2.9f, 1.35f, 0.30f, 2.0f, time);
    h += WaveHeight(p, normalize(float2( 0.72f, -0.61f)), 4.3f, 1.75f, 0.16f, 1.5f, time);
    h += WaveHeight(p, normalize(float2(-0.83f, -0.42f)), 6.1f, 2.10f, 0.08f, 1.5f, time);
    return h;
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv) * 5.5f;

    // --- (1) 高さから法線を作る ---------------------------------------------
    //   隣との高さの差が、その向きの傾き。22 章の法線マップと同じ考え方。
    //   ★ e を小さくしすぎると、細かい起伏を拾いすぎて法線が暴れる。
    const float e = 0.012f;

    float h  = SurfaceHeight(p, t);
    float hx = SurfaceHeight(p + float2(e, 0.0f), t);
    float hy = SurfaceHeight(p + float2(0.0f, e), t);

    // 高さが右へ上がるなら法線は左へ傾くので、符号は負。
    //   z を大きくすると面が寝る（穏やかになる）。ここが「波の強さ」の調整点。
    float3 normal = normalize(float3(-(hx - h) / e, -(hy - h) / e, 5.0f));

    // --- (2) ライティング ---------------------------------------------------
    float3 toLight = normalize(float3(-0.45f, 0.42f, 0.79f));
    float3 toEye   = float3(0.0f, 0.0f, 1.0f);
    float3 halfVec = normalize(toLight + toEye);

    float diffuse  = saturate(dot(normal, toLight));
    float specular = pow(saturate(dot(normal, halfVec)), 160.0f);

    // --- (3) フレネル : 浅い角度ほど空を映す --------------------------------
    float fresnel = pow(1.0f - saturate(dot(normal, toEye)), 5.0f);

    float3 deep    = float3(0.015f, 0.075f, 0.135f);
    float3 shallow = float3(0.050f, 0.290f, 0.400f);
    float3 skyTint = float3(0.520f, 0.700f, 0.900f);

    float3 color = lerp(deep, shallow, saturate(h * 0.9f));
    color = lerp(color, skyTint, saturate(fresnel) * 0.30f);
    color += diffuse * 0.16f;
    color += specular * float3(1.0f, 0.97f, 0.90f) * 1.3f;

    // --- (4) 波の峰にだけ、うっすら泡を乗せる -------------------------------
    float foam = smoothstep(0.86f, 1.02f, h);
    color = lerp(color, float3(0.88f, 0.94f, 0.97f), foam * 0.35f);

    // --- 左上 : 高さそのものを見せる ---------------------------------------
    if (uv.x < 0.26f && uv.y < 0.26f)
    {
        float2 q = uv / 0.26f;
        float value = saturate(SurfaceHeight(p, t) * 0.75f);
        color = float3(value, value, value);

        float frame = 1.0f - smoothstep(0.0f, 0.02f,
                                        min(min(q.x, 1.0f - q.x),
                                            min(q.y, 1.0f - q.y)));
        color = lerp(color, float3(0.95f, 0.55f, 0.30f), frame);
    }

    // --- 右上 : 法線を色にして見せる ---------------------------------------
    if (uv.x > 0.74f && uv.y < 0.26f)
    {
        float2 q = (uv - float2(0.74f, 0.0f)) / 0.26f;
        color = normal * 0.5f + 0.5f;

        float frame = 1.0f - smoothstep(0.0f, 0.02f,
                                        min(min(q.x, 1.0f - q.x),
                                            min(q.y, 1.0f - q.y)));
        color = lerp(color, float3(0.95f, 0.55f, 0.30f), frame);
    }

    return LabOutput(color);
}
