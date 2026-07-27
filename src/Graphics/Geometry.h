//=============================================================================
// Geometry.h
//   頂点の形式と、基本的な形状（立方体・床）のデータ生成。
//
//   DirectX の API を呼ばず、頂点とインデックスの配列を作るだけの層。
//   GPU へ載せる処理は Mesh が受け持つ。
//=============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace dx12
{

/// <summary>
/// 頂点 1 個ぶんのデータ構造。
/// </summary>
/// <remarks>
/// この並び順は入力レイアウト（`MeshPipeline.cpp`）と HLSL の `VSInput` に
/// 一致していなければなりません。並べ替えたら 3 か所すべてを直してください。
/// </remarks>
struct Vertex
{
    /// <summary>
    /// 頂点の座標 (x, y, z)。モデルの原点を基準としたローカル座標。
    /// </summary>
    float position[3];

    /// <summary>
    /// 面の向きを表す法線ベクトル (x, y, z)。長さ 1 に正規化しておきます。
    /// </summary>
    /// <remarks>
    /// 光の当たり具合はこのベクトルだけで決まります。座標が同じ頂点でも、属する面が
    /// 違えば法線が違うため、立方体の頂点は 8 個ではなく 24 個必要になります。
    /// </remarks>
    float normal[3];

    /// <summary>
    /// 頂点の色 (r, g, b, a)。各成分は 0.0〜1.0。
    /// </summary>
    float color[4];

    /// <summary>
    /// テクスチャ座標 (u, v)。左上が (0,0)、右下が (1,1)。
    /// </summary>
    /// <remarks>
    /// V は下向きが正です（画面の Y が上向きなのと逆）。 取り違えるとテクスチャが上下逆さまに貼
    /// られます。
    /// </remarks>
    float uv[2];
};


/// <summary>
/// GPU へ載せる前の形状データ（頂点とインデックスの組）。
/// </summary>
struct MeshData
{
    /// <summary>頂点の並び。</summary>
    std::vector<Vertex> vertices;

    /// <summary>頂点を引く順番。3 個で三角形 1 枚。</summary>
    std::vector<uint16_t> indices;
};


/// <summary>
/// 原点を中心とした立方体を作ります。
/// </summary>
/// <param name="halfExtent">中心から面までの距離。1 辺の長さはこの 2 倍。</param>
/// <returns>頂点 24 個・インデックス 36 個の形状データ。</returns>
/// <remarks>
/// 角は 8 個ですが、面ごとに法線と UV が違うため頂点は 24 個必要です。
/// </remarks>
MeshData CreateCube(float halfExtent);

/// <summary>
/// XZ 平面に広がる、上（+Y）を向いた正方形の床を作ります。
/// </summary>
/// <param name="halfExtent">中心から端までの距離。1 辺の長さはこの 2 倍。</param>
/// <param name="height">床を置く Y 座標。</param>
/// <param name="uvTiling">UV の繰り返し回数。1 より大きくするとテクスチャが並びます。</param>
/// <returns>頂点 4 個・インデックス 6 個の形状データ。</returns>
MeshData CreatePlane(float halfExtent, float height, float uvTiling);

} // namespace dx12
