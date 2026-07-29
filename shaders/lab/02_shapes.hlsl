//=============================================================================
// 02_shapes.hlsl
//   距離関数（SDF）で図形を描く。
//
//   「その点から図形までの距離」を返す関数さえあれば、
//   塗り・輪郭・影・角の丸めが、すべて同じ仕組みで作れる。
//   解説 : docs/shader-lab/02_距離関数で図形を描く.md
//=============================================================================
#include "LabCommon.hlsli"

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 p = ToCenteredUv(input.uv);
    float  t = g_time.x;

    // 背景
    float3 color = float3(0.09f, 0.10f, 0.13f);

    // --- (1) 円 -------------------------------------------------------------
    //   中が負、外が正の「符号付き距離」。0 のところが輪郭。
    float circle = SdCircle(p - float2(-0.9f, 0.35f), 0.28f);
    color = lerp(color, float3(0.30f, 0.62f, 0.95f), FillMask(circle));

    // --- (2) 角の丸い矩形 ---------------------------------------------------
    //   丸め半径ぶん内側で距離を測り、あとから引くだけで角が丸くなる。
    float box = SdRoundedBox(p - float2(-0.1f, 0.35f), float2(0.30f, 0.22f), 0.09f);
    color = lerp(color, float3(0.95f, 0.72f, 0.28f), FillMask(box));

    // --- (3) 線分 -----------------------------------------------------------
    //   「線分上のいちばん近い点」までの距離。太さは輪郭の幅で決める。
    float2 a = float2(0.55f, 0.12f);
    float2 b = float2(1.15f, 0.58f);
    float segment = SdSegment(p, a, b) - 0.03f;
    color = lerp(color, float3(0.45f, 0.90f, 0.55f), FillMask(segment));

    // --- (4) 輪郭だけ描く ---------------------------------------------------
    float ring = SdCircle(p - float2(-0.9f, -0.45f), 0.26f);
    color = lerp(color, float3(0.95f, 0.45f, 0.40f), StrokeMask(ring, 0.02f));

    // --- (5) 2 つの図形を組み合わせる ---------------------------------------
    //   min = 和（どちらか）、max = 積（両方）、max(a, -b) = 差（片方から抜く）。
    float2 q = p - float2(-0.1f, -0.45f);
    float c1 = SdCircle(q - float2(-0.11f, 0.0f), 0.22f);
    float c2 = SdCircle(q - float2( 0.11f, 0.0f), 0.22f);

    // 差集合。左の円から右の円をくり抜く。
    float difference = max(c1, -c2);
    color = lerp(color, float3(0.72f, 0.55f, 0.95f), FillMask(difference));

    // --- (6) なめらかに繋ぐ（スムーズ和）-----------------------------------
    //   min をそのまま使うと繋ぎ目が角張る。重なり具合で補間すると
    //   水滴どうしが合わさるような丸みが出る。
    float2 r = p - float2(0.85f, -0.45f);
    float d1 = SdCircle(r - float2(-0.16f, 0.0f), 0.18f);
    float d2 = SdCircle(r - float2( 0.16f * cos(t), 0.10f * sin(t)), 0.18f);

    float k = 0.14f;
    float h = saturate(0.5f + 0.5f * (d2 - d1) / k);
    float smoothUnion = lerp(d2, d1, h) - k * h * (1.0f - h);

    color = lerp(color, float3(0.35f, 0.85f, 0.90f), FillMask(smoothUnion));

    // --- 距離そのものを縞で見せる（下の帯）---------------------------------
    if (input.uv.y > 0.88f)
    {
        float d = SdCircle(p - float2(0.0f, -0.78f), 0.10f);

        // 距離を 0.05 ごとの縞にする。負の側（内側）は色を変える。
        float bands = 0.5f + 0.5f * cos(d * 80.0f);
        float3 inside  = float3(0.95f, 0.60f, 0.35f);
        float3 outside = float3(0.35f, 0.55f, 0.80f);
        color = lerp(outside, inside, step(d, 0.0f)) * (0.45f + 0.55f * bands);
    }

    return LabOutput(color);
}
