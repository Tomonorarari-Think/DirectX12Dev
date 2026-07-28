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

    /// <summary>
    /// 接線 (x, y, z) と、従接線の向き w。法線マップを世界の向きへ直すのに使います。
    /// </summary>
    /// <remarks>
    /// 接線は「U が増える向き」を面の上でたどったベクトルです。w は +1 か -1 で、
    /// 従接線を `cross(normal, tangent) * w` として復元するための符号です。
    /// UV が鏡像になっている面では w が反転します。
    /// </remarks>
    float tangent[4];
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

    /// <summary>金属らしさ。0 が非金属、1 が金属。</summary>
    /// <remarks>
    /// 中間の値に物理的な意味はありません。塗装が剥げた金属のように
    /// 「場所によって違う」ものをテクスチャで表すために存在します。
    /// </remarks>
    float metallicFactor = 0.0f;

    /// <summary>表面の粗さ。0 が鏡のよう、1 がざらざら。</summary>
    float roughnessFactor = 0.6f;

    /// <summary>金属らしさと粗さのテクスチャ。無ければ空。</summary>
    /// <remarks>
    /// glTF の決まりで、**緑が粗さ、青が金属らしさ**です。
    /// 色ではなく数値なので、sRGB として読んではいけません。
    /// </remarks>
    assets::ImageData metallicRoughnessTexture;

    /// <summary>法線マップ。無ければ空。</summary>
    /// <remarks>
    /// 接線空間の法線を RGB に詰めた画像です。**色ではなくベクトル**なので、
    /// sRGB として読んではいけません。
    /// </remarks>
    assets::ImageData normalTexture;

    /// <summary>法線マップの効き具合。1.0 が等倍、0.0 で無効。</summary>
    float normalScale = 1.0f;

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

/// <summary>
/// 頂点の位置と UV から接線を求め、`Vertex::tangent` へ書き込みます。
/// </summary>
/// <param name="mesh">接線を持たない形状データ。書き換えられます。</param>
/// <remarks>
/// 三角形ごとに「U が増える向き」を求め、頂点を共有する面ぶんを足し合わせて
/// ならします。法線マップを使うのに必要ですが、モデル側が接線を持っていれば
/// そちらを優先してください。
///
/// UV が退化している（3 頂点が同じ UV を指す）面は計算できないため飛ばします。
/// 結果が 0 になった頂点には、法線と直交する適当な向きを入れます。
/// </remarks>
void GenerateTangents(MeshData& mesh);

/// <summary>
/// 形状データが接線を持っているかを調べます。
/// </summary>
/// <param name="mesh">調べる形状データ。</param>
/// <returns>1 つでも長さのある接線があれば `true`。</returns>
/// <remarks>
/// 読み込み側が接線を書かなければ、`Vertex` は 0 初期化されて長さ 0 のままです。
/// それを手掛かりに「持っていない」と判定します。
/// </remarks>
bool HasTangents(const MeshData& mesh);

} // namespace dx12
