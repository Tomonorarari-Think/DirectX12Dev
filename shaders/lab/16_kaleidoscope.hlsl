//=============================================================================
// 16_kaleidoscope.hlsl
//   万華鏡 : 角度を折り返して鏡を作る。
//
//   極座標にして角度を繰り返し、さらに `abs` で折り返すと鏡映になる。
//   もとの模様が何であっても、対称な絵になる。
//   解説 : docs/shader-lab/16_万華鏡.md
//=============================================================================
#include "LabCommon.hlsli"

/// 万華鏡の元になる模様。ここを差し替えれば見え方がまるごと変わる。
float3 SourcePattern(float2 p, float time)
{
    // ドメインワープをかけた fBm（07 番の縮小版）
    float2 q = float2(Fbm(p + time * 0.05f, 4),
                      Fbm(p + float2(4.7f, 2.3f) - time * 0.04f, 4));

    float value = Fbm(p + 2.4f * q, 5);

    float3 color = PaletteWarm(value * 1.5f + time * 0.02f);

    // 小さな円をいくつか散らして、対称性を見えやすくする
    float2 cell  = floor(p * 2.0f);
    float2 local = frac(p * 2.0f) - 0.5f;
    float seed   = Hash21(cell);

    float dots = FillMask(SdCircle(local, 0.10f + 0.10f * seed));
    color = lerp(color, float3(1.0f, 0.97f, 0.90f), dots * 0.55f);

    return color;
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv);

    // --- (1) 極座標へ -------------------------------------------------------
    float radius = length(p);
    float angle  = atan2(p.y, p.x);

    // --- (2) 角度を n 等分して繰り返す --------------------------------------
    //   0〜1 に直してから frac を取るのは 03 番と同じ。
    const float kSectors = 8.0f;
    float sectorSize = kTau / kSectors;

    // ゆっくり回す
    angle += t * 0.10f;

    // 区画の中での角度（-半分 〜 +半分）
    float local = angle - sectorSize * floor(angle / sectorSize + 0.5f);

    // --- (3) さらに折り返して鏡にする ---------------------------------------
    //   ★ ここが万華鏡の肝。abs を取ると、区画の中央を軸にした鏡映になる。
    //     繰り返しだけだと「回転対称」、折り返すと「鏡映対称」。
    local = abs(local);

    // --- (4) 直交座標へ戻す -------------------------------------------------
    float2 folded = float2(cos(local), sin(local)) * radius;

    // 半径方向にも折り返すと、外へ向かって模様が反復する
    float ringSize = 0.55f;
    float ringIndex = floor(radius / ringSize);
    float ringLocal = frac(radius / ringSize);

    // 奇数番目の輪は内外を反転させ、継ぎ目を目立たなくする
    if (fmod(ringIndex, 2.0f) > 0.5f)
    {
        ringLocal = 1.0f - ringLocal;
    }

    folded = float2(cos(local), sin(local)) * (ringLocal * ringSize + 0.25f);

    // --- (5) 元の模様を引く -------------------------------------------------
    float3 color = SourcePattern(folded * 3.0f, t);

    // 中心を少し明るく、外を落とす
    color *= 1.15f - 0.55f * saturate(radius * 0.7f);

    // 鏡の境目を薄く見せる
    float seam = 1.0f - smoothstep(0.0f, 0.008f, abs(local));
    color = lerp(color, color * 1.35f + 0.06f, seam * 0.6f);

    return LabOutput(color);
}
