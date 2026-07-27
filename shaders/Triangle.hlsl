//=============================================================================
// Triangle.hlsl
//   三角形を描画する頂点シェーダーとピクセルシェーダー。
//
//   詳しい解説は docs/tutorial/ を参照。
//=============================================================================

// 描画全体で共通の値。C++ 側のルートシグネチャの ShaderRegister と番号を合わせる。
cbuffer SceneConstants : register(b0)
{
    // ワールド × ビュー × プロジェクションをまとめた変換行列。
    // HLSL は列優先で読むため、C++ 側で転置してから書き込んでいる。
    float4x4 g_worldViewProjection;
};

// t = テクスチャ (SRV)、s = サンプラー。
// Texture2D が「画像データ」、SamplerState が「その読み方」。
Texture2D    g_texture : register(t0);
SamplerState g_sampler : register(s0);

// 頂点シェーダーへの入力。
// C++ 側の Vertex 構造体と入力レイアウトに、順序・型・セマンティクス名を合わせる。
struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD;   // UV は V が下向きが正
};

// 頂点シェーダーの出力 ＝ ピクセルシェーダーの入力。
// SV_POSITION はラスタライズに使われる特別な値なので、float4 で必須。
struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD;
};

// 頂点 1 個につき 1 回呼ばれ、頂点の最終的な位置を決める。
VSOutput VSMain(VSInput input)
{
    VSOutput output;

    // w = 1.0 を足して同次座標にしてから変換行列を掛ける。
    // DirectXMath は行ベクトル規約なので mul(位置, 行列) の順。
    output.position = mul(float4(input.position, 1.0f), g_worldViewProjection);

    // 色と UV はラスタライザが頂点間で自動補間してくれる。
    output.color = input.color;
    output.uv    = input.uv;

    return output;
}

// 塗られるピクセル 1 個につき 1 回呼ばれ、そのピクセルの色を決める。
float4 PSMain(VSOutput input) : SV_TARGET
{
    // 補間された UV の位置の色をテクスチャから読み出す。
    float4 textureColor = g_texture.Sample(g_sampler, input.uv);

    // 頂点カラーと掛け合わせる（モジュレート）。
    return input.color * textureColor;
}
