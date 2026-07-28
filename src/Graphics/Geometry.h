//=============================================================================
// Geometry.h
//   頂点の形式と、基本的な形状（立方体・床）のデータ生成。
//
//   DirectX の API を呼ばず、頂点とインデックスの配列を作るだけの層。
//   GPU へ載せる処理は Mesh が受け持つ。
//=============================================================================
#pragma once

#include "../Assets/ImageLoader.h"

#include <cstdint>
#include <string>
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
/// 材質 1 つぶんの情報。
/// </summary>
/// <remarks>
/// 画像そのものを持たせています。`ImageData` は DirectX を知らない素の構造体なので、
/// この層から参照しても「DirectX に依存しない」という切り分けは崩れません。
/// </remarks>
struct MaterialData
{
    /// <summary>材質の名前。ログや調査に使うだけで、描画には影響しません。</summary>
    std::string name;

    /// <summary>基本色 (r, g, b, a)。テクスチャと掛け合わせます。</summary>
    float baseColorFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    /// <summary>基本色テクスチャ。無ければ空。</summary>
    assets::ImageData baseColorTexture;

    /// <summary>テクスチャを持っているかどうかを返します。</summary>
    /// <returns>持っていれば `true`。</returns>
    bool HasTexture() const { return !baseColorTexture.pixels.empty(); }
};


/// <summary>
/// 1 つの材質で描ける、インデックスの連続した範囲。
/// </summary>
/// <remarks>
/// 材質が変わるたびに描画を区切る必要があるため、範囲を分けて持ちます。
/// </remarks>
struct SubMesh
{
    /// <summary>インデックス配列の何番目から始まるか。</summary>
    uint32_t indexOffset = 0;

    /// <summary>何個ぶんのインデックスを使うか。</summary>
    uint32_t indexCount = 0;

    /// <summary>使う材質の番号。</summary>
    uint32_t materialIndex = 0;
};


/// <summary>
/// GPU へ載せる前の形状データ。
/// </summary>
struct MeshData
{
    /// <summary>頂点の並び。</summary>
    std::vector<Vertex> vertices;

    /// <summary>頂点を引く順番。3 個で三角形 1 枚。</summary>
    std::vector<uint16_t> indices;

    /// <summary>材質ごとに区切った描画範囲。</summary>
    /// <remarks>
    /// 空の場合、呼び出し側は「材質 0 番で全体を描く」とみなします。
    /// </remarks>
    std::vector<SubMesh> subMeshes;

    /// <summary>この形状が使う材質の一覧。</summary>
    /// <remarks>空の場合は、白 1 色の既定の材質が使われます。</remarks>
    std::vector<MaterialData> materials;

    /// <summary>
    /// 材質やサブメッシュが未設定なら、既定のものを 1 つ用意します。
    /// </summary>
    /// <remarks>
    /// 読み込み側がどう作っても、描画側から見た形を揃えるための後始末です。
    /// </remarks>
    void EnsureDefaultMaterial();
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
