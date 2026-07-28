//=============================================================================
// Skybox.hlsl
//   背景に環境マップを描くシェーダー。
//
//   頂点バッファを使わず、頂点 ID から画面いっぱいの三角形を作る。
//   詳しい解説は docs/tutorial/23_スカイボックス.md を参照。
//=============================================================================

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

// トーンマッピングで「白」とみなす明るさ。Mesh.hlsl と同じ値にすること。
static const float kWhitePoint = 2.2f;

struct VSOutput
{
    float4 position : SV_POSITION;

    // 画面上の位置を -1〜1 で表したもの。左下が (-1, -1)、右上が (1, 1)。
    // ピクセルシェーダーで視線の向きを組み立てるのに使う。
    float2 screenPosition : TEXCOORD;
};

// 頂点バッファを使わず、頂点 ID だけで画面を覆う三角形を作る。
//   3 頂点で画面全体を覆えるので、四角形（2 三角形）より無駄が少ない。
//
//     id = 0 → (-1, -1)   id = 1 → (-1,  3)   id = 2 → ( 3, -1)
//
//   はみ出した部分はラスタライザが切り落とす。
VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;

    float2 corner = float2((vertexId == 2) ? 3.0f : -1.0f,
                           (vertexId == 1) ? 3.0f : -1.0f);

    // ★ z = w = 1 にすると、透視除算のあと深度が 1.0（一番奥）になる。
    //   深度テストを LESS_EQUAL にしておけば、何も描かれていない所だけが通る。
    output.position       = float4(corner, 1.0f, 1.0f);
    output.screenPosition = corner;

    return output;
}

// 方向ベクトルを、正距円筒図法のテクスチャ座標へ変換する。
//   Mesh.hlsl の DirectionToEquirectUv と同じ式。
float2 DirectionToEquirectUv(float3 direction)
{
    float u = atan2(direction.z, direction.x) / (2.0f * kPi) + 0.5f;
    float v = acos(clamp(direction.y, -1.0f, 1.0f)) / kPi;
    return float2(u, v);
}

// リニアの明るさを、画面に出せる 0〜1 の範囲へ圧縮する。
//   Mesh.hlsl と同じ式を使わないと、物体と背景で明るさの扱いが食い違う。
float3 ToneMap(float3 color)
{
    float3 numerator = color * (1.0f + color / (kWhitePoint * kWhitePoint));
    return numerator / (1.0f + color);
}

float4 PSMain(VSOutput input) : SV_TARGET
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

    // 物体の環境光と同じ倍率を掛けてから、同じ式で圧縮する。
    return float4(ToneMap(color * g_skyboxParams.z), 1.0f);
}
