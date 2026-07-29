//=============================================================================
// 11_raymarch.hlsl
//   レイマーチング : 三角形を 1 枚も使わずに 3D を描く。
//
//   「その点から物体までの距離」を返す関数さえあれば、
//   視線を少しずつ進めていくだけで形が見える。
//   解説 : docs/shader-lab/11_レイマーチング.md
//=============================================================================
#include "LabCommon.hlsli"

/// 球までの距離。
float SdSphere(float3 p, float radius)
{
    return length(p) - radius;
}

/// 角の丸い箱までの距離。
float SdRoundBox(float3 p, float3 halfSize, float radius)
{
    float3 d = abs(p) - halfSize + radius;
    return length(max(d, 0.0f)) + min(max(d.x, max(d.y, d.z)), 0.0f) - radius;
}

/// ドーナツ（トーラス）までの距離。
float SdTorus(float3 p, float2 size)
{
    float2 q = float2(length(p.xz) - size.x, p.y);
    return length(q) - size.y;
}

/// なめらかに繋ぐ和。k が大きいほど繋ぎ目が丸くなる。
float SmoothUnion(float a, float b, float k)
{
    float h = saturate(0.5f + 0.5f * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.0f - h);
}

/// シーン全体までの距離。ここに形を足していく。
float MapScene(float3 p, float time)
{
    // 床（y = -1 の平面）
    float ground = p.y + 1.0f;

    // 上下する球
    float3 sphereP = p - float3(-1.15f, 0.15f + 0.30f * sin(time * 1.3f), 0.0f);
    float sphere = SdSphere(sphereP, 0.72f);

    // 回る箱
    float3 boxP = p - float3(1.15f, 0.05f, 0.0f);
    boxP.xz = mul(Rotate2D(time * 0.6f), boxP.xz);
    float box = SdRoundBox(boxP, float3(0.55f, 0.55f, 0.55f), 0.10f);

    // 傾いたトーラス
    float3 torusP = p - float3(0.0f, 0.20f, -1.30f);
    torusP.yz = mul(Rotate2D(0.6f), torusP.yz);
    float torus = SdTorus(torusP, float2(0.70f, 0.20f));

    // 球と箱を「なめらかに繋ぐ」ための小さな橋
    float3 blobP = p - float3(0.0f, -0.35f + 0.15f * sin(time), 0.0f);
    float blob = SdSphere(blobP, 0.45f);

    float shapes = SmoothUnion(sphere, blob, 0.45f);
    shapes = SmoothUnion(shapes, box, 0.35f);
    shapes = min(shapes, torus);

    return min(ground, shapes);
}

/// 法線 : 距離関数の勾配。4 点の差分で求める（テトラヘドロン法）。
float3 CalcNormal(float3 p, float time)
{
    const float e = 0.0015f;
    float2 k = float2(1.0f, -1.0f);

    return normalize(
        k.xyy * MapScene(p + k.xyy * e, time) +
        k.yyx * MapScene(p + k.yyx * e, time) +
        k.yxy * MapScene(p + k.yxy * e, time) +
        k.xxx * MapScene(p + k.xxx * e, time));
}

/// 影 : 光へ向かって進み、途中で何かに近づいたら暗くする。
///   距離関数がそのまま「どれだけ掠めたか」を教えてくれるので、
///   1 本のレイでやわらかい影が作れる。
float SoftShadow(float3 origin, float3 direction, float time)
{
    float result = 1.0f;
    float t = 0.05f;

    for (int i = 0; i < 40; ++i)
    {
        float d = MapScene(origin + direction * t, time);
        result = min(result, 10.0f * d / t);

        t += clamp(d, 0.02f, 0.30f);

        if (result < 0.005f || t > 8.0f)
        {
            break;
        }
    }

    return saturate(result);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 p = ToCenteredUv(input.uv);
    float  t = g_time.x;

    // --- カメラ -------------------------------------------------------------
    float angle = t * 0.25f;
    float3 eye    = float3(sin(angle) * 4.6f, 1.55f, cos(angle) * 4.6f);
    float3 target = float3(0.0f, -0.05f, 0.0f);

    float3 forward = normalize(target - eye);
    float3 right   = normalize(cross(float3(0.0f, 1.0f, 0.0f), forward));
    float3 up      = cross(forward, right);

    // 画角 60 度ぶんの傾き
    float3 direction = normalize(forward + right * p.x * 0.58f + up * p.y * 0.58f);

    // --- マーチング ---------------------------------------------------------
    //   距離関数は「その点から最寄りの面までの距離」なので、
    //   その長さだけ進んでも決して面を突き抜けない（球トレーシング）。
    float distance = 0.0f;
    bool  hit      = false;

    for (int i = 0; i < 96; ++i)
    {
        float3 position = eye + direction * distance;
        float  d        = MapScene(position, t);

        if (d < 0.0015f)
        {
            hit = true;
            break;
        }

        distance += d;

        if (distance > 24.0f)
        {
            break;
        }
    }

    // --- 陰影 ---------------------------------------------------------------
    float3 sky = lerp(float3(0.30f, 0.42f, 0.62f),
                      float3(0.07f, 0.10f, 0.18f),
                      saturate(p.y * 0.6f + 0.4f));

    float3 color = sky;

    if (hit)
    {
        float3 position = eye + direction * distance;
        float3 normal   = CalcNormal(position, t);
        float3 toLight  = normalize(float3(-0.55f, 0.75f, 0.42f));

        float shadow  = SoftShadow(position + normal * 0.01f, toLight, t);
        float diffuse = saturate(dot(normal, toLight)) * shadow;

        // 空からの環境光。上を向いた面ほど明るい。
        float ambient = 0.35f + 0.35f * normal.y;

        // 床は市松、それ以外は法線から色を決める
        float3 albedo;
        if (position.y < -0.98f)
        {
            float2 grid = floor(position.xz * 1.2f);
            float  checker = fmod(grid.x + grid.y, 2.0f);
            albedo = lerp(float3(0.18f, 0.19f, 0.22f),
                          float3(0.42f, 0.44f, 0.48f), checker);
        }
        else
        {
            albedo = 0.45f + 0.45f * normal;
        }

        float3 halfVec  = normalize(toLight - direction);
        float  specular = pow(saturate(dot(normal, halfVec)), 48.0f) * shadow;

        color = albedo * (ambient + diffuse * 0.95f)
              + float3(1.0f, 0.96f, 0.88f) * specular * 0.55f;

        // 遠くを空の色へ溶かす（フォグ）
        color = lerp(color, sky, saturate(distance / 24.0f));
    }

    // 軽いトーンマッピング
    color = color / (1.0f + color);
    color = pow(color, 1.0f / 1.15f);

    return LabOutput(color);
}
