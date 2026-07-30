//=============================================================================
// 29_shockwave.hlsl
//   衝撃波 : 座標をずらして「空間が歪んだ」ように見せる。
//
//   物を描くのではなく、背景を読む位置をずらすだけ。
//   歪みは VFX でいちばん費用対効果が高い手のひとつ。
//   解説 : docs/shader-lab/29_衝撃波.md
//=============================================================================
#include "LabCommon.hlsli"

/// 歪ませる対象の背景。
float3 Background(float2 uv)
{
    float2 p = ToCenteredUv(uv);

    // 市松と斜めのストライプ。歪みが目で追えるよう、線を多めにする。
    float2 grid = floor(uv * 14.0f);
    float checker = fmod(grid.x + grid.y, 2.0f);

    float3 color = lerp(float3(0.16f, 0.20f, 0.30f),
                        float3(0.48f, 0.58f, 0.75f), checker);

    float stripes = 0.5f + 0.5f * sin((p.x + p.y) * 26.0f);
    color = lerp(color, PaletteWarm(0.55f) * 0.5f, stripes * 0.35f);

    return color;
}

/// 1 発ぶんの衝撃波。戻り値 : x = ずらす量、y = リングの明るさ。
float2 Shockwave(float2 p, float2 center, float time, float speed, float width)
{
    float distance = length(p - center);

    // 半径は時間に比例して広がる
    float radius = time * speed;

    // ★ リングからの「ずれ」。リングの上で 1、離れると 0。
    float ring = 1.0f - saturate(abs(distance - radius) / width);

    // 縁を鋭くする
    ring = pow(ring, 2.0f);

    // 遠くへ行くほど弱くなる（エネルギーが薄まる）
    float decay = 1.0f / (1.0f + radius * radius * 1.2f);

    return float2(ring * decay, ring * decay);
}

float4 PSMain(FullScreenVSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float  t  = g_time.x;

    float2 p = ToCenteredUv(uv);

    // 1.8 秒ごとに 1 発。位置は毎回変える。
    const float period = 1.1f;
    float shot  = floor(t / period);
    float local = frac(t / period) * period;

    float2 center = (Hash22(float2(shot, 3.3f)) - 0.5f) * float2(1.6f, 1.0f);

    // 少しずらして 2 発目を重ねると、余韻が出る
    float2 wave1 = Shockwave(p, center, local, 1.35f, 0.16f);
    float2 wave2 = Shockwave(p, center, max(local - 0.12f, 0.0f), 1.10f, 0.10f);

    float displace = wave1.x * 0.115f + wave2.x * 0.060f;
    float glow     = wave1.y * 1.35f + wave2.y * 0.85f;

    // --- (1) 歪み : 読む位置をリングの外向きへずらす -----------------------
    float2 direction = normalize(p - center + 1e-5f);

    // ★ ずらすのは「見る位置」であって、背景そのものは動かない。
    //   これがレンズ歪みや熱揺らぎと同じ仕組み。
    float2 warpedUv = uv + direction * displace * float2(g_resolution.y * g_resolution.z, 1.0f);

    float3 color = Background(saturate(warpedUv));

    // --- (2) 色収差 : 波長ごとにずらす量を変える ---------------------------
    //   歪みが強いところほど、縁が虹色に割れる。
    if (displace > 0.001f)
    {
        float2 shift = direction * displace * 0.22f;
        color.r = Background(saturate(warpedUv + shift)).r;
        color.b = Background(saturate(warpedUv - shift)).b;
    }

    // --- (3) リングそのものを光らせる --------------------------------------
    color += float3(0.85f, 0.92f, 1.00f) * glow * 0.55f;

    // --- (4) 中心の閃光 -----------------------------------------------------
    float flash = exp(-local * 9.0f) * 0.030f / max(length(p - center), 0.02f);
    color += float3(1.00f, 0.92f, 0.78f) * flash;

    color = color / (1.0f + color);

    return LabOutput(color);
}
