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

    // x = 金属らしさ、y = 粗さ、z = 法線マップの効き具合。w は未使用。
    float4 g_materialParams;
};

// t = テクスチャ (SRV)、s = サンプラー。
// Texture2D が「画像データ」、SamplerState が「その読み方」。
Texture2D    g_texture : register(t0);
SamplerState g_sampler : register(s0);

// 光源から見た深度を書き込んだテクスチャ。
Texture2D g_shadowMap : register(t1);

// 金属らしさと粗さのテクスチャ。緑が粗さ、青が金属らしさ（glTF の決まり）。
// 色ではなく数値なので、C++ 側で sRGB ではない形式として作っている。
Texture2D g_metallicRoughness : register(t2);

// 周囲の景色。正距円筒図法で、段が進むほどぼけている。
Texture2D g_environment : register(t3);

// 拡散反射用に、あらゆる方向から届く光を積分したもの。
Texture2D g_irradiance : register(t4);

// 法線マップ。接線空間の法線を RGB に詰めたもの。
// 色ではなくベクトルなので、C++ 側で sRGB ではない形式として作っている。
// 持たない材質には (128, 128, 255) の 1 ピクセルが割り当てられる。
Texture2D g_normalMap : register(t5);

// 環境マップの段数。C++ 側の kEnvironmentMipCount と合わせること。
static const float kEnvironmentMipCount = 6.0f;

// 比較サンプラー。読んだ値をそのまま返すのではなく、渡した値と比較して
// 「合格した割合」を返す。周囲 4 テクセルぶんを比較して補間するので、
// 1 回の読み取りだけで影の境目がなめらかになる。
SamplerComparisonState g_shadowSampler : register(s1);

// シャドウマップの一辺のテクセル数。PCF のずらし幅を求めるのに使う。
static const float kShadowMapSize = 2048.0f;

// 円周率。拡散反射を割るのに使う。
static const float kPi = 3.14159265f;

// 非金属の垂直入射での反射率。
// ほとんどの誘電体（プラスチック・木・肌など）が 0.04 前後に収まるため、
// 定数で済ませるのが一般的。金属では基本色そのものが反射率になる。
static const float3 kDielectricF0 = float3(0.04f, 0.04f, 0.04f);

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
    float4 tangent  : TANGENT;    // xyz = U が増える向き、w = 従接線の符号
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
    float4 worldTangent  : TANGENT;    // xyz は法線と同じくワールド空間へ移す
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

    // 接線も向きなので w = 0。w 成分（従接線の符号）はそのまま持ち回る。
    output.worldTangent = float4(mul(float4(input.tangent.xyz, 0.0f), g_world).xyz,
                                 input.tangent.w);

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

//-----------------------------------------------------------------------------
// マイクロファセット BRDF（クック・トランス）
//   表面を「向きの揃っていない無数の微小な鏡」の集まりとみなすモデル。
//   3 つの項の積で表される:
//     D … 微小な鏡のうち、光を目に返す向きのものがどれだけあるか（分布）
//     G … 微小な鏡どうしが影を作り合って減る割合（幾何減衰）
//     F … 見る角度による反射率の変化（フレネル）
//-----------------------------------------------------------------------------

// D: GGX（トロウブリッジ・ライツ）の法線分布関数。
//   ハイライトの中心が鋭く、裾が長く伸びる。実測に近いとされ、現在の標準。
float DistributionGGX(float normalDotHalf, float roughness)
{
    // 粗さは 2 乗して使うのが慣例。見た目の変化が直感に合いやすい。
    float alpha  = roughness * roughness;
    float alpha2 = alpha * alpha;

    float denominator = normalDotHalf * normalDotHalf * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / max(kPi * denominator * denominator, 1e-7f);
}

// G の片側。視線側と光源側で 1 回ずつ使う。
float GeometrySchlickGGX(float normalDotVector, float roughness)
{
    // 直接光の場合の k。環境光（IBL）では別の式になる。
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;

    return normalDotVector / (normalDotVector * (1.0f - k) + k);
}

// G: スミスの幾何減衰。粗い面ほど、微小な鏡が互いを隠して暗くなる。
float GeometrySmith(float normalDotView, float normalDotLight, float roughness)
{
    return GeometrySchlickGGX(normalDotView,  roughness)
         * GeometrySchlickGGX(normalDotLight, roughness);
}

// F: シュリックの近似によるフレネル項。
//   浅い角度から見るほど、どんな材質でもよく反射する（水面を思い出すとよい）。
float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}


// 方向ベクトルを、正距円筒図法のテクスチャ座標へ変換する。
//   横は方位角、縦は天頂からの角度。v = 0 が真上（+Y）。
float2 DirectionToEquirectUv(float3 direction)
{
    float u = atan2(direction.z, direction.x) / (2.0f * kPi) + 0.5f;
    float v = acos(clamp(direction.y, -1.0f, 1.0f)) / kPi;
    return float2(u, v);
}

// 環境光の鏡面反射で使う積分の近似（分割和近似の第 2 項）。
//   本来は事前計算した表を引くところを、多項式で近似したもの。
//   Karis, "Real Shading in Unreal Engine 4" (2013) による。
float2 EnvironmentBrdfApprox(float roughness, float normalDotView)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f,  0.022f);
    const float4 c1 = float4( 1.0f,  0.0425f,  1.040f, -0.040f);

    float4 r = roughness * c0 + c1;
    float  a = min(r.x * r.x, exp2(-9.28f * normalDotView)) * r.x + r.y;

    return float2(-1.04f, 1.04f) * a + r.zw;
}


// リニアの明るさを、画面に出せる 0〜1 の範囲へ圧縮する。
//   拡張版ラインハルト。白点より暗い部分はほとんど変えず、
//   明るい部分だけをなだらかに押し込むので、白飛びが目立たなくなる。
float3 ToneMap(float3 color)
{
    const float whiteSquared = kWhitePoint * kWhitePoint;
    return color * (1.0f + color / whiteSquared) / (1.0f + color);
}

// 法線マップを読み、面の向きを傾けた法線をワールド空間で返す。
//   uv          … テクスチャ座標
//   worldNormal … 頂点から補間した法線（正規化済み）
//   worldTangent… xyz = 接線、w = 従接線の符号
float3 ApplyNormalMap(float2 uv, float3 worldNormal, float4 worldTangent)
{
    // 0〜1 で記録されている値を -1〜1 へ戻す。
    //   ★ この画像を sRGB として読んではいけない。色ではなく座標だから。
    float3 tangentNormal = g_normalMap.Sample(g_sampler, uv).rgb * 2.0f - 1.0f;

    // 効き具合。XY だけを弱めると、0 で「傾き無し」になる。
    tangentNormal.xy *= g_materialParams.z;

    // 補間で接線が法線から傾くので、法線に垂直な成分だけを残す（グラム・シュミット）。
    float3 tangent = normalize(worldTangent.xyz
                             - worldNormal * dot(worldNormal, worldTangent.xyz));

    // 従接線は外積で復元できる。w は UV が鏡像になっている面で -1 になる。
    float3 bitangent = cross(worldNormal, tangent) * worldTangent.w;

    // 接線空間 → ワールド空間。3 本の軸を並べた行列を掛けるのと同じこと。
    float3 result = tangentNormal.x * tangent
                  + tangentNormal.y * bitangent
                  + tangentNormal.z * worldNormal;

    return normalize(result);
}

// 塗られるピクセル 1 個につき 1 回呼ばれ、そのピクセルの色を決める。
float4 PSMain(VSOutput input) : SV_TARGET
{
    // 補間で長さが 1 から崩れるため、使う直前に必ず正規化し直す。
    float3 geometryNormal = normalize(input.worldNormal);

    // 法線マップで面の向きを傾ける。ここから先はこの法線だけを使う。
    //   ★ 影の判定には元の位置を使うので、影の形は変わらない。
    float3 normal = ApplyNormalMap(input.uv, geometryNormal, input.worldTangent);

    // g_lightDirection は「光が進む向き」なので、面から光源へ向かうベクトルは逆向き。
    float3 toLight    = -g_lightDirection.xyz;
    float3 toEye      = normalize(g_cameraPosition.xyz - input.worldPosition);
    float3 halfVector = normalize(toLight + toEye);

    float normalDotLight = saturate(dot(normal, toLight));
    float normalDotView  = saturate(dot(normal, toEye));
    float normalDotHalf  = saturate(dot(normal, halfVector));
    float halfDotView    = saturate(dot(halfVector, toEye));

    // 物体そのものの色 ＝ 頂点カラー × 材質の基本色 × テクスチャ。
    float4 baseColor = input.color
                     * g_baseColorFactor
                     * g_texture.Sample(g_sampler, input.uv);

    // 金属らしさと粗さ。テクスチャの緑が粗さ、青が金属らしさ。
    //   テクスチャを持たない材質には白が割り当てられるので、掛けても値は変わらない。
    float4 materialSample = g_metallicRoughness.Sample(g_sampler, input.uv);

    float metallic  = saturate(g_materialParams.x * materialSample.b);
    float roughness = clamp(g_materialParams.y * materialSample.g, 0.03f, 1.0f);

    // ★ 金属と非金属で、色の意味が入れ替わる。
    //   非金属 : 基本色は拡散反射の色。反射は色を持たず 0.04 程度
    //   金属   : 拡散反射が無く、基本色がそのまま反射の色になる
    float3 f0     = lerp(kDielectricF0, baseColor.rgb, metallic);
    float3 albedo = baseColor.rgb * (1.0f - metallic);

    // マイクロファセット BRDF の 3 項
    float  distribution = DistributionGGX(normalDotHalf, roughness);
    float  geometry     = GeometrySmith(normalDotView, normalDotLight, roughness);
    float3 fresnel      = FresnelSchlick(halfDotView, f0);

    // 分母の 4 は、立体角の変換から出てくる定数。
    float3 specular = (distribution * geometry * fresnel)
                    / max(4.0f * normalDotView * normalDotLight, 1e-4f);

    // エネルギー保存: 反射した割合 (F) の残りだけが内部へ入り、拡散反射になる。
    //   これがあるおかげで「光っている所ほど下地の色が薄い」という自然な見え方になる。
    float3 diffuseWeight = (1.0f - fresnel);

    // 遮る物があれば、光が「届かなかった」ことにする。
    //   ★ 減らすのは直接光だけ。環境光は照り返しなので影の中でも残る。
    float shadow = SampleShadow(input.worldPosition);

    // 拡散反射を π で割るのが物理的に正しい形。
    //   そのぶん C++ 側で光の強さに π を掛けて明るさを揃えている。
    float3 direct = (diffuseWeight * albedo / kPi + specular)
                  * g_lightColor.rgb * normalDotLight * shadow;

    // ここからが環境光 (IBL)。定数ではなく、周囲の景色から求める。

    // 拡散ぶん : 面の向きで、積分済みの画像を引くだけ。
    float3 irradiance = g_irradiance.Sample(
        g_sampler, DirectionToEquirectUv(normal)).rgb;

    float3 diffuseAmbient = irradiance * albedo;

    // 鏡面ぶん : 視線を面で反射させた方向の景色を読む。
    //   粗い材質ほど後ろの段（ぼけた段）を読むことで、映り込みがぼやける。
    float3 reflectDirection = reflect(-toEye, normal);

    float3 prefiltered = g_environment.SampleLevel(
        g_sampler,
        DirectionToEquirectUv(reflectDirection),
        roughness * (kEnvironmentMipCount - 1.0f)).rgb;

    // 反射率は視線の角度と粗さで変わる。その積分を近似で求める。
    float2 environmentBrdf = EnvironmentBrdfApprox(roughness, normalDotView);
    float3 specularAmbient = prefiltered * (f0 * environmentBrdf.x + environmentBrdf.y);

    // ★ 金属はここでようやく見えるようになる。
    //   拡散反射を持たないので、映り込みが無ければ真っ黒のままだった。
    float3 ambient = (diffuseAmbient + specularAmbient) * g_lightColor.a;

    float3 lit = direct + ambient;

    // ★ ここまでの計算はすべてリニア空間で行っている。
    //   まず明るさを 0〜1 へ押し込み、sRGB への変換はレンダーターゲットに任せる
    //   （RTV が _SRGB 形式なので、GPU が書き込み時に変換してくれる）。
    return float4(ToneMap(lit), baseColor.a);
}
