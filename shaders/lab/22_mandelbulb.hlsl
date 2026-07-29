//=============================================================================
// 22_mandelbulb.hlsl
//   マンデルバルブ : 3D のフラクタルをレイマーチングで描く。
//
//   12 番の z ← z² + c を 3 次元へ持ち上げ、11 番のレイマーチングで見る。
//   「距離を返す関数」さえ作れれば、どんな形でも描ける。
//   解説 : docs/shader-lab/22_マンデルバルブ.md
//=============================================================================
#include "LabCommon.hlsli"

/// マンデルバルブまでの「距離の推定値」。
///   複素数の 2 乗を、球面座標で「半径を n 乗・角度を n 倍」に置き換える。
///   これが 3 次元版の z ← z^n + c にあたる。
float MapBulb(float3 p, float power, out float trap)
{
    float3 z = p;
    float dr = 1.0f;
    float r  = 0.0f;

    trap = 1e10f;

    for (int i = 0; i < 8; ++i)
    {
        r = length(z);

        if (r > 2.0f)
        {
            break;
        }

        // 軌道がどれだけ原点に近づいたかを覚えておく（色付けに使う）
        trap = min(trap, r);

        // 球面座標へ
        float theta = acos(clamp(z.z / r, -1.0f, 1.0f));
        float phi   = atan2(z.y, z.x);

        // 距離の推定値を更新する（微分の連鎖）
        dr = pow(r, power - 1.0f) * power * dr + 1.0f;

        // 半径を n 乗、角度を n 倍
        float zr = pow(r, power);
        theta *= power;
        phi   *= power;

        z = zr * float3(sin(theta) * cos(phi),
                        sin(theta) * sin(phi),
                        cos(theta));
        z += p;
    }

    // ★ 距離の推定値（distance estimator）。
    //   正確な距離ではないが、「これ以上進んでも突き抜けない」量にはなる。
    return 0.5f * log(max(r, 1e-6f)) * r / dr;
}

float MapScene(float3 p, float power)
{
    float trap;
    return MapBulb(p, power, trap);
}

float3 CalcNormal(float3 p, float power)
{
    const float e = 0.0008f;
    float2 k = float2(1.0f, -1.0f);

    return normalize(
        k.xyy * MapScene(p + k.xyy * e, power) +
        k.yyx * MapScene(p + k.yyx * e, power) +
        k.yxy * MapScene(p + k.yxy * e, power) +
        k.xxx * MapScene(p + k.xxx * e, power));
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = ToCenteredUv(input.uv);
    float  t  = g_time.x;

    // 次数をゆっくり動かすと、形がまるごと変わる
    float power = 6.0f + 3.0f * (0.5f + 0.5f * sin(t * 0.18f));

    // --- カメラ -------------------------------------------------------------
    float angle = t * 0.16f;
    float3 eye    = float3(sin(angle) * 2.35f, 0.55f, cos(angle) * 2.35f);
    float3 target = float3(0.0f, 0.0f, 0.0f);

    float3 forward = normalize(target - eye);
    float3 right   = normalize(cross(float3(0.0f, 1.0f, 0.0f), forward));
    float3 up      = cross(forward, right);

    float3 direction = normalize(forward + right * uv.x * 0.62f + up * uv.y * 0.62f);

    // --- マーチング ---------------------------------------------------------
    float distance = 0.0f;
    bool  hit      = false;
    float trap     = 1e10f;

    for (int i = 0; i < 110; ++i)
    {
        float3 position = eye + direction * distance;

        float localTrap;
        float d = MapBulb(position, power, localTrap);

        if (d < 0.0006f * distance)
        {
            hit  = true;
            trap = localTrap;
            break;
        }

        // ★ 推定値なので、少し控えめに進む。そのまま進むと突き抜ける。
        distance += d * 0.85f;

        if (distance > 6.0f)
        {
            break;
        }
    }

    // --- 陰影 ---------------------------------------------------------------
    float3 background = lerp(float3(0.05f, 0.06f, 0.11f),
                             float3(0.01f, 0.01f, 0.03f),
                             saturate(length(uv) * 0.6f));

    float3 color = background;

    if (hit)
    {
        float3 position = eye + direction * distance;
        float3 normal   = CalcNormal(position, power);
        float3 toLight  = normalize(float3(-0.55f, 0.70f, 0.45f));

        float diffuse = saturate(dot(normal, toLight));
        float ambient = 0.28f + 0.32f * normal.y;

        // 軌道の最小半径（orbit trap）を色にすると、内部構造が浮かび上がる
        float3 albedo = PaletteWarm(saturate(trap * 1.4f) * 0.8f + 0.1f);

        float3 halfVec  = normalize(toLight - direction);
        float  specular = pow(saturate(dot(normal, halfVec)), 42.0f);

        color = albedo * (ambient + diffuse * 0.95f)
              + float3(1.0f, 0.96f, 0.90f) * specular * 0.45f;

        // 近くの面ほど明るく（簡易的な環境光遮蔽）
        color *= 1.0f - saturate(distance - 1.4f) * 0.35f;
    }

    color = color / (1.0f + color);
    color = pow(color, 1.0f / 1.1f);

    return LabOutput(color);
}
