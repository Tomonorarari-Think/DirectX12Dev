//=============================================================================
// ParticleDraw.hlsl
//   GPU 上のパーティクルを、ビルボードとして描くシェーダー。
//
//   頂点バッファも定数バッファの配列も使わない。
//   構造化バッファを頂点シェーダーから直接読む。
//   詳しい解説は docs/tutorial/29_GPUパーティクル.md を参照。
//=============================================================================
#include "GpuParticle.hlsli"

cbuffer ParticleDrawConstants : register(b0)
{
    float4x4 g_viewProjection;

    // カメラの右方向と上方向（ワールド空間）。板をカメラへ向けるのに使う。
    float4 g_cameraRight;
    float4 g_cameraUp;

    // x = 射影行列の _33、y = 同 _43、z = 消し始める距離、w = 0 でソフト化を切る。
    float4 g_depthParams;
};

// ★ 更新したものを、そのまま読む。CPU を 1 度も経由しない。
StructuredBuffer<GpuParticle> g_particles : register(t0);

// 不透明な物を描き終えた時点の深度（[28 章](28_ソフトパーティクル.md)）。
Texture2D<float> g_sceneDepth : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 local    : TEXCOORD0;
    float3 color    : COLOR;
    float  fadeOut  : TEXCOORD1;
};

static const float2 kCorners[6] =
{
    float2(-1.0f, -1.0f), float2(-1.0f,  1.0f), float2( 1.0f, -1.0f),
    float2( 1.0f, -1.0f), float2(-1.0f,  1.0f), float2( 1.0f,  1.0f),
};


/// 深度バッファの値を、カメラからの距離へ戻す。
float ViewDepth(float ndcDepth)
{
    return g_depthParams.y / (ndcDepth - g_depthParams.x);
}


VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VSOutput output;

    GpuParticle particle = g_particles[instanceId];

    float2 corner = kCorners[vertexId];

    // 残り寿命の割合。1 で生まれたて、0 で消える。
    float life = saturate(particle.velocityLife.w / max(particle.colorLifetime.a, 0.001f));

    // 消えぎわほど小さくする。
    float size = particle.positionSize.w * (0.35f + 0.65f * life);

    float3 worldPosition = particle.positionSize.xyz
                         + g_cameraRight.xyz * corner.x * size
                         + g_cameraUp.xyz    * corner.y * size;

    output.position = mul(float4(worldPosition, 1.0f), g_viewProjection);
    output.local    = corner;

    // 冷めるほど暗く、赤くなる。
    output.color   = particle.colorLifetime.rgb * life * life;
    output.fadeOut = life;

    return output;
}


float4 PSMain(VSOutput input) : SV_TARGET
{
    float distance = length(input.local);

    // 中心が明るく、縁が 0 になる丸。
    float alpha = saturate(1.0f - distance);
    alpha = pow(alpha, 2.2f);

    if (g_depthParams.w > 0.5f)
    {
        float sceneNdcDepth = g_sceneDepth.Load(int3((int2)input.position.xy, 0));

        float sceneDistance    = ViewDepth(sceneNdcDepth);
        float particleDistance = ViewDepth(input.position.z);

        alpha *= saturate((sceneDistance - particleDistance) / g_depthParams.z);
    }

    // 加算合成なので、そのまま足される色を返す。
    return float4(input.color * alpha, alpha);
}
