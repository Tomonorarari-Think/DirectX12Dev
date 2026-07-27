//=============================================================================
// Geometry.cpp
//   Geometry の実装。形状データを組み立てるだけで、DirectX には触れない。
//=============================================================================
#include "Geometry.h"

#include <iterator>  // std::size / std::begin / std::end

namespace dx12
{
namespace
{
/// <summary>
/// 立方体の面 1 枚ぶんの定義（法線・色・4 隅の順序）。
/// </summary>
struct FaceDefinition
{
    /// <summary>面の向き。</summary>
    float normal[3];

    /// <summary>面の色。</summary>
    float color[4];

    /// <summary>
    /// 4 隅の符号。`halfExtent` に掛けて座標にします。
    /// 外から見て時計回りに並べること（逆にすると背面カリングで消えます）。
    /// </summary>
    float corners[4][3];
};

/// <summary>
/// 立方体の 6 面。面ごとに色を変えて、回転しているのが分かるようにしています。
/// </summary>
constexpr FaceDefinition kCubeFaces[] = {
    // 手前 (-Z) 青
    { {  0.0f,  0.0f, -1.0f }, { 0.35f, 0.55f, 0.95f, 1.0f },
      { { -1,  1, -1 }, {  1,  1, -1 }, {  1, -1, -1 }, { -1, -1, -1 } } },

    // 奥 (+Z) 緑
    { {  0.0f,  0.0f,  1.0f }, { 0.35f, 0.85f, 0.45f, 1.0f },
      { {  1,  1,  1 }, { -1,  1,  1 }, { -1, -1,  1 }, {  1, -1,  1 } } },

    // 左 (-X) 赤
    { { -1.0f,  0.0f,  0.0f }, { 0.90f, 0.40f, 0.35f, 1.0f },
      { { -1,  1,  1 }, { -1,  1, -1 }, { -1, -1, -1 }, { -1, -1,  1 } } },

    // 右 (+X) 黄
    { {  1.0f,  0.0f,  0.0f }, { 0.95f, 0.80f, 0.30f, 1.0f },
      { {  1,  1, -1 }, {  1,  1,  1 }, {  1, -1,  1 }, {  1, -1, -1 } } },

    // 上 (+Y) 水色
    { {  0.0f,  1.0f,  0.0f }, { 0.45f, 0.85f, 0.95f, 1.0f },
      { { -1,  1,  1 }, {  1,  1,  1 }, {  1,  1, -1 }, { -1,  1, -1 } } },

    // 下 (-Y) 紫
    { {  0.0f, -1.0f,  0.0f }, { 0.70f, 0.50f, 0.95f, 1.0f },
      { { -1, -1, -1 }, {  1, -1, -1 }, {  1, -1,  1 }, { -1, -1,  1 } } },
};

/// <summary>
/// 四角形 1 枚ぶんの UV。4 隅の順序に対応します。
/// </summary>
constexpr float kQuadUv[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f },
                                  { 1.0f, 1.0f }, { 0.0f, 1.0f } };

/// <summary>
/// 四角形 1 枚を三角形 2 枚に分けるインデックスの並び。
/// </summary>
constexpr uint16_t kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };
} // namespace


/// <summary>
/// 原点を中心とした立方体を作ります。
/// </summary>
MeshData CreateCube(float halfExtent)
{
    MeshData mesh;
    mesh.vertices.reserve(std::size(kCubeFaces) * 4);
    mesh.indices.reserve(std::size(kCubeFaces) * 6);

    for (const FaceDefinition& face : kCubeFaces)
    {
        // インデックスは「この面の頂点が何番から始まるか」を基準に付ける。
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());

        for (int corner = 0; corner < 4; ++corner)
        {
            Vertex vertex = {};

            for (int axis = 0; axis < 3; ++axis)
            {
                vertex.position[axis] = face.corners[corner][axis] * halfExtent;
                vertex.normal[axis]   = face.normal[axis];
            }

            for (int channel = 0; channel < 4; ++channel)
            {
                vertex.color[channel] = face.color[channel];
            }

            vertex.uv[0] = kQuadUv[corner][0];
            vertex.uv[1] = kQuadUv[corner][1];

            mesh.vertices.push_back(vertex);
        }

        for (uint16_t offset : kQuadIndices)
        {
            mesh.indices.push_back(static_cast<uint16_t>(base + offset));
        }
    }

    return mesh;
}


/// <summary>
/// XZ 平面に広がる、上（+Y）を向いた正方形の床を作ります。
/// </summary>
MeshData CreatePlane(float halfExtent, float height, float uvTiling)
{
    // 上から見て時計回り。立方体の上面と同じ並び順にしてある。
    const float corners[4][2] = {
        { -halfExtent,  halfExtent },
        {  halfExtent,  halfExtent },
        {  halfExtent, -halfExtent },
        { -halfExtent, -halfExtent },
    };

    MeshData mesh;
    mesh.vertices.reserve(4);

    for (int corner = 0; corner < 4; ++corner)
    {
        Vertex vertex = {};

        vertex.position[0] = corners[corner][0];
        vertex.position[1] = height;
        vertex.position[2] = corners[corner][1];

        vertex.normal[0] = 0.0f;
        vertex.normal[1] = 1.0f;
        vertex.normal[2] = 0.0f;

        // 床は陰影が読み取りやすいよう、彩度の低い明るい色にする。
        vertex.color[0] = 0.78f;
        vertex.color[1] = 0.79f;
        vertex.color[2] = 0.84f;
        vertex.color[3] = 1.0f;

        // UV を 1 より大きくすると、サンプラーの WRAP 設定によって模様が繰り返される。
        vertex.uv[0] = kQuadUv[corner][0] * uvTiling;
        vertex.uv[1] = kQuadUv[corner][1] * uvTiling;

        mesh.vertices.push_back(vertex);
    }

    mesh.indices.assign(std::begin(kQuadIndices), std::end(kQuadIndices));

    return mesh;
}

} // namespace dx12
