//=============================================================================
// Vfx.hlsl
//   半透明のビルボード（常にカメラを向く板）を描くシェーダー。
//
//   頂点バッファを持たず、頂点 ID とインスタンス ID から板を組み立てる。
//   詳しい解説は docs/tutorial/27_半透明とブレンディング.md を参照。
//=============================================================================

// 板 1 枚ぶんの情報。
struct VfxParticle
{
    // xyz = ワールド座標、w = 大きさ（半径）。
    float4 positionSize;

    // rgb = 色（リニア）、a = 不透明度。
    float4 color;

    // x = やわらかさ（0 で輪郭がはっきり、1 でぼんやり）、
    // y = 回転（ラジアン）、z, w = 未使用。
    float4 params;
};

cbuffer VfxConstants : register(b0)
{
    // ビュー射影行列。
    float4x4 g_viewProjection;

    // カメラの右方向と上方向（ワールド空間）。板をカメラへ向けるのに使う。
    float4 g_cameraRight;
    float4 g_cameraUp;

    // 板の一覧。
    VfxParticle g_particles[64];
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 local    : TEXCOORD0;   // 板の中での位置（-1〜1）
    float4 color    : COLOR;
    float  softness : TEXCOORD1;
};

// 板 1 枚は三角形 2 枚 ＝ 頂点 6 個。
//   頂点バッファを持たず、ID から四隅を作る。
//     0,1,2 と 2,1,3 の順で並べると、2 枚の三角形で四角形になる。
static const float2 kCorners[6] =
{
    float2(-1.0f, -1.0f), float2(-1.0f,  1.0f), float2( 1.0f, -1.0f),
    float2( 1.0f, -1.0f), float2(-1.0f,  1.0f), float2( 1.0f,  1.0f),
};

VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VSOutput output;

    VfxParticle particle = g_particles[instanceId];

    float2 corner = kCorners[vertexId];

    // 板の中で回す。ビルボードは向きが揃いやすいので、
    // 少し回しておくと同じ絵の繰り返しに見えにくい。
    float s = sin(particle.params.y);
    float c = cos(particle.params.y);
    float2 rotated = float2(corner.x * c - corner.y * s,
                            corner.x * s + corner.y * c);

    // ★ カメラの右と上で板を張る。これで常にカメラを向く（ビルボード）。
    //   モデル行列で回すのではなく、頂点の位置をその場で作るのが要点。
    float3 worldPosition = particle.positionSize.xyz
                         + g_cameraRight.xyz * rotated.x * particle.positionSize.w
                         + g_cameraUp.xyz    * rotated.y * particle.positionSize.w;

    output.position = mul(float4(worldPosition, 1.0f), g_viewProjection);
    output.local    = corner;
    output.color    = particle.color;
    output.softness = particle.params.x;

    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    // 中心からの距離で丸く抜く。テクスチャを使わずに済む。
    float distance = length(input.local);

    // やわらかさ。0 で輪郭がはっきり、1 で中心だけぼんやり光る。
    float inner = lerp(0.92f, 0.0f, input.softness);
    float alpha = smoothstep(1.0f, inner, distance);

    // 縁を鋭くする。そのままだと、どれも同じぼんやりした丸になる。
    alpha = pow(alpha, lerp(1.0f, 2.4f, input.softness));

    // ★ 色にアルファを掛けてから出す（乗算済みアルファ）。
    //   こうしておくと、アルファ合成と加算合成を
    //   「同じシェーダー・同じ出力」のままブレンド設定だけで切り替えられる。
    float3 color = input.color.rgb * input.color.a * alpha;

    return float4(color, input.color.a * alpha);
}
