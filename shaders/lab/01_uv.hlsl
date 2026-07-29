//=============================================================================
// 01_uv.hlsl
//   すべての出発点 : 「いま塗っているのは画面のどこか」を色にする。
//
//   ピクセルシェーダーが知っているのは自分の座標だけ。
//   そこから何が作れるかを見るために、まず座標そのものを色にしてみる。
//   解説 : docs/shader-lab/01_UVと座標.md
//=============================================================================
#include "LabCommon.hlsli"

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;

    // 画面を 2 x 2 に割り、区画ごとに違う見せ方をする。
    //   各区画の中で、もう一度 0〜1 の座標を作り直す。
    int2   quadrant = (int2)(uv * 2.0f);          // (0,0) 〜 (1,1)
    float2 local    = frac(uv * 2.0f);            // 区画の中で 0〜1

    // 区画の中で「中心が原点・上が +Y」の座標。区画は横長なので比率を掛ける。
    float2 p = local * 2.0f - 1.0f;
    p.y = -p.y;
    p.x *= g_resolution.x * g_resolution.w;

    float3 color;

    if (quadrant.x == 0 && quadrant.y == 0)
    {
        // --- 左上 : U を赤、V を緑にそのまま流す ---------------------------
        //   左上が黒、右上が赤、左下が緑、右下が黄になる。
        //   「座標がそのまま色になる」ことを確かめる、いちばん基本の絵。
        color = float3(local.x, local.y, 0.25f);
    }
    else if (quadrant.x == 1 && quadrant.y == 0)
    {
        // --- 右上 : 中心からの距離 -----------------------------------------
        //   length() は原点からの長さ。等距離の点が同じ色になるので同心円。
        //   frac で折り返すと、等高線のような縞になる。
        float r = length(p);
        float rings = frac(r * 4.0f);

        color = lerp(float3(0.10f, 0.12f, 0.16f),
                     float3(0.35f, 0.75f, 0.95f), rings);

        // 中央に半径 1 の円を重ねて、目盛りを分かりやすくする。
        color = lerp(color, float3(0.98f, 0.85f, 0.35f),
                     StrokeMask(r - 1.0f, 0.012f));
    }
    else if (quadrant.x == 0 && quadrant.y == 1)
    {
        // --- 左下 : 中心から見た角度 ---------------------------------------
        //   atan2(y, x) は -π〜π。0〜1 に直して色の帯として使うと、
        //   ぐるりと 1 周してもとの色に戻る。
        float angle = atan2(p.y, p.x) / kTau + 0.5f;
        color = PaletteWarm(angle);

        // 角度の目盛り（12 等分）
        float ticks = abs(frac(angle * 12.0f) - 0.5f);
        color *= 0.65f + 0.35f * smoothstep(0.02f, 0.06f, ticks);
    }
    else
    {
        // --- 右下 : frac で座標を折り返す ----------------------------------
        //   小数部だけを取り出すと、1 ごとに 0 へ戻る。
        //   これがタイリング（繰り返し）の原理そのもの。
        float2 tiled = frac(p * 3.0f);
        color = float3(tiled.x, tiled.y, 0.35f);

        // マスの境目
        float2 edge = abs(tiled - 0.5f);
        color = lerp(color * 0.35f, color,
                     smoothstep(0.48f, 0.46f, max(edge.x, edge.y)));
    }

    // 区画の境目に線を引く
    float2 border = abs(uv - 0.5f);
    float lineMask = 1.0f - smoothstep(0.0f, 0.002f, min(border.x, border.y));
    color = lerp(color, float3(0.95f, 0.55f, 0.30f), lineMask);

    return LabOutput(color);
}
