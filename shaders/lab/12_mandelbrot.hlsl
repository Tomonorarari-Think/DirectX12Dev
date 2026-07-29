//=============================================================================
// 12_mandelbrot.hlsl
//   マンデルブロ集合 : z = z² + c を繰り返すだけ。
//
//   たった 1 行の漸化式が、いくら拡大しても尽きない模様を作る。
//   「発散するまでの回数」を色にするのが定石。
//   解説 : docs/shader-lab/12_マンデルブロ集合.md
//=============================================================================
#include "LabCommon.hlsli"

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 p = ToCenteredUv(input.uv);
    float  t = g_time.x;

    // ゆっくり拡大しながら、面白い場所へ寄っていく。
    //   指数で動かすと、拡大の速さが見た目で一定になる。
    float zoom = exp(-1.0f - 2.4f * (0.5f + 0.5f * sin(t * 0.10f)));
    float2 center = float2(-0.7453f, 0.1127f);   // シーホースバレー

    float2 c = center + p * zoom;

    // --- 反復 ---------------------------------------------------------------
    float2 z = float2(0.0f, 0.0f);
    int    iteration = 0;
    const int kMaxIteration = 220;

    // |z| が 2 を超えたら、以降は必ず発散する（証明済み）。
    //   比較は 2 乗のままにして sqrt を省く。
    for (int i = 0; i < kMaxIteration; ++i)
    {
        // 複素数の 2 乗 : (a + bi)² = a² - b² + 2abi
        z = float2(z.x * z.x - z.y * z.y, 2.0f * z.x * z.y) + c;

        if (dot(z, z) > 4.0f)
        {
            break;
        }

        ++iteration;
    }

    float3 color;

    if (iteration >= kMaxIteration)
    {
        // 発散しなかった＝集合の内側。真っ黒にする。
        color = float3(0.0f, 0.0f, 0.0f);
    }
    else
    {
        // --- なめらかな色付け -----------------------------------------------
        //   回数は整数なので、そのまま色にすると縞が段になる。
        //   最後の |z| を使って小数部を補うと、なめらかに繋がる。
        float smoothCount = (float)iteration
                          - log2(max(log2(length(z)), 1e-6f)) + 4.0f;

        float value = smoothCount * 0.035f;

        color = Palette(value,
                        float3(0.50f, 0.50f, 0.50f),
                        float3(0.50f, 0.50f, 0.50f),
                        float3(1.00f, 1.00f, 1.00f),
                        float3(0.00f, 0.15f, 0.35f));

        // 縁を少し暗くして、模様の骨格を見せる。
        color *= 0.35f + 0.65f * saturate(smoothCount / 40.0f);
    }

    // 左下 : 反復のようすを図にする（|z| の推移）
    if (input.uv.x < 0.22f && input.uv.y > 0.78f)
    {
        float2 q = (input.uv - float2(0.0f, 0.78f)) / float2(0.22f, 0.22f);
        q.y = 1.0f - q.y;

        // 横 = 何回目、縦 = |z|（0〜3 を表示）
        int step1 = (int)(q.x * 24.0f);

        float2 zz = float2(0.0f, 0.0f);
        float len = 0.0f;
        for (int k = 0; k <= step1; ++k)
        {
            zz = float2(zz.x * zz.x - zz.y * zz.y, 2.0f * zz.x * zz.y) + c;
            len = length(zz);
            if (len > 8.0f) { break; }
        }

        float bar = 1.0f - smoothstep(0.0f, 0.03f, abs(q.y - saturate(len / 3.0f)));
        float limit = 1.0f - smoothstep(0.0f, 0.01f, abs(q.y - 2.0f / 3.0f));

        color = float3(0.08f, 0.09f, 0.12f);
        color = lerp(color, float3(0.95f, 0.40f, 0.35f), limit);
        color = lerp(color, float3(0.40f, 0.85f, 0.95f), bar);
    }

    return LabOutput(color);
}
