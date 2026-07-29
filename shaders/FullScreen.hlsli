//=============================================================================
// FullScreen.hlsli
//   画面いっぱいの三角形を、頂点バッファ無しで作るための共通部分。
//
//   背景（Skybox.hlsl）と後処理（PostProcess.hlsl）の両方が使う。
//   詳しい解説は docs/tutorial/23_スカイボックス.md と 25_ポストプロセス.md を参照。
//=============================================================================
#ifndef FULLSCREEN_HLSLI
#define FULLSCREEN_HLSLI

struct FullScreenVSOutput
{
    float4 position : SV_POSITION;

    // 画面上の位置を -1〜1 で表したもの。左下が (-1, -1)、右上が (1, 1)。
    float2 screenPosition : TEXCOORD0;

    // テクスチャ座標。左上が (0, 0)、右下が (1, 1)。
    float2 uv : TEXCOORD1;
};

// 頂点 ID だけで画面を覆う三角形を作る。
//   3 頂点で画面全体を覆えるので、四角形（2 三角形）より無駄が少ない。
//
//     id = 0 → (-1, -1)   id = 1 → (-1,  3)   id = 2 → ( 3, -1)
//
//   はみ出した部分はラスタライザが切り落とす。
FullScreenVSOutput FullScreenVS(uint vertexId)
{
    FullScreenVSOutput output;

    float2 corner = float2((vertexId == 2) ? 3.0f : -1.0f,
                           (vertexId == 1) ? 3.0f : -1.0f);

    // z = w = 1 にすると、透視除算のあと深度が 1.0（一番奥）になる。
    output.position       = float4(corner, 1.0f, 1.0f);
    output.screenPosition = corner;

    // 画面の -1〜1 を、テクスチャの 0〜1 へ。
    //   ★ V だけ向きが逆。画面は上が +1、テクスチャは上が 0。
    output.uv = float2(corner.x * 0.5f + 0.5f, 0.5f - corner.y * 0.5f);

    return output;
}

#endif // FULLSCREEN_HLSLI
