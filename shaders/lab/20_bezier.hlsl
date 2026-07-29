//=============================================================================
// 20_bezier.hlsl
//   曲線までの距離 : ベジエ曲線を「塗る」。
//
//   2 次ベジエなら、距離が解析的に求まる（3 次方程式を解く）。
//   距離さえ分かれば、太さも輪郭も影も 02 番と同じ手が使える。
//   解説 : docs/shader-lab/20_ベジエ曲線.md
//=============================================================================
#include "LabCommon.hlsli"

/// 2 次ベジエ曲線までの符号なし距離（Inigo Quilez）。
///   点から曲線までの最短距離は 3 次方程式の解になる。
///   判別式で場合分けし、カルダノの公式または三角関数で解く。
float SdBezier(float2 pos, float2 A, float2 B, float2 C)
{
    float2 a = B - A;
    float2 b = A - 2.0f * B + C;
    float2 c = a * 2.0f;
    float2 d = A - pos;

    float kk = 1.0f / max(dot(b, b), 1e-6f);
    float kx = kk * dot(a, b);
    float ky = kk * (2.0f * dot(a, a) + dot(d, b)) / 3.0f;
    float kz = kk * dot(d, a);

    float p  = ky - kx * kx;
    float p3 = p * p * p;
    float q  = kx * (2.0f * kx * kx - 3.0f * ky) + kz;
    float h  = q * q + 4.0f * p3;

    float result;

    if (h >= 0.0f)
    {
        // 解が 1 つ（カルダノの公式）
        h = sqrt(h);
        float2 x = (float2(h, -h) - q) * 0.5f;
        float2 uv = sign(x) * pow(abs(x), 1.0f / 3.0f);
        float  tt = saturate(uv.x + uv.y - kx);
        float2 e  = d + (c + b * tt) * tt;
        result = dot(e, e);
    }
    else
    {
        // 解が 3 つ（三角関数で解く）
        float z = sqrt(-p);
        float v = acos(clamp(q / (p * z * 2.0f), -1.0f, 1.0f)) / 3.0f;
        float m = cos(v);
        float n = sin(v) * 1.732050808f;   // sqrt(3)

        float3 tt = saturate(float3(m + m, -n - m, n - m) * z - kx);

        float2 e1 = d + (c + b * tt.x) * tt.x;
        float2 e2 = d + (c + b * tt.y) * tt.y;

        result = min(dot(e1, e1), dot(e2, e2));
    }

    return sqrt(result);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv);

    float3 color = float3(0.07f, 0.08f, 0.11f);

    // --- 制御点。真ん中の点を動かす ----------------------------------------
    float2 A = float2(-1.25f, -0.45f);
    float2 B = float2(0.0f + 0.55f * sin(t * 0.7f), 0.55f + 0.35f * cos(t * 0.9f));
    float2 C = float2(1.25f, -0.45f);

    float d = SdBezier(p, A, B, C);

    // --- (1) 太さを変えながら塗る ------------------------------------------
    //   距離が分かっているので、太さは「距離のしきい値」でしかない。
    float stroke = 0.035f;
    float mask = 1.0f - smoothstep(stroke - fwidth(d), stroke + fwidth(d), d);

    float3 lineColor = PaletteWarm(0.55f);
    color = lerp(color, lineColor, mask);

    // --- (2) にじみ（距離の逆数）-------------------------------------------
    color += lineColor * 0.012f / max(d, 0.004f) * 0.35f;

    // --- (3) 等高線で距離場を見せる ----------------------------------------
    float rings = 0.5f + 0.5f * cos(d * 55.0f);
    color = lerp(color, color * (0.72f + 0.28f * rings), 0.75f);

    // --- (4) 制御点と制御多角形 --------------------------------------------
    float guide = min(SdSegment(p, A, B), SdSegment(p, B, C)) - 0.004f;
    color = lerp(color, float3(0.45f, 0.48f, 0.55f), FillMask(guide) * 0.7f);

    color = lerp(color, float3(0.98f, 0.72f, 0.25f),
                 FillMask(SdCircle(p - A, 0.028f)));
    color = lerp(color, float3(0.35f, 0.90f, 0.60f),
                 FillMask(SdCircle(p - B, 0.032f)));
    color = lerp(color, float3(0.98f, 0.72f, 0.25f),
                 FillMask(SdCircle(p - C, 0.028f)));

    // --- (5) 太さを変えた 2 本目 -------------------------------------------
    //   距離が分かっているので、太さは「しきい値」を変えるだけで済む。
    //   しきい値を場所によって変えれば、筆圧のような表現になる。
    {
        float2 A2 = float2(-1.25f, 0.30f);
        float2 B2 = float2(0.0f - 0.45f * sin(t * 0.5f), -0.75f);
        float2 C2 = float2(1.25f, 0.30f);

        float d2 = SdBezier(p, A2, B2, C2);

        // 左から右へ細く → 太く → 細く
        float u = saturate((p.x + 1.5f) / 3.0f);
        float taper = 0.055f * sin(u * kPi);

        float m = 1.0f - smoothstep(taper - fwidth(d2), taper + fwidth(d2), d2);

        color = lerp(color, PaletteWarm(0.15f), m);
    }

    return LabOutput(color);
}
