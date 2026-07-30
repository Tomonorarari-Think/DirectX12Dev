//=============================================================================
// 28_dissolve.hlsl
//   ディゾルブの作り分け : 模様を変えると消え方が変わる。
//
//   26 章では 3 次元ノイズで溶かした。ここでは同じ仕組みのまま、
//   模様だけを差し替えて 4 通りの消え方を並べる。
//   解説 : docs/shader-lab/28_ディゾルブの作り分け.md
//=============================================================================
#include "LabCommon.hlsli"

/// 消す対象の絵。ここが「溶ける物体」に当たる。
float3 SourceImage(float2 p)
{
    // 同心円と放射で作った紋章のような図形
    float radius = length(p);
    float angle  = atan2(p.y, p.x) / kTau + 0.5f;

    float3 color = PaletteWarm(radius * 0.8f + 0.15f) * 1.25f;
    color *= 0.72f + 0.28f * cos(angle * kTau * 6.0f);

    // 縁を丸く切り抜く
    color *= smoothstep(0.86f, 0.80f, radius);

    return color;
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    // 4 分割。区画ごとに座標を取り直す。
    int2   quadrant = (int2)(uv * 2.0f);
    float2 local    = frac(uv * 2.0f);

    float2 p = local * 2.0f - 1.0f;
    p.y = -p.y;
    p.x *= g_resolution.x * g_resolution.w;

    // 進み具合。0 で無傷、1 で消滅。
    //   ★ sin で往復させると、両端（無傷・消滅）で長く止まってしまう。
    //     見せたいのは「途中」なので、範囲を 0.10〜0.90 に絞る。
    float amount = 0.50f + 0.40f * sin(t * 0.9f);

    // --- 模様を区画ごとに変える --------------------------------------------
    float pattern;

    if (quadrant.x == 0 && quadrant.y == 0)
    {
        // 左上 : ノイズ（26 章と同じ）。ちぎれるように消える。
        pattern = Fbm(p * 4.0f, 4);
    }
    else if (quadrant.x == 1 && quadrant.y == 0)
    {
        // 右上 : 高さ。下から溶ける。
        //   模様に座標をそのまま使うと「方向を持った消え方」になる。
        pattern = p.y * 0.5f + 0.5f;

        // ふちを揺らすと、ただの直線に見えなくなる
        pattern += (Fbm(p * 6.0f, 3) - 0.5f) * 0.22f;
    }
    else if (quadrant.x == 0 && quadrant.y == 1)
    {
        // 左下 : 中心からの距離。内側から広がる輪。
        pattern = 1.0f - length(p) * 0.85f;
        pattern += (Fbm(p * 5.0f, 3) - 0.5f) * 0.16f;
    }
    else
    {
        // 右下 : ボロノイ。区画ごとに、まとまって消える。
        float2 cell  = floor(p * 4.0f);
        float2 frac1 = frac(p * 4.0f);

        float f1 = 8.0f;
        float id = 0.0f;
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                float2 offset = float2(x, y);
                float2 seed = Hash22(cell + offset);
                float d = length(offset + seed - frac1);
                if (d < f1) { f1 = d; id = Hash21(cell + offset); }
            }
        }
        pattern = id;
    }

    // --- 判定は 4 通りとも同じ ---------------------------------------------
    const float edgeWidth = 0.10f;
    float threshold = amount * (1.0f + edgeWidth) - edgeWidth;

    float3 color = float3(0.05f, 0.06f, 0.09f);

    if (pattern > threshold)
    {
        color = SourceImage(p);

        // 燃え際
        float edge = 1.0f - saturate((pattern - threshold) / edgeWidth);
        edge = pow(edge, 2.0f);

        // 元の絵が透明な（黒い）ところには縁を出さない
        float inside = smoothstep(0.86f, 0.80f, length(p));

        color += float3(1.60f, 0.55f, 0.14f) * edge * inside;
    }

    // 区画の境目
    float2 border = abs(uv - 0.5f);
    color = lerp(color, float3(0.95f, 0.55f, 0.30f),
                 1.0f - smoothstep(0.0f, 0.002f, min(border.x, border.y)));

    return LabOutput(color);
}
