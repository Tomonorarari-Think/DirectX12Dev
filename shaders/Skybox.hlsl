//=============================================================================
// Skybox.hlsl
//   背景に環境マップを描くシェーダー。
//
//   頂点バッファを使わず、頂点 ID から画面いっぱいの三角形を作る。
//   詳しい解説は docs/tutorial/23_スカイボックス.md を参照。
//=============================================================================
#include "FullScreen.hlsli"

cbuffer SkyboxConstants : register(b0)
{
    // カメラの 3 軸（ワールド空間、正規化済み）。w は未使用。
    // 位置は渡さない。背景は無限に遠いものとして描くため。
    float4 g_cameraRight;
    float4 g_cameraUp;
    float4 g_cameraForward;

    // x = 画角の半分の正接、y = 縦横比、z = 環境光（IBL）の強さ。w は未使用。
    float4 g_skyboxParams;
};

Texture2D    g_environment : register(t0);
SamplerState g_sampler     : register(s0);

// 円周率。
static const float kPi = 3.14159265f;

FullScreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullScreenVS(vertexId);
}

// 方向ベクトルを、正距円筒図法のテクスチャ座標へ変換する。
//   Mesh.hlsl の DirectionToEquirectUv と同じ式。
float2 DirectionToEquirectUv(float3 direction)
{
    float u = atan2(direction.z, direction.x) / (2.0f * kPi) + 0.5f;
    float v = acos(clamp(direction.y, -1.0f, 1.0f)) / kPi;
    return float2(u, v);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    // 画面の位置から視線の向きを組み立てる。
    //   画面の端は、前方から「画角の半分」だけ傾いた向きになる。
    //   横は縦横比のぶんだけ広がる。
    float tanHalfFov = g_skyboxParams.x;
    float aspect     = g_skyboxParams.y;

    float3 direction = normalize(
          g_cameraForward.xyz
        + g_cameraRight.xyz * (input.screenPosition.x * tanHalfFov * aspect)
        + g_cameraUp.xyz    * (input.screenPosition.y * tanHalfFov));

    float3 color = g_environment.SampleLevel(
        g_sampler, DirectionToEquirectUv(direction), 0.0f).rgb;

    // 物体の環境光と同じ倍率を掛ける。
    //   ★ 圧縮（トーンマッピング）はここではしない。後処理でまとめて行う。
    return float4(color * g_skyboxParams.z, 1.0f);
}
