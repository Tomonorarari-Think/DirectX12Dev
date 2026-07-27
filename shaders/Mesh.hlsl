//=============================================================================
// Mesh.hlsl
//   メッシュ（現在は立方体）を描画する頂点シェーダーとピクセルシェーダー。
//
//   詳しい解説は docs/tutorial/ を参照。
//=============================================================================

// 描画全体で共通の値。C++ 側のルートシグネチャの ShaderRegister と番号を合わせる。
// 定数バッファは 16 バイト単位で区切られる。float3 の後ろに float を書くと
// 同じ 16 バイトに詰め込まれ、C++ 側とずれるため、全て float4 / float4x4 で揃えている。
cbuffer SceneConstants : register(b0)
{
    // ワールド × ビュー × プロジェクションをまとめた変換行列。
    // HLSL は列優先で読むため、C++ 側で転置してから書き込んでいる。
    float4x4 g_worldViewProjection;

    // ワールド行列。法線をワールド空間へ移すために単体でも必要。
    float4x4 g_world;

    // xyz = 光が進む向き（正規化済み）。w は未使用。
    float4 g_lightDirection;

    // rgb = 光の色と強さ、a = 環境光の強さ。
    float4 g_lightColor;

    // xyz = 視点のワールド座標。鏡面反射に使う。w は未使用。
    float4 g_cameraPosition;
};

// t = テクスチャ (SRV)、s = サンプラー。
// Texture2D が「画像データ」、SamplerState が「その読み方」。
Texture2D    g_texture : register(t0);
SamplerState g_sampler : register(s0);

// 鏡面反射の鋭さ。大きいほどハイライトが小さく硬くなる。
// 立方体は面が平らなので、ハイライトは点ではなく面全体の明滅として現れる。
static const float kSpecularPower = 24.0f;

// 鏡面反射の強さ。
static const float kSpecularIntensity = 0.35f;

// 頂点シェーダーへの入力。
// C++ 側の Vertex 構造体と入力レイアウトに、順序・型・セマンティクス名を合わせる。
struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;     // 面の向き。長さ 1
    float4 color    : COLOR;
    float2 uv       : TEXCOORD;   // UV は V が下向きが正
};

// 頂点シェーダーの出力 ＝ ピクセルシェーダーの入力。
// SV_POSITION はラスタライズに使われる特別な値なので、float4 で必須。
struct VSOutput
{
    float4 position      : SV_POSITION;
    float3 worldPosition : POSITION;    // ライティングはワールド空間で計算する
    float3 worldNormal   : NORMAL;
    float4 color         : COLOR;
    float2 uv            : TEXCOORD;
};

// 頂点 1 個につき 1 回呼ばれ、頂点の最終的な位置を決める。
VSOutput VSMain(VSInput input)
{
    VSOutput output;

    // w = 1.0 を足して同次座標にしてから変換行列を掛ける。
    // DirectXMath は行ベクトル規約なので mul(位置, 行列) の順。
    output.position = mul(float4(input.position, 1.0f), g_worldViewProjection);

    // ライトの向きはワールド空間で決めているので、頂点と法線もワールド空間へ揃える。
    output.worldPosition = mul(float4(input.position, 1.0f), g_world).xyz;

    // 法線は「向き」なので平行移動を受けてはいけない。w = 0 にすると
    // 行列の 4 行目（平行移動成分）が掛からず、回転だけが適用される。
    output.worldNormal = mul(float4(input.normal, 0.0f), g_world).xyz;

    // 色と UV はラスタライザが頂点間で自動補間してくれる。
    output.color = input.color;
    output.uv    = input.uv;

    return output;
}

// 塗られるピクセル 1 個につき 1 回呼ばれ、そのピクセルの色を決める。
float4 PSMain(VSOutput input) : SV_TARGET
{
    // 補間で長さが 1 から崩れるため、使う直前に必ず正規化し直す。
    float3 normal = normalize(input.worldNormal);

    // g_lightDirection は「光が進む向き」なので、面から光源へ向かうベクトルは逆向き。
    float3 toLight = -g_lightDirection.xyz;
    float3 toEye   = normalize(g_cameraPosition.xyz - input.worldPosition);

    // 拡散反射（ランバート）: 面が光に正対するほど明るい。
    //   内積は cos なので、真正面で 1、真横で 0、裏側で負になる。
    //   saturate で 0〜1 に丸め、裏を向いた面が負で暗くなりすぎるのを防ぐ。
    float diffuse = saturate(dot(normal, toLight));

    // 鏡面反射（ブリン・フォン）: 光と視線のちょうど中間を向いた面が最も光る。
    //   反射ベクトルを求めるより計算が軽く、見た目もほとんど変わらない。
    float3 halfVector = normalize(toLight + toEye);
    float  specular   = pow(saturate(dot(normal, halfVector)), kSpecularPower);

    // 光が当たっていない面にハイライトが乗らないよう、拡散の強さで打ち消す。
    specular *= diffuse;

    // 物体そのものの色 ＝ 頂点カラー × テクスチャ。
    float4 baseColor = input.color * g_texture.Sample(g_sampler, input.uv);

    // 環境光 + 拡散反射 で物体の色を照らし、最後に鏡面反射を足す。
    float3 ambient = g_lightColor.aaa;
    float3 lit     = baseColor.rgb * (ambient + g_lightColor.rgb * diffuse)
                   + g_lightColor.rgb * specular * kSpecularIntensity;

    return float4(lit, baseColor.a);
}
