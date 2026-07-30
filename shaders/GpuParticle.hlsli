//=============================================================================
// GpuParticle.hlsli
//   GPU 上に置くパーティクル 1 個ぶんの並び。
//
//   更新（コンピュート）と描画（頂点）の両方から読むので、
//   定義を 1 か所にまとめる。C++ 側の GpuParticle と一致させること。
//=============================================================================
#ifndef GPU_PARTICLE_HLSLI
#define GPU_PARTICLE_HLSLI

struct GpuParticle
{
    // xyz = 位置（ワールド）、w = 大きさ（半径）。
    float4 positionSize;

    // xyz = 速度（毎秒）、w = 残り寿命（秒）。
    float4 velocityLife;

    // rgb = 色（リニア）、a = 最初の寿命（秒）。
    float4 colorLifetime;
};

#endif // GPU_PARTICLE_HLSLI
