//=============================================================================
// Mesh.hlsl
//   メッシュ（現在は立方体）を描画する頂点シェーダーとピクセルシェーダー。
//
//   詳しい解説は docs/tutorial/ を参照。
//=============================================================================

// 定数バッファは 16 バイト単位で区切られる。float3 の後ろに float を書くと
// 同じ 16 バイトに詰め込まれ、C++ 側とずれるため、全て float4 / float4x4 で揃えている。
//
// b0 と b1 で分けているのは、書き換わる頻度が違うため。
// カメラとライトは 1 フレームに 1 回、変換行列はオブジェクトごとに変わる。

// 1 フレームのあいだ、描くもの全てで共通の値。
cbuffer FrameConstants : register(b0)
{
    // ビュー行列 × 射影行列。HLSL は列優先で読むため C++ 側で転置してある。
    float4x4 g_viewProjection;

    // 光源から見たビュー行列 × 射影行列。
    // シャドウマップを描くときは変換行列として、画面を描くときは
    // 「この点が影の中か」を調べる座標変換として、同じ行列を 2 度使う。
    float4x4 g_lightViewProjection;

    // xyz = 光が進む向き（正規化済み）。w は未使用。
    float4 g_lightDirection;

    // rgb = 光の色と強さ、a = 環境光の強さ。
    float4 g_lightColor;

    // xyz = 視点のワールド座標。鏡面反射に使う。w は未使用。
    float4 g_cameraPosition;
};

// オブジェクト 1 個ごとに変わる値。描く直前に差し替える。
cbuffer ObjectConstants : register(b1)
{
    // ワールド × ビュー × 射影。CPU 側で合成済み。
    float4x4 g_worldViewProjection;

    // ワールド行列。法線をワールド空間へ移すために単体でも必要。
    float4x4 g_world;
};

// 材質 1 つごとに変わる値。サブメッシュを描く直前に差し替える。
cbuffer MaterialConstants : register(b2)
{
    // 基本色。テクスチャの色と掛け合わせる。
    // テクスチャを持たない材質には白 1 ピクセルが割り当てられるので、
    // シェーダー側に「テクスチャの有無」の分岐は要らない。
    float4 g_baseColorFactor;
};

// t = テクスチャ (SRV)、s = サンプラー。
// Texture2D が「画像データ」、SamplerState が「その読み方」。
Texture2D    g_texture : register(t0);
SamplerState g_sampler : register(s0);

// 光源から見た深度を書き込んだテクスチャ。
Texture2D g_shadowMap : register(t1);

// 比較サンプラー。読んだ値をそのまま返すのではなく、渡した値と比較して
// 「合格した割合」を返す。周囲 4 テクセルぶんを比較して補間するので、
// 1 回の読み取りだけで影の境目がなめらかになる。
SamplerComparisonState g_shadowSampler : register(s1);

// シャドウマップの一辺のテクセル数。PCF のずらし幅を求めるのに使う。
static const float kShadowMapSize = 2048.0f;

// 鏡面反射の鋭さ。大きいほどハイライトが小さく硬くなる。
// 立方体は面が平らなので、ハイライトは点ではなく面全体の明滅として現れる。
static const float kSpecularPower = 24.0f;

// 鏡面反射の強さ。
static const float kSpecularIntensity = 0.35f;

// トーンマッピングで「白」とみなす明るさ。
// これを超える部分だけが圧縮され、それ以下はほとんどそのまま残る。
static const float kWhitePoint = 2.2f;

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

//-----------------------------------------------------------------------------
// シャドウマップを描くパス
//   色は要らないので頂点シェーダーだけ。深度が書き込まれれば目的は達する。
//-----------------------------------------------------------------------------
float4 VSShadow(VSInput input) : SV_POSITION
{
    // カメラの代わりに光源から見た行列を掛ける。それ以外は通常の描画と同じ。
    return mul(mul(float4(input.position, 1.0f), g_world), g_lightViewProjection);
}

// この点が影の中にあるかを 0（完全な影）〜1（当たっている）で返す。
float SampleShadow(float3 worldPosition)
{
    // (1) ワールド座標を、光源から見たクリップ空間へ移す。
    float4 lightSpace = mul(float4(worldPosition, 1.0f), g_lightViewProjection);

    // (2) 透視除算。正射影なので w は 1 だが、一般形で書いておく。
    float3 projected = lightSpace.xyz / lightSpace.w;

    // (3) クリップ空間 (-1〜+1) をテクスチャ座標 (0〜1) へ。
    //   Y だけ符号が逆なのは、UV の V が下向きで、NDC の Y が上向きだから。
    float2 uv = float2(projected.x * 0.5f + 0.5f, -projected.y * 0.5f + 0.5f);

    // (4) 光源の視界の外は影にしない。
    //   サンプラーの BORDER が UV の外を白（＝当たっている）にしてくれるが、
    //   奥のクリップ面より遠い場合は自分で弾く必要がある。
    if (projected.z > 1.0f)
    {
        return 1.0f;
    }

    // (5) PCF (Percentage Closer Filtering)
    //   1 点だけ比べると影の輪郭がギザギザになる。周囲 3x3 を比べて平均すると、
    //   段階的な値になって境目がなめらかに見える。
    //   比較サンプラー自体も 2x2 を補間するので、実質 6x6 相当をならしている。
    float texelSize = 1.0f / kShadowMapSize;
    float lit = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * texelSize;

            // SampleCmpLevelZero : 「記録された深度 <= 渡した深度」の割合を返す。
            //   合格 = 遮る物が無い = 光が当たっている。
            lit += g_shadowMap.SampleCmpLevelZero(g_shadowSampler, uv + offset, projected.z);
        }
    }

    return lit / 9.0f;
}

// リニアの明るさを、画面に出せる 0〜1 の範囲へ圧縮する。
//   拡張版ラインハルト。白点より暗い部分はほとんど変えず、
//   明るい部分だけをなだらかに押し込むので、白飛びが目立たなくなる。
float3 ToneMap(float3 color)
{
    const float whiteSquared = kWhitePoint * kWhitePoint;
    return color * (1.0f + color / whiteSquared) / (1.0f + color);
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

    // 遮る物があれば、光が「届かなかった」ことにする。
    //   ★ 減らすのは直接光（拡散反射と鏡面反射）だけ。
    //     環境光は周囲からの照り返しなので、影の中でも残る。
    float shadow = SampleShadow(input.worldPosition);

    diffuse  *= shadow;
    specular *= shadow;

    // 物体そのものの色 ＝ 頂点カラー × 材質の基本色 × テクスチャ。
    float4 baseColor = input.color
                     * g_baseColorFactor
                     * g_texture.Sample(g_sampler, input.uv);

    // 環境光 + 拡散反射 で物体の色を照らし、最後に鏡面反射を足す。
    float3 ambient = g_lightColor.aaa;
    float3 lit     = baseColor.rgb * (ambient + g_lightColor.rgb * diffuse)
                   + g_lightColor.rgb * specular * kSpecularIntensity;

    // ★ ここまでの計算はすべてリニア空間で行っている。
    //   まず明るさを 0〜1 へ押し込み、sRGB への変換はレンダーターゲットに任せる
    //   （RTV が _SRGB 形式なので、GPU が書き込み時に変換してくれる）。
    return float4(ToneMap(lit), baseColor.a);
}
