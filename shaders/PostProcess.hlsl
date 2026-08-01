//=============================================================================
// PostProcess.hlsl
//   描き終えた絵をもう一度読み、画面へ出す前に加工するシェーダー。
//
//   ここまでの絵は HDR（1.0 を超える明るさを持つ）。
//   露出 → ブルーム合成 → トーンマッピング → ビネット、の順で処理する。
//   詳しい解説は docs/tutorial/25_ポストプロセス.md を参照。
//=============================================================================
#include "FullScreen.hlsli"

cbuffer PostProcessConstants : register(b0)
{
    // x = 露出（自動が切のときに使う）、y = トーンマッピングの白点、
    // z = ブルームの強さ、w = ビネットの強さ。
    float4 g_params;

    // xy = 画面の大きさ（ピクセル）、zw = その逆数。
    float4 g_screenSize;

    // x = 1 なら自動露出を使う。y, z, w は未使用。
    float4 g_options;
};

// 描き終えたシーン（HDR）。
Texture2D g_scene : register(t0);

// ぼかしたブルーム画像。
Texture2D g_bloom : register(t1);

// 自動露出が求めた値。[0] = 集計中の合計、[4] = 露出。
//   ★ 露出は CPU を経由しない。GPU が測り、GPU が使う。
ByteAddressBuffer g_exposureState : register(t2);

SamplerState g_sampler : register(s0);

FullScreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullScreenVS(vertexId);
}

// リニアの明るさを、画面に出せる 0〜1 の範囲へ圧縮する。
//   拡張版ラインハルト。白点より暗い部分はほとんど変えず、
//   明るい部分だけをなだらかに押し込むので、白飛びが目立たなくなる。
float3 ToneMap(float3 color, float whitePoint)
{
    float3 numerator = color * (1.0f + color / (whitePoint * whitePoint));
    return numerator / (1.0f + color);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float exposure    = g_params.x;
    float whitePoint  = g_params.y;

    if (g_options.x > 0.5f)
    {
        exposure = asfloat(g_exposureState.Load(4));
    }

    float bloomAmount = g_params.z;
    float vignette    = g_params.w;

    // (1) シーンの色（リニア、1.0 を超え得る）
    float3 color = g_scene.Sample(g_sampler, input.uv).rgb;

    // (2) ブルームを足す。
    //   ★ トーンマッピングの「前」に足す。あとで足すと、圧縮済みの絵に
    //     明るさを乗せることになり、白飛びしやすくなる。
    color += g_bloom.Sample(g_sampler, input.uv).rgb * bloomAmount;

    // (3) 露出。カメラの絞りに当たる。掛けるだけ。
    color *= exposure;

    // (4) 0〜1 へ圧縮
    color = ToneMap(color, whitePoint);

    // (5) 周辺減光（ビネット）。画面の隅を落として中央へ目を向けさせる。
    //   ★ 縦横比を掛けて、円形になるようにする。掛けないと楕円になる。
    float2 centered = input.screenPosition;
    centered.x *= g_screenSize.x * g_screenSize.w;   // 幅 / 高さ

    float distance = length(centered) * 0.5f;
    float falloff  = 1.0f - vignette * saturate(distance * distance);

    color *= falloff;

    // 書き込み先は sRGB のレンダーターゲット。GPU が変換してくれる。
    return float4(color, 1.0f);
}
