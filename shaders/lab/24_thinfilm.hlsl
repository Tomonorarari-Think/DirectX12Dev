//=============================================================================
// 24_thinfilm.hlsl
//   薄膜干渉 : シャボン玉と油膜の虹色。
//
//   色素ではなく「光の波が重なって打ち消し合う」ことで色が付く。
//   膜の厚みと見る角度で色が変わるのが、この現象の特徴。
//   解説 : docs/shader-lab/24_薄膜干渉.md
//=============================================================================
#include "LabCommon.hlsli"

/// 波長（nm）から、おおよその RGB を返す。
///   CIE の等色関数を、区分的な近似で置き換えたもの。
float3 WavelengthToRgb(float wavelength)
{
    float3 color = float3(0.0f, 0.0f, 0.0f);

    if (wavelength < 440.0f)
    {
        color = float3(-(wavelength - 440.0f) / 60.0f, 0.0f, 1.0f);
    }
    else if (wavelength < 490.0f)
    {
        color = float3(0.0f, (wavelength - 440.0f) / 50.0f, 1.0f);
    }
    else if (wavelength < 510.0f)
    {
        color = float3(0.0f, 1.0f, -(wavelength - 510.0f) / 20.0f);
    }
    else if (wavelength < 580.0f)
    {
        color = float3((wavelength - 510.0f) / 70.0f, 1.0f, 0.0f);
    }
    else if (wavelength < 645.0f)
    {
        color = float3(1.0f, -(wavelength - 645.0f) / 65.0f, 0.0f);
    }
    else
    {
        color = float3(1.0f, 0.0f, 0.0f);
    }

    // 端は目に見えにくいので暗くする
    float fade = 1.0f;
    if (wavelength < 420.0f) { fade = 0.3f + 0.7f * (wavelength - 380.0f) / 40.0f; }
    if (wavelength > 700.0f) { fade = 0.3f + 0.7f * (780.0f - wavelength) / 80.0f; }

    return saturate(color) * saturate(fade);
}

/// 薄膜による反射の強さ（0〜1）。
///   膜の表と裏で反射した 2 つの波の「行き来した距離の差」が
///   波長の整数倍なら強め合い、半波長ずれていれば打ち消し合う。
///
///   厚み d、屈折率 n、入射角の余弦 cosTheta のとき、
///   行路差 = 2 · n · d · cos(屈折角)
float FilmIntensity(float thickness, float ior, float cosTheta, float wavelength)
{
    // スネルの法則から、膜の中での余弦を求める
    float sinTheta2 = (1.0f - cosTheta * cosTheta) / (ior * ior);
    float cosInside = sqrt(max(1.0f - sinTheta2, 0.0f));

    // 行路差（nm 単位）
    float pathDifference = 2.0f * ior * thickness * cosInside;

    // 位相差。片方の反射で π ずれる（固定端反射）ので kPi を足す
    float phase = kTau * pathDifference / wavelength + kPi;

    // 強め合い / 打ち消し合い
    return 0.5f + 0.5f * cos(phase);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv);

    float3 color;

    if (uv.y < 0.78f)
    {
        // --- シャボン玉 -----------------------------------------------------
        float radius = 0.62f;
        float d = length(p) - radius;

        if (d > 0.0f)
        {
            // 外側は暗い背景
            color = float3(0.03f, 0.035f, 0.05f);

            // 玉のまわりのにじみ
            color += float3(0.18f, 0.22f, 0.30f) * 0.010f / max(d + 0.01f, 0.004f);
        }
        else
        {
            // 球面の法線（正射影とみなす）
            float z = sqrt(max(radius * radius - dot(p, p), 0.0f)) / radius;
            float3 normal = normalize(float3(p / radius, z));

            float3 toEye = float3(0.0f, 0.0f, 1.0f);
            float cosTheta = saturate(dot(normal, toEye));

            // 膜の厚み（nm）。重力で下が厚くなり、揺らぎも乗る。
            float thickness = 320.0f
                            + 260.0f * saturate(0.5f - p.y * 0.8f)
                            + 90.0f * Fbm(p * 2.6f + float2(t * 0.20f, -t * 0.13f), 4);

            // 屈折率（シャボン水は 1.33 前後）
            const float ior = 1.33f;

            // --- 波長ごとに強さを求めて足し合わせる -------------------------
            //   ★ ここが「虹色になる」理由。波長ごとに強め合う厚みが違う。
            float3 sum = float3(0.0f, 0.0f, 0.0f);
            float  weightSum = 0.0f;

            const int kSamples = 24;
            for (int i = 0; i < kSamples; ++i)
            {
                float wavelength = lerp(390.0f, 720.0f, (i + 0.5f) / kSamples);
                float intensity = FilmIntensity(thickness, ior, cosTheta, wavelength);

                sum += WavelengthToRgb(wavelength) * intensity;
                weightSum += 1.0f;
            }

            color = sum / weightSum * 2.4f;

            // フレネル : 縁ほど強く反射する
            float fresnel = pow(1.0f - cosTheta, 3.0f);
            color *= 0.35f + 1.65f * fresnel;

            // 上からの白いハイライト
            float3 toLight = normalize(float3(-0.35f, 0.55f, 0.75f));
            float spec = pow(saturate(dot(normal, normalize(toLight + toEye))), 60.0f);
            color += spec * 0.55f;
        }
    }
    else
    {
        // --- 下の帯 : 厚みごとの色の変化 ------------------------------------
        float2 q = float2(uv.x, (uv.y - 0.78f) / 0.22f);

        float thickness = lerp(180.0f, 1100.0f, q.x);

        float3 sum = float3(0.0f, 0.0f, 0.0f);
        const int kSamples = 24;
        for (int i = 0; i < kSamples; ++i)
        {
            float wavelength = lerp(390.0f, 720.0f, (i + 0.5f) / kSamples);
            sum += WavelengthToRgb(wavelength)
                 * FilmIntensity(thickness, 1.33f, 1.0f, wavelength);
        }

        color = sum / kSamples * 2.4f;

        color = lerp(color, float3(0.95f, 0.55f, 0.30f), FrameMask(q, 0.012f));
    }

    return LabOutput(color);
}
