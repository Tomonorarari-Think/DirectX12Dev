//=============================================================================
// 23_lensflare.hlsl
//   レンズフレア : 光源の「反対側」に並ぶゴーストと、放射状のハロ。
//
//   レンズの中で反射した光が、画面の中心を挟んで反対側に像を結ぶ。
//   だから「光源の位置を中心で反転させ、その線上に並べる」だけでよい。
//   解説 : docs/shader-lab/23_レンズフレア.md
//=============================================================================
#include "LabCommon.hlsli"

/// 背景。フレアを乗せる下地。
float3 Background(float2 p, float time)
{
    // 遠くの山と空
    float horizon = -0.25f + 0.10f * Fbm(float2(p.x * 0.9f + 4.0f, 0.0f), 4);

    float3 sky = lerp(float3(0.55f, 0.62f, 0.80f),
                      float3(0.10f, 0.20f, 0.45f),
                      saturate(p.y * 0.8f + 0.35f));

    float3 ground = lerp(float3(0.10f, 0.11f, 0.14f),
                         float3(0.03f, 0.03f, 0.05f),
                         saturate(-p.y));

    return (p.y > horizon) ? sky : ground;
}

/// 円形のゴースト。中心からの距離で明るさを決める。
float3 Ghost(float2 p, float2 center, float size, float3 tint, float intensity)
{
    float d = length(p - center);

    // 縁が明るいリング状にすると、それらしくなる
    float disc = smoothstep(size, size * 0.55f, d);
    float ring = smoothstep(size * 1.02f, size * 0.92f, d)
               * smoothstep(size * 0.78f, size * 0.90f, d);

    return tint * (disc * 0.35f + ring * 0.9f) * intensity;
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 p = ToCenteredUv(input.uv);
    float  t = g_time.x;

    // 光源はゆっくり左右に動く
    float2 sun = float2(0.85f * sin(t * 0.25f), 0.42f);

    float3 color = Background(p, t);

    float2 toSun = p - sun;
    float  sunDistance = length(toSun);

    // --- (1) 光源そのもの ---------------------------------------------------
    color += float3(1.0f, 0.96f, 0.86f) * 0.02f / max(sunDistance * sunDistance, 0.0006f);

    // --- (2) 放射状の光条（アナモルフィック） -------------------------------
    //   横に長く伸びる筋。シネマレンズの特徴。
    {
        float streak = 0.010f / max(abs(toSun.y) * 22.0f + abs(toSun.x) * 0.30f, 0.002f);
        color += float3(0.45f, 0.62f, 1.00f) * streak;
    }

    // --- (3) 放射状のスパイク ----------------------------------------------
    //   絞り羽根による回折。角度で振動させて筋を作る。
    {
        float angle = atan2(toSun.y, toSun.x);
        float spikes = 0.5f + 0.5f * cos(angle * 12.0f + t * 0.2f);
        spikes = pow(spikes, 6.0f);

        float falloff = 0.020f / max(sunDistance, 0.02f);
        color += float3(1.0f, 0.90f, 0.72f) * spikes * falloff * 0.55f;
    }

    // --- (4) ゴースト -------------------------------------------------------
    //   ★ 光源を画面中心で反転した向きに、等間隔で並べる。
    //     これがレンズフレアの「並び方」の正体。
    float2 axis = -sun;   // 中心を挟んで反対側

    color += Ghost(p, axis *  0.35f, 0.075f, float3(0.35f, 0.65f, 1.00f), 0.55f);
    color += Ghost(p, axis *  0.60f, 0.045f, float3(1.00f, 0.55f, 0.35f), 0.75f);
    color += Ghost(p, axis *  0.95f, 0.110f, float3(0.45f, 1.00f, 0.60f), 0.35f);
    color += Ghost(p, axis *  1.35f, 0.060f, float3(1.00f, 0.85f, 0.40f), 0.50f);
    color += Ghost(p, axis * -0.45f, 0.090f, float3(0.85f, 0.40f, 0.95f), 0.30f);
    color += Ghost(p, axis *  1.80f, 0.150f, float3(0.30f, 0.55f, 0.95f), 0.20f);

    // --- (5) 全体のハロ -----------------------------------------------------
    //   光源から離れるほど弱くなる、広い光のかぶり。
    float haze = pow(saturate(1.0f - sunDistance * 0.42f), 3.0f);
    color += float3(1.0f, 0.92f, 0.78f) * haze * 0.22f;

    // --- (6) 画面の端へ向かって弱める --------------------------------------
    //   実際のレンズフレアも、光源が画面外へ出ると消える。
    float visibility = smoothstep(1.6f, 1.0f, length(sun));
    color = lerp(Background(p, t), color, visibility);

    color = color / (1.0f + color);

    return LabOutput(color);
}
