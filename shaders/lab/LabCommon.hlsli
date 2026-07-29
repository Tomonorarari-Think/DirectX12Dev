//=============================================================================
// LabCommon.hlsli
//   シェーダー習作で共通に使う道具箱。
//
//   ここに置いてあるのは「画面を絵で埋めるための部品」で、
//   3D モデルの描画とは独立している。
//   詳しい解説は docs/shader-lab/ を参照。
//=============================================================================
#ifndef LAB_COMMON_HLSLI
#define LAB_COMMON_HLSLI

#include "../FullScreen.hlsli"

//-----------------------------------------------------------------------------
// 共通の入力
//-----------------------------------------------------------------------------
cbuffer LabConstants : register(b0)
{
    // x = 経過秒、y = 前フレームからの秒、z = 何番目のシェーダーか、w = 未使用。
    float4 g_time;

    // xy = 画面の大きさ（ピクセル）、zw = その逆数。
    float4 g_resolution;

    // xy = マウスの位置（ピクセル）、z = 左ボタンが押されていれば 1。
    float4 g_mouse;
};

static const float kPi  = 3.14159265f;
static const float kTau = 6.28318531f;

//-----------------------------------------------------------------------------
// 座標
//-----------------------------------------------------------------------------

/// テクスチャ座標（左上が 0、右下が 1）を、
/// 「中心が原点・上が +Y・縦が -1〜1」の座標へ直す。
///   横は縦横比のぶんだけ広がるので、円を描けば画面でも円になる。
float2 ToCenteredUv(float2 uv01)
{
    float2 p = uv01 * 2.0f - 1.0f;   // -1〜1（ただし Y は下向き）
    p.y = -p.y;                      // 上を +Y にする
    p.x *= g_resolution.x * g_resolution.w;   // 幅 / 高さ
    return p;
}

/// テクスチャ座標をピクセル座標へ。
float2 ToPixel(float2 uv01)
{
    return uv01 * g_resolution.xy;
}

/// 2 次元の回転行列。
float2x2 Rotate2D(float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2x2(c, -s, s, c);
}

//-----------------------------------------------------------------------------
// 乱数とノイズ
//-----------------------------------------------------------------------------

/// 座標から 0〜1 の「決まった乱数」を作る（ハッシュ）。
///   同じ座標なら必ず同じ値になるので、毎フレーム変わらない模様が作れる。
///
///   ★ 途中の値を 0〜1 付近に保つのが肝心。
///     `frac(sin(dot(p, 大きな数)) * 43758.5)` という書き方が有名だが、
///     32 bit 浮動小数点では途中で桁が溢れ、**縦縞や階段が出る**。
///     ここでは Dave Hoskins の書き方（値が小さいまま完結する）を使う。
float Hash21(float2 p)
{
    float3 p3 = frac(p.xyx * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

/// 座標から 0〜1 の乱数を 2 つ作る。
float2 Hash22(float2 p)
{
    float3 p3 = frac(p.xyx * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.xx + p3.yz) * p3.zy);
}

/// 値ノイズ。格子の頂点に乱数を置き、あいだをなめらかに補間する。
float ValueNoise(float2 p)
{
    float2 cell  = floor(p);
    float2 local = frac(p);

    // 直線で補間すると格子が見えるので、両端で傾きが 0 になる曲線を使う。
    float2 weight = local * local * (3.0f - 2.0f * local);

    float a = Hash21(cell + float2(0.0f, 0.0f));
    float b = Hash21(cell + float2(1.0f, 0.0f));
    float c = Hash21(cell + float2(0.0f, 1.0f));
    float d = Hash21(cell + float2(1.0f, 1.0f));

    return lerp(lerp(a, b, weight.x), lerp(c, d, weight.x), weight.y);
}

/// 細かさを変えたノイズを重ねる（fractional Brownian motion）。
///   1 回ごとに 2 倍細かく、半分の強さで足す。自然物の「ざらつき」に近くなる。
float Fbm(float2 p, int octaves)
{
    float sum       = 0.0f;
    float amplitude = 0.5f;

    for (int i = 0; i < octaves; ++i)
    {
        sum += ValueNoise(p) * amplitude;
        p *= 2.0f;
        amplitude *= 0.5f;
    }

    return sum;
}

//-----------------------------------------------------------------------------
// 距離関数（SDF）
//-----------------------------------------------------------------------------

/// 円までの符号付き距離。中が負、外が正。
float SdCircle(float2 p, float radius)
{
    return length(p) - radius;
}

/// 角の丸い矩形までの符号付き距離。
float SdRoundedBox(float2 p, float2 halfSize, float radius)
{
    float2 d = abs(p) - halfSize + radius;
    return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f) - radius;
}

/// 線分までの距離。
float SdSegment(float2 p, float2 a, float2 b)
{
    float2 pa = p - a;
    float2 ba = b - a;
    float t = saturate(dot(pa, ba) / dot(ba, ba));
    return length(pa - ba * t);
}

/// 距離を「塗り」に変える。境目を 1 ピクセルぶんだけぼかす。
///   fwidth は「隣のピクセルとの差」で、拡大率が変わっても太さが保たれる。
float FillMask(float distance)
{
    float width = fwidth(distance);
    return 1.0f - smoothstep(-width, width, distance);
}

/// 距離を「輪郭線」に変える。
float StrokeMask(float distance, float thickness)
{
    float width = fwidth(distance);
    return 1.0f - smoothstep(thickness - width, thickness + width, abs(distance));
}

/// 0〜1 の座標を n 等分したときの「仕切り線」を返す。
///   frac(x*n) が 0（＝ 区画の境目）に近いところで 1 になる。
float DividerMask(float x, float n, float width)
{
    float d = abs(frac(x * n) - 0.5f);
    return smoothstep(0.5f - width, 0.5f, d);
}

/// 0〜1 の矩形の縁を返す。
float FrameMask(float2 q, float width)
{
    float d = min(min(q.x, 1.0f - q.x), min(q.y, 1.0f - q.y));
    return 1.0f - smoothstep(0.0f, width, d);
}

//-----------------------------------------------------------------------------
// 色
//-----------------------------------------------------------------------------

/// cos を使った連続的なパレット（Inigo Quilez）。
///   4 つのベクトルだけで、なめらかにつながる色の帯を作れる。
///   a = 明るさの中心、b = 振れ幅、c = 繰り返しの速さ、d = 位相のずれ。
float3 Palette(float t, float3 a, float3 b, float3 c, float3 d)
{
    return a + b * cos(kTau * (c * t + d));
}

/// よく使う色味（青 → 桃 → 黄）。
float3 PaletteWarm(float t)
{
    return Palette(t,
                   float3(0.50f, 0.50f, 0.50f),
                   float3(0.50f, 0.50f, 0.50f),
                   float3(1.00f, 1.00f, 1.00f),
                   float3(0.00f, 0.10f, 0.20f));
}

/// sRGB の記録値をリニアの明るさへ戻す。
float3 SrgbToLinear(float3 color)
{
    color = saturate(color);
    return (color <= 0.04045f) ? (color / 12.92f)
                               : pow((color + 0.055f) / 1.055f, 2.4f);
}

/// 習作の出力。
///   習作では「画面に出したい色」をそのまま組み立てる。
///   書き込み先が sRGB のレンダーターゲットで、GPU が
///   リニア → sRGB の変換を掛けるので、その逆を先に通して打ち消す。
///   こうしないと、意図した色より明るく・浅く出てしまう。
float4 LabOutput(float3 displayColor)
{
    return float4(SrgbToLinear(displayColor), 1.0f);
}

#endif // LAB_COMMON_HLSLI
