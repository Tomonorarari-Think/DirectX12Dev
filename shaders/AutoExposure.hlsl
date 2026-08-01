//=============================================================================
// AutoExposure.hlsl
//   画面の明るさを測り、露出を自動で合わせるコンピュートシェーダー。
//
//   何万ピクセルの平均を 1 つの値にまとめる「並列リダクション」。
//   シェーダーモデル 6 の Wave 命令を使う版と、使わない版を両方持つ。
//   詳しい解説は docs/tutorial/32_自動露出とWave命令.md を参照。
//=============================================================================

// 1 グループのスレッド数（8 x 8）。
#define THREADS_X 8
#define THREADS_Y 8
#define THREADS   (THREADS_X * THREADS_Y)

// 1 グループぶんの部分和を置く数。
//   ★ 波の幅は GPU により 4〜128 と幅がある。いちばん狭い 4 でも足りる数を取る。
#define MAX_WAVES_PER_GROUP (THREADS / 4)

cbuffer AutoExposureConstants : register(b0)
{
    // xy = 測る画像の大きさ（ピクセル）、z = 測る点の総数、w = 前フレームからの秒。
    float4 g_sourceSize;

    // x = 目標の明るさ、y = 追従の速さ、z = 露出の下限、w = 露出の上限。
    float4 g_tuning;

    // x = 対数輝度の下限、y = 上限、z = 固定小数の倍率、w = 未使用。
    float4 g_range;
};

// 測る対象（シーンを描き終えた HDR の絵）。
Texture2D<float4> g_source : register(t0);
SamplerState      g_linearSampler : register(s0);

// 集計結果。
//   [0] = 対数輝度の合計（固定小数）、[1] = 露出（float のビット列）。
RWByteAddressBuffer g_state : register(u0);


/// 明るさ（輝度）。人の目の感度に合わせた重み。
float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}


/// 輝度を対数へ。明るさは桁で効くので、平均は対数で採る。
///   ★ 線形のまま平均すると、画面のごく一部の強い光に全体が引きずられる。
float ToLogLuminance(float3 color)
{
    const float luminance = max(Luminance(color), 1e-5f);

    return clamp(log2(luminance), g_range.x, g_range.y);
}


/// 対数輝度を、足し合わせられる整数へ。
///   InterlockedAdd は整数にしか使えないため、固定小数へ直す。
uint ToFixedPoint(float logLuminance)
{
    return (uint)((logLuminance - g_range.x) * g_range.z);
}


groupshared float g_partialSums[MAX_WAVES_PER_GROUP];
groupshared float g_treeSums[THREADS];


//-----------------------------------------------------------------------------
// 第 1 段 : 画面を分担して測り、1 グループにつき 1 回だけ足し込む
//-----------------------------------------------------------------------------
[numthreads(THREADS_X, THREADS_Y, 1)]
void CSAccumulate(uint3 dispatchThreadId : SV_DispatchThreadID,
                  uint  groupIndex       : SV_GroupIndex)
{
    // 担当するピクセルの中心を UV で読む。
    const float2 uv = (dispatchThreadId.xy + 0.5f) / g_sourceSize.xy;

    float logLuminance = 0.0f;

    // 範囲外のスレッドは 0 を足す（分岐で抜けると、下の集計に参加できない）。
    //   ★ Wave 命令も GroupMemoryBarrier も「全員が到達する」ことが前提。
    //     早期 return を挟むと、GPU により結果が変わる。
    if (dispatchThreadId.x < (uint)g_sourceSize.x &&
        dispatchThreadId.y < (uint)g_sourceSize.y)
    {
        const float3 color = g_source.SampleLevel(g_linearSampler, uv, 0).rgb;

        logLuminance = ToLogLuminance(color);
    }

    float groupSum = 0.0f;

#if defined(USE_WAVE_INTRINSICS)

    //-------------------------------------------------------------------------
    // Wave 版 : 同じ波の中は命令 1 つで合計できる
    //-------------------------------------------------------------------------
    //   ★ 共有メモリも同期も要らない。波の中は元々そろって動いているため。
    const float waveSum = WaveActiveSum(logLuminance);

    const uint waveIndex = groupIndex / WaveGetLaneCount();
    const uint waveCount = (THREADS + WaveGetLaneCount() - 1) / WaveGetLaneCount();

    // 波ごとの合計を 1 つずつ共有メモリへ。書くのは各波の先頭レーンだけ。
    if (WaveIsFirstLane())
    {
        g_partialSums[waveIndex] = waveSum;
    }

    GroupMemoryBarrierWithGroupSync();

    // 残った数個（多くて 16 個）を 1 スレッドで足す。
    if (groupIndex == 0)
    {
        for (uint i = 0; i < waveCount; ++i)
        {
            groupSum += g_partialSums[i];
        }
    }

#else

    //-------------------------------------------------------------------------
    // 共有メモリ版 : 半分ずつ折りたたむ（Wave 命令を使わない従来のやり方）
    //-------------------------------------------------------------------------
    g_treeSums[groupIndex] = logLuminance;

    GroupMemoryBarrierWithGroupSync();

    // 64 → 32 → 16 → … → 1。毎段で全員の足並みをそろえる必要がある。
    for (uint stride = THREADS / 2; stride > 0; stride >>= 1)
    {
        if (groupIndex < stride)
        {
            g_treeSums[groupIndex] += g_treeSums[groupIndex + stride];
        }

        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0)
    {
        groupSum = g_treeSums[0];
    }

#endif

    // グループの代表 1 スレッドだけが、全体の合計へ足し込む。
    //   ★ ここを全スレッドでやると、衝突が 64 倍になって一気に遅くなる。
    if (groupIndex == 0)
    {
        uint ignored;
        g_state.InterlockedAdd(0, ToFixedPoint(groupSum / (float)THREADS) * THREADS,
                               ignored);
    }
}


//-----------------------------------------------------------------------------
// 第 2 段 : 合計を露出へ直し、前フレームからなめらかに近づける
//-----------------------------------------------------------------------------
[numthreads(1, 1, 1)]
void CSResolve(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint total = g_state.Load(0);

    // 固定小数から戻す。
    const float averageLog =
        (float)total / (g_range.z * g_sourceSize.z) + g_range.x;

    const float averageLuminance = max(exp2(averageLog), 1e-4f);

    // 目標の明るさになるような露出。
    float target = g_tuning.x / averageLuminance;
    target = clamp(target, g_tuning.z, g_tuning.w);

    const float previous = asfloat(g_state.Load(4));

    // ★ いきなり切り替えず、時間をかけて近づける。
    //   そうしないと、明るい物が横切るたびに画面全体がちらつく。
    //   1 - exp(-dt * speed) は「フレーム時間に依らない補間の割合」。
    const float blend = 1.0f - exp(-g_sourceSize.w * g_tuning.y);

    const float exposure = (previous > 0.0f) ? lerp(previous, target, blend)
                                             : target;

    g_state.Store(4, asuint(exposure));

    // 次のフレームのために合計を 0 へ戻す。
    g_state.Store(0, 0);
}
