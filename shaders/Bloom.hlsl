//=============================================================================
// Bloom.hlsl
//   明るい部分だけを取り出し、ぼかして「にじみ」を作るシェーダー。
//
//   1. しきい値抽出 : 明るい所だけを残す（同時に 1/2 に縮小）
//   2. ぼかし       : 横 → 縦の 2 回に分けて行う
//   詳しい解説は docs/tutorial/25_ポストプロセス.md を参照。
//=============================================================================
#include "FullScreen.hlsli"

cbuffer BloomConstants : register(b0)
{
    // x = しきい値、y = 立ち上がりのなだらかさ、z, w = 未使用。
    float4 g_params;

    // xy = ずらす向き（ピクセル単位）、zw = 入力テクスチャの 1 テクセルの大きさ。
    float4 g_blurDirection;
};

Texture2D    g_source  : register(t0);
SamplerState g_sampler : register(s0);

FullScreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullScreenVS(vertexId);
}

// 明るい部分だけを残す。
//   単純に「しきい値未満なら 0」にすると、境目がくっきり出て不自然になる。
//   smoothstep でなだらかに立ち上げると、境目が目立たない。
float4 PSThreshold(FullScreenVSOutput input) : SV_TARGET
{
    float threshold = g_params.x;
    float softness  = g_params.y;

    float3 color = g_source.Sample(g_sampler, input.uv).rgb;

    // 明るさの代表値。最大成分を使うと、赤や青だけが強い色も拾える。
    float brightness = max(color.r, max(color.g, color.b));

    float weight = smoothstep(threshold, threshold + softness, brightness);

    return float4(color * weight, 1.0f);
}

// 1 方向のぼかし。横と縦の 2 回に分けて呼ぶ。
//   ★ 2 次元のぼかしを 1 次元 2 回で済ませられるのは、ガウス関数が
//     縦横に分解できるため（分離可能）。9x9 なら 81 回が 18 回で済む。
float4 PSBlur(FullScreenVSOutput input) : SV_TARGET
{
    // 5 タップぶんの重み（ガウス分布に近い値）。中心 → 外側の順。
    const float weights[5] = { 0.227027f, 0.194594f, 0.121621f,
                               0.054054f, 0.016216f };

    float2 step = g_blurDirection.xy * g_blurDirection.zw;

    float3 sum = g_source.Sample(g_sampler, input.uv).rgb * weights[0];

    [unroll]
    for (int i = 1; i < 5; ++i)
    {
        float2 offset = step * i;
        sum += g_source.Sample(g_sampler, input.uv + offset).rgb * weights[i];
        sum += g_source.Sample(g_sampler, input.uv - offset).rgb * weights[i];
    }

    return float4(sum, 1.0f);
}
