//=============================================================================
// Terrain.cpp
//   Terrain の実装。
//=============================================================================
#include "Terrain.h"

#include <cmath>

namespace dx12
{
namespace
{

/// <summary>原点まわりを平らにする半径。</summary>
constexpr float kFlatRadius = 6.0f;

/// <summary>平らな部分から起伏へ移り変わる幅。</summary>
constexpr float kBlendWidth = 5.0f;

/// <summary>起伏の高さ。</summary>
constexpr float kHeightScale = 3.2f;

/// <summary>地形をわずかに沈める量。既存の床と Z ファイティングを起こさないため。</summary>
constexpr float kGroundOffset = -0.04f;


/// <summary>
/// 座標から 0〜1 の決まった乱数を作ります。
/// </summary>
/// <param name="x">格子の X。</param>
/// <param name="y">格子の Y。</param>
/// <returns>0 以上 1 未満の値。</returns>
/// <remarks>
/// シェーダー側で使っている Dave Hoskins のハッシュと同じ考え方です。
/// `sin` を使う古い書き方は、値が大きくなると桁が溢れて縞が出ます
/// （[習作 15](../../docs/shader-lab/15_ハッシュの精度.md)）。
/// </remarks>
float Hash(float x, float y)
{
    float px = std::fmod(x * 0.1031f, 1.0f);
    float py = std::fmod(y * 0.1030f, 1.0f);
    float pz = std::fmod(x * 0.0973f, 1.0f);

    const float dot = px * (py + 33.33f) + py * (pz + 33.33f) + pz * (px + 33.33f);

    px += dot;
    py += dot;
    pz += dot;

    const float value = (px + py) * pz;

    return value - std::floor(value);
}


/// <summary>
/// 格子の乱数をなめらかに繋いだ値ノイズを返します。
/// </summary>
/// <param name="x">位置の X。</param>
/// <param name="y">位置の Y。</param>
/// <returns>0〜1 のなめらかな値。</returns>
float ValueNoise(float x, float y)
{
    const float ix = std::floor(x);
    const float iy = std::floor(y);

    const float fx = x - ix;
    const float fy = y - iy;

    // 5 次の補間曲線。1 次微分も 2 次微分も端で 0 になるので、格子の跡が出ない。
    const float ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    const float uy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);

    const float a = Hash(ix, iy);
    const float b = Hash(ix + 1.0f, iy);
    const float c = Hash(ix, iy + 1.0f);
    const float d = Hash(ix + 1.0f, iy + 1.0f);

    const float top    = a + (b - a) * ux;
    const float bottom = c + (d - c) * ux;

    return top + (bottom - top) * uy;
}


/// <summary>
/// 細かさを変えたノイズを重ねます（fBm）。
/// </summary>
/// <param name="x">位置の X。</param>
/// <param name="y">位置の Y。</param>
/// <returns>0〜1 付近の値。</returns>
float Fbm(float x, float y)
{
    float sum       = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;

    for (int octave = 0; octave < 4; ++octave)
    {
        sum       += ValueNoise(x * frequency, y * frequency) * amplitude;
        frequency *= 2.03f;   // ちょうど 2 倍にすると格子が揃って模様が出る
        amplitude *= 0.5f;
    }

    return sum;
}

/// <summary>
/// 箱 1 個ぶんの頂点と索引を、既存の形状データへ足します。
/// </summary>
/// <param name="mesh">足す先。</param>
/// <param name="minimum">箱の最小の角 (x, y, z)。</param>
/// <param name="maximum">箱の最大の角 (x, y, z)。</param>
/// <param name="color">頂点色 (r, g, b, a)。</param>
/// <remarks>
/// 面ごとに法線が違うので、頂点は 8 個ではなく 24 個必要です。
/// </remarks>
void AppendBox(MeshData& mesh, const float minimum[3], const float maximum[3],
               const float color[4])
{
    // 6 面ぶんの { 法線, 4 隅（0 = minimum 側、1 = maximum 側）} の並び。
    struct Face
    {
        float normal[3];
        int   corners[4][3];
    };

    static const Face kFaces[6] = {
        // +Y（上）
        { {  0.0f,  1.0f,  0.0f }, { {0,1,0}, {1,1,0}, {1,1,1}, {0,1,1} } },
        // -Y（下）
        { {  0.0f, -1.0f,  0.0f }, { {0,0,1}, {1,0,1}, {1,0,0}, {0,0,0} } },
        // +X
        { {  1.0f,  0.0f,  0.0f }, { {1,0,0}, {1,0,1}, {1,1,1}, {1,1,0} } },
        // -X
        { { -1.0f,  0.0f,  0.0f }, { {0,0,1}, {0,0,0}, {0,1,0}, {0,1,1} } },
        // +Z
        { {  0.0f,  0.0f,  1.0f }, { {1,0,1}, {0,0,1}, {0,1,1}, {1,1,1} } },
        // -Z
        { {  0.0f,  0.0f, -1.0f }, { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0} } },
    };

    static const float kUv[4][2] = { { 0.0f, 1.0f }, { 1.0f, 1.0f },
                                     { 1.0f, 0.0f }, { 0.0f, 0.0f } };

    static const uint16_t kIndices[6] = { 0, 1, 2, 0, 2, 3 };

    for (const Face& face : kFaces)
    {
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());

        for (int corner = 0; corner < 4; ++corner)
        {
            Vertex vertex = {};

            for (int axis = 0; axis < 3; ++axis)
            {
                vertex.position[axis] = (face.corners[corner][axis] == 0)
                                            ? minimum[axis] : maximum[axis];
                vertex.normal[axis] = face.normal[axis];
            }

            for (int channel = 0; channel < 4; ++channel)
            {
                vertex.color[channel] = color[channel];
            }

            vertex.uv[0] = kUv[corner][0];
            vertex.uv[1] = kUv[corner][1];

            mesh.vertices.push_back(vertex);
        }

        for (uint16_t offset : kIndices)
        {
            mesh.indices.push_back(static_cast<uint16_t>(base + offset));
        }
    }
}

} // namespace


/// <summary>
/// 地形の高さを返します。
/// </summary>
float TerrainHeight(float x, float z)
{
    const float distance = std::sqrt(x * x + z * z);

    // 原点まわりは平ら。既存の床と段差を作らない。
    if (distance <= kFlatRadius)
    {
        return kGroundOffset;
    }

    // 平らな部分から起伏へ、なめらかに移す。
    float blend = (distance - kFlatRadius) / kBlendWidth;
    blend = (blend > 1.0f) ? 1.0f : blend;
    blend = blend * blend * (3.0f - 2.0f * blend);   // smoothstep

    const float height = (Fbm(x * 0.035f, z * 0.035f) - 0.45f) * kHeightScale;

    return kGroundOffset + height * blend;
}


/// <summary>
/// 起伏のある広い地形を作ります。
/// </summary>
MeshData CreateTerrain(float halfExtent, uint32_t resolution)
{
    MeshData mesh;

    // ★ 索引が uint16 なので、頂点は 65,536 個まで。
    //   一辺 255 分割（65,536 頂点）が上限になる。
    const uint32_t sideVertices = resolution + 1;
    const float    step         = (halfExtent * 2.0f) / static_cast<float>(resolution);

    mesh.vertices.reserve(static_cast<size_t>(sideVertices) * sideVertices);

    for (uint32_t row = 0; row < sideVertices; ++row)
    {
        for (uint32_t column = 0; column < sideVertices; ++column)
        {
            const float x = -halfExtent + static_cast<float>(column) * step;
            const float z = -halfExtent + static_cast<float>(row) * step;

            Vertex vertex = {};

            vertex.position[0] = x;
            vertex.position[1] = TerrainHeight(x, z);
            vertex.position[2] = z;

            // 法線は隣との高さの差から求める。
            //   ★ 面ごとに求めて平均するより安く、格子状の地形では十分。
            const float left  = TerrainHeight(x - step, z);
            const float right = TerrainHeight(x + step, z);
            const float back  = TerrainHeight(x, z - step);
            const float front = TerrainHeight(x, z + step);

            const float dx = left - right;
            const float dz = back - front;

            const float length = std::sqrt(dx * dx + dz * dz + (2.0f * step) * (2.0f * step));

            vertex.normal[0] = dx / length;
            vertex.normal[1] = (2.0f * step) / length;
            vertex.normal[2] = dz / length;

            // 高いところほど明るい。起伏が読み取りやすくなる。
            //   ★ 暗くしすぎると、影が落ちても分からない。
            //     影を見せるための地面なので、床と同じくらい明るくする。
            const float shade = 0.58f + 0.07f * (vertex.position[1] + 1.5f);

            vertex.color[0] = shade * 0.92f;
            vertex.color[1] = shade * 0.95f;
            vertex.color[2] = shade * 0.86f;
            vertex.color[3] = 1.0f;

            vertex.uv[0] = static_cast<float>(column) * 0.5f;
            vertex.uv[1] = static_cast<float>(row) * 0.5f;

            mesh.vertices.push_back(vertex);
        }
    }

    mesh.indices.reserve(static_cast<size_t>(resolution) * resolution * 6);

    for (uint32_t row = 0; row < resolution; ++row)
    {
        for (uint32_t column = 0; column < resolution; ++column)
        {
            const uint32_t topLeft     = row * sideVertices + column;
            const uint32_t topRight    = topLeft + 1;
            const uint32_t bottomLeft  = topLeft + sideVertices;
            const uint32_t bottomRight = bottomLeft + 1;

            // ★ 上から見て時計回りにする。床と巻き順を揃えること。
            //   逆にすると裏面として捨てられ、地形が丸ごと見えなくなる。
            //   このとき「空の地面が見えているだけ」なので、
            //   何も描かれていないことに気付きにくい。
            mesh.indices.push_back(topLeft);
            mesh.indices.push_back(bottomRight);
            mesh.indices.push_back(topRight);

            mesh.indices.push_back(topLeft);
            mesh.indices.push_back(bottomLeft);
            mesh.indices.push_back(bottomRight);
        }
    }

    GenerateTangents(mesh);
    mesh.EnsureDefaultMaterial();

    return mesh;
}


/// <summary>
/// 地形の上に柱を散らして、1 つの形状データにまとめます。
/// </summary>
MeshData CreatePillarField(float halfExtent, uint32_t count, float clearRadius)
{
    MeshData mesh;

    // 箱 1 個の面。法線ごとに頂点を分けるので、1 個で 24 頂点・36 索引。
    for (uint32_t index = 0; index < count; ++index)
    {
        const float seed = static_cast<float>(index) * 7.13f;

        const float x = (Hash(seed, 11.7f) * 2.0f - 1.0f) * halfExtent;
        const float z = (Hash(seed, 29.3f) * 2.0f - 1.0f) * halfExtent;

        // 既存の床のまわりには置かない。
        if (std::sqrt(x * x + z * z) < clearRadius)
        {
            continue;
        }

        const float width  = 0.35f + Hash(seed, 41.1f) * 0.9f;
        const float height = 1.2f + Hash(seed, 53.9f) * 4.5f;

        const float baseY = TerrainHeight(x, z);

        const float minimum[3] = { x - width, baseY, z - width };
        const float maximum[3] = { x + width, baseY + height, z + width };

        // 柱ごとに色を少し変える。同じ色だと重なりが読み取れない。
        const float tint = 0.55f + Hash(seed, 67.3f) * 0.45f;

        const float color[4] = { tint * 0.86f, tint * 0.80f, tint * 0.74f, 1.0f };

        AppendBox(mesh, minimum, maximum, color);
    }

    GenerateTangents(mesh);
    mesh.EnsureDefaultMaterial();

    return mesh;
}

} // namespace dx12
