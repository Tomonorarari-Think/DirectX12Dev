//=============================================================================
// ParticleUpdate.hlsl
//   パーティクルを GPU 上で 1 フレームぶん進めるコンピュートシェーダー。
//
//   CPU は「何スレッド走らせるか」しか言わない。位置も速度も CPU へ戻さない。
//   詳しい解説は docs/tutorial/29_GPUパーティクル.md を参照。
//=============================================================================
#include "GpuParticle.hlsli"

// 1 グループあたりのスレッド数。
//   ★ ハードウェアは 32 か 64 単位で動くので、その倍数にする。
//     半端な数にすると、余ったレーンが遊ぶ。
#define THREAD_GROUP_SIZE 64

cbuffer ParticleUpdateConstants : register(b0)
{
    // x = 前フレームからの秒、y = 経過秒、z = パーティクルの総数、w = 未使用。
    float4 g_timing;

    // xyz = 湧き出し口の位置、w = 初速の大きさ。
    float4 g_emitter;

    // xyz = 重力、w = 床の高さ。
    float4 g_gravity;
};

// ★ RW が付くと「読み書きできる」。読むだけなら StructuredBuffer。
RWStructuredBuffer<GpuParticle> g_particles : register(u0);


/// 座標から決まった乱数を作る（Dave Hoskins 版）。
float Hash11(float value)
{
    float3 p = frac(float3(value, value, value) * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}


/// 単位球の中の 1 点を、だいたい一様に選ぶ。
float3 RandomDirection(float seed)
{
    float u = Hash11(seed) * 2.0f - 1.0f;          // cos θ
    float a = Hash11(seed + 7.7f) * 6.2831853f;    // φ

    float r = sqrt(max(1.0f - u * u, 0.0f));

    return float3(r * cos(a), u, r * sin(a));
}


/// 死んだパーティクルを湧き出し口へ戻す。
GpuParticle Respawn(uint index, float time)
{
    // ★ 添字と時刻を混ぜて種にする。
    //   添字だけだと、何度生き返っても毎回まったく同じ軌道になる。
    float seed = (float)index * 1.618f + floor(time * 60.0f) * 0.017f;

    float3 direction = RandomDirection(seed);

    // 上向きを強めて、噴水のようにする。
    direction.y = abs(direction.y) * 1.7f + 0.35f;

    float lifetime = 1.6f + Hash11(seed + 3.1f) * 1.8f;

    GpuParticle particle;

    particle.positionSize = float4(g_emitter.xyz + direction * 0.04f,
                                   0.013f + Hash11(seed + 5.5f) * 0.020f);

    particle.velocityLife = float4(direction * g_emitter.w
                                       * (0.65f + Hash11(seed + 9.2f) * 0.7f),
                                   lifetime);

    // 熱いほど白く、冷めるほど橙。生まれた瞬間の色をここで決める。
    float heat = 0.55f + Hash11(seed + 11.3f) * 0.45f;

    particle.colorLifetime = float4(1.45f * heat,
                                    0.52f * heat * heat,
                                    0.19f * heat * heat * heat,
                                    lifetime);

    return particle;
}


[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;

    // ★ 総数がスレッド数の倍数とは限らない。はみ出したスレッドは帰す。
    //   これを忘れると、確保していない領域へ書き込む。
    if (index >= (uint)g_timing.z)
    {
        return;
    }

    float deltaTime = g_timing.x;
    float time      = g_timing.y;

    GpuParticle particle = g_particles[index];

    particle.velocityLife.w -= deltaTime;

    if (particle.velocityLife.w <= 0.0f)
    {
        g_particles[index] = Respawn(index, time);
        return;
    }

    // 速度を重力で曲げ、位置を進める（オイラー法）。
    particle.velocityLife.xyz += g_gravity.xyz * deltaTime;
    particle.positionSize.xyz += particle.velocityLife.xyz * deltaTime;

    // 床で跳ね返る。跳ね返るたびに勢いを失う。
    if (particle.positionSize.y < g_gravity.w)
    {
        particle.positionSize.y  = g_gravity.w;
        particle.velocityLife.y  = abs(particle.velocityLife.y) * 0.42f;
        particle.velocityLife.xz *= 0.72f;
    }

    g_particles[index] = particle;
}
