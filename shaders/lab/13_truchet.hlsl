//=============================================================================
// 13_truchet.hlsl
//   トルシェタイル : 2 種類の絵をランダムに並べるだけで迷路が現れる。
//
//   1 マスに「四分円 2 本」を置き、向きをコイン投げで決める。
//   それだけで、どこまでも繋がる曲線の網ができる。
//   解説 : docs/shader-lab/13_トルシェタイル.md
//=============================================================================
#include "LabCommon.hlsli"

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    // ゆっくりスクロールさせる
    float2 p = ToCenteredUv(uv) * 4.0f + float2(t * 0.25f, t * 0.12f);

    float2 cell  = floor(p);
    float2 local = frac(p) - 0.5f;   // -0.5〜0.5

    // --- マスごとにコインを投げる -------------------------------------------
    //   0.5 より大きければ、マスの中身を反転させる。
    float flip = Hash21(cell);

    if (flip > 0.5f)
    {
        local.x = -local.x;
    }

    // --- 四分円 2 本までの距離 ----------------------------------------------
    //   左下の角と右上の角を中心にした、半径 0.5 の円弧を 2 本描く。
    //   `length(p - 角) - 0.5` が「円周までの距離」なので、
    //   その絶対値が小さいところが線になる。近いほうを採る。
    float arcA = abs(length(local - float2(-0.5f, -0.5f)) - 0.5f);
    float arcB = abs(length(local - float2( 0.5f,  0.5f)) - 0.5f);
    float d = min(arcA, arcB);

    // 線の太さ
    const float thickness = 0.09f;

    float lineMask = smoothstep(thickness + 0.02f, thickness - 0.02f, d);

    // --- 色 -----------------------------------------------------------------
    //   線に沿って進んだ角度を色にすると、繋がりが見える。
    float along = (arcA < arcB)
                ? atan2(local.y + 0.5f, local.x + 0.5f)
                : atan2(0.5f - local.y, 0.5f - local.x);
    along /= kTau;
    float3 lineColor = PaletteWarm(along * 0.5f + Hash21(cell) * 0.3f + t * 0.05f);

    float3 back = float3(0.06f, 0.07f, 0.10f);
    float3 color = lerp(back, lineColor, lineMask);

    // マスの境目をうっすら出す
    float2 edge = abs(frac(p) - 0.5f);
    float grid = 1.0f - smoothstep(0.47f, 0.495f, max(edge.x, edge.y));
    color = lerp(color * 1.25f, color, grid);

    // --- 右上 : 1 マスぶんを拡大して見せる ---------------------------------
    if (uv.x > 0.72f && uv.y < 0.28f)
    {
        float2 q = (uv - float2(0.72f, 0.0f)) / 0.28f;   // 0〜1
        q.y = 1.0f - q.y;

        // 左半分が「そのまま」、右半分が「反転」
        float2 l = frac(q * float2(2.0f, 1.0f)) - 0.5f;
        if (q.x > 0.5f) { l.x = -l.x; }

        float dd = min(abs(length(l - float2(-0.5f, -0.5f)) - 0.5f),
                       abs(length(l - float2( 0.5f,  0.5f)) - 0.5f));
        float mask = smoothstep(0.10f, 0.06f, dd);

        color = lerp(float3(0.12f, 0.13f, 0.16f),
                     float3(0.95f, 0.75f, 0.35f), mask);

        float sep = 1.0f - smoothstep(0.0f, 0.008f, abs(q.x - 0.5f));
        color = lerp(color, float3(0.95f, 0.45f, 0.30f), sep);

        float frame = 1.0f - smoothstep(0.0f, 0.02f,
                                        min(min(q.x, 1.0f - q.x),
                                            min(q.y, 1.0f - q.y)));
        color = lerp(color, float3(0.95f, 0.45f, 0.30f), frame);
    }

    return LabOutput(color);
}
