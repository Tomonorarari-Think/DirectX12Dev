//=============================================================================
// Geometry.cpp
//   Geometry の実装。形状データを組み立てるだけで、DirectX には触れない。
//=============================================================================
#include "Geometry.h"

#include <array>
#include <cmath>
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
/// 材質やサブメッシュが未設定なら、既定のものを 1 つ用意します。
/// </summary>
void MeshData::EnsureDefaultMaterial()
{
    if (materials.empty())
    {
        // 白 1 色。頂点カラーとテクスチャがそのまま出る。
        MaterialData material;
        material.name = "default";
        materials.push_back(std::move(material));
    }

    if (subMeshes.empty() && !indices.empty())
    {
        SubMesh subMesh;
        subMesh.indexOffset   = 0;
        subMesh.indexCount    = static_cast<uint32_t>(indices.size());
        subMesh.materialIndex = 0;
        subMeshes.push_back(subMesh);
    }
}


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

    GenerateTangents(mesh);
    mesh.EnsureDefaultMaterial();
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

    GenerateTangents(mesh);
    mesh.EnsureDefaultMaterial();
    return mesh;
}

/// <summary>
/// 形状データが接線を持っているかを調べます。
/// </summary>
bool HasTangents(const MeshData& mesh)
{
    for (const Vertex& vertex : mesh.vertices)
    {
        const float lengthSquared = vertex.tangent[0] * vertex.tangent[0]
                                  + vertex.tangent[1] * vertex.tangent[1]
                                  + vertex.tangent[2] * vertex.tangent[2];
        if (lengthSquared > 1e-8f)
        {
            return true;
        }
    }
    return false;
}


/// <summary>
/// 頂点の位置と UV から接線を求め、`Vertex::tangent` へ書き込みます。
/// </summary>
void GenerateTangents(MeshData& mesh)
{
    const size_t vertexCount = mesh.vertices.size();

    // 面ごとの結果を足し込む場所。共有する頂点はここでならされる。
    std::vector<std::array<float, 3>> tangentSum(vertexCount, { 0.0f, 0.0f, 0.0f });
    std::vector<std::array<float, 3>> bitangentSum(vertexCount, { 0.0f, 0.0f, 0.0f });

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const uint16_t i0 = mesh.indices[i];
        const uint16_t i1 = mesh.indices[i + 1];
        const uint16_t i2 = mesh.indices[i + 2];

        const Vertex& v0 = mesh.vertices[i0];
        const Vertex& v1 = mesh.vertices[i1];
        const Vertex& v2 = mesh.vertices[i2];

        // 三角形の 2 辺を、位置と UV の両方で見る。
        const float e1[3] = { v1.position[0] - v0.position[0],
                              v1.position[1] - v0.position[1],
                              v1.position[2] - v0.position[2] };
        const float e2[3] = { v2.position[0] - v0.position[0],
                              v2.position[1] - v0.position[1],
                              v2.position[2] - v0.position[2] };

        const float du1 = v1.uv[0] - v0.uv[0];
        const float dv1 = v1.uv[1] - v0.uv[1];
        const float du2 = v2.uv[0] - v0.uv[0];
        const float dv2 = v2.uv[1] - v0.uv[1];

        // 連立方程式の行列式。0 に近い面は UV が潰れていて解けない。
        const float determinant = du1 * dv2 - du2 * dv1;
        if (std::fabs(determinant) < 1e-12f)
        {
            continue;
        }

        const float inverse = 1.0f / determinant;

        const float tangent[3] = { (dv2 * e1[0] - dv1 * e2[0]) * inverse,
                                   (dv2 * e1[1] - dv1 * e2[1]) * inverse,
                                   (dv2 * e1[2] - dv1 * e2[2]) * inverse };

        const float bitangent[3] = { (du1 * e2[0] - du2 * e1[0]) * inverse,
                                     (du1 * e2[1] - du2 * e1[1]) * inverse,
                                     (du1 * e2[2] - du2 * e1[2]) * inverse };

        for (uint16_t index : { i0, i1, i2 })
        {
            for (int c = 0; c < 3; ++c)
            {
                tangentSum[index][c]   += tangent[c];
                bitangentSum[index][c] += bitangent[c];
            }
        }
    }

    for (size_t i = 0; i < vertexCount; ++i)
    {
        Vertex& vertex = mesh.vertices[i];

        const float* n = vertex.normal;
        float t[3] = { tangentSum[i][0], tangentSum[i][1], tangentSum[i][2] };

        // 法線と直交させる（グラム・シュミット）。
        //   ならした結果は法線から少し傾いているので、その成分を抜く。
        const float projection = t[0] * n[0] + t[1] * n[1] + t[2] * n[2];
        for (int c = 0; c < 3; ++c)
        {
            t[c] -= n[c] * projection;
        }

        float length = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);

        if (length < 1e-8f)
        {
            // UV が無い、または潰れている頂点。法線と直交する適当な向きを入れる。
            //   向きは何でもよいが、法線と平行にだけはしない。
            float axis[3] = { 0.0f, 1.0f, 0.0f };
            if (std::fabs(n[1]) > 0.9f)
            {
                axis[0] = 1.0f; axis[1] = 0.0f; axis[2] = 0.0f;
            }

            t[0] = axis[1] * n[2] - axis[2] * n[1];
            t[1] = axis[2] * n[0] - axis[0] * n[2];
            t[2] = axis[0] * n[1] - axis[1] * n[0];

            length = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            if (length < 1e-8f)
            {
                t[0] = 1.0f; t[1] = 0.0f; t[2] = 0.0f;
                length = 1.0f;
            }
        }

        for (int c = 0; c < 3; ++c)
        {
            vertex.tangent[c] = t[c] / length;
        }

        // 従接線の向き。UV が鏡像になっている面では反転する。
        const float cross[3] = { n[1] * t[2] - n[2] * t[1],
                                 n[2] * t[0] - n[0] * t[2],
                                 n[0] * t[1] - n[1] * t[0] };

        const float handedness = cross[0] * bitangentSum[i][0]
                               + cross[1] * bitangentSum[i][1]
                               + cross[2] * bitangentSum[i][2];

        vertex.tangent[3] = (handedness < 0.0f) ? -1.0f : 1.0f;
    }
}

} // namespace dx12
