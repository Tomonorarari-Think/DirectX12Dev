//=============================================================================
// 08_palette.hlsl
//   cos だけで作る連続的なカラーパレット。
//
//   色 = a + b * cos(2π(c*t + d))
//   4 つのベクトルを選ぶだけで、途切れずに繋がる色の帯が作れる。
//   解説 : docs/shader-lab/08_カラーパレット.md
//=============================================================================
#include "LabCommon.hlsli"

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    // 6 本の帯を縦に並べ、パラメータの違いを見比べる。
    const int kBandCount = 6;

    float band  = uv.y * kBandCount;
    int   index = min((int)band, kBandCount - 1);
    float inner = frac(band);

    // 横方向が t（0〜1）。時間で少しずらして流す。
    float x = uv.x + t * 0.05f;

    float3 a, b, c, d;

    if (index == 0)
    {
        // 基本形 : 3 成分とも同じ位相 → 灰色の濃淡
        a = float3(0.5f, 0.5f, 0.5f);
        b = float3(0.5f, 0.5f, 0.5f);
        c = float3(1.0f, 1.0f, 1.0f);
        d = float3(0.0f, 0.0f, 0.0f);
    }
    else if (index == 1)
    {
        // 位相を少しずつずらす → 虹のような帯
        a = float3(0.5f, 0.5f, 0.5f);
        b = float3(0.5f, 0.5f, 0.5f);
        c = float3(1.0f, 1.0f, 1.0f);
        d = float3(0.0f, 0.33f, 0.67f);
    }
    else if (index == 2)
    {
        // 本プロジェクトでよく使う色味（青 → 桃 → 黄）
        a = float3(0.5f, 0.5f, 0.5f);
        b = float3(0.5f, 0.5f, 0.5f);
        c = float3(1.0f, 1.0f, 1.0f);
        d = float3(0.0f, 0.10f, 0.20f);
    }
    else if (index == 3)
    {
        // c を成分ごとに変える → 繰り返しの速さが違い、複雑になる
        a = float3(0.5f, 0.5f, 0.5f);
        b = float3(0.5f, 0.5f, 0.5f);
        c = float3(1.0f, 1.0f, 0.5f);
        d = float3(0.8f, 0.90f, 0.30f);
    }
    else if (index == 4)
    {
        // b を小さくする → 振れ幅が減り、落ち着いた色になる
        a = float3(0.5f, 0.5f, 0.5f);
        b = float3(0.2f, 0.3f, 0.2f);
        c = float3(1.0f, 1.0f, 1.0f);
        d = float3(0.0f, 0.25f, 0.25f);
    }
    else
    {
        // a を偏らせる → 全体が暖色（寒色）に寄る
        a = float3(0.8f, 0.5f, 0.4f);
        b = float3(0.2f, 0.4f, 0.2f);
        c = float3(2.0f, 1.0f, 1.0f);
        d = float3(0.0f, 0.25f, 0.25f);
    }

    float3 color = Palette(x, a, b, c, d);

    // 帯の中の下 1/4 に、成分ごとの波形を重ねる。
    if (inner > 0.72f)
    {
        float y = 1.0f - (inner - 0.72f) / 0.28f;   // 0〜1

        float3 wave = color;   // Palette の値そのもの
        float3 mask = float3(
            1.0f - smoothstep(0.0f, 0.02f, abs(y - saturate(wave.r))),
            1.0f - smoothstep(0.0f, 0.02f, abs(y - saturate(wave.g))),
            1.0f - smoothstep(0.0f, 0.02f, abs(y - saturate(wave.b))));

        float3 back = float3(0.08f, 0.09f, 0.11f);
        color = back
              + float3(1.0f, 0.25f, 0.25f) * mask.r
              + float3(0.25f, 1.0f, 0.25f) * mask.g
              + float3(0.30f, 0.45f, 1.0f) * mask.b;
    }

    // 帯の境目
    float sep = 1.0f - smoothstep(0.0f, 0.004f, min(inner, 1.0f - inner));
    color = lerp(color, float3(0.05f, 0.06f, 0.08f), sep);

    return LabOutput(color);
}
