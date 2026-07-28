//=============================================================================
// ModelLoader.cpp
//   ModelLoader の実装。拡張子でローダを振り分ける。
//=============================================================================
#include "ModelLoader.h"

#include "../Common/GraphicsCommon.h"
#include "GltfLoader.h"
#include "ObjLoader.h"

#include <algorithm>
#include <cwctype>   // std::towlower
#include <format>
#include <limits>
#include <stdexcept>

namespace dx12::assets
{
namespace
{
/// <summary>
/// パスから拡張子を取り出し、小文字にして返します。
/// </summary>
/// <param name="filePath">対象のパス。</param>
/// <returns>ドットを含まない小文字の拡張子。無ければ空文字。</returns>
std::wstring LowerCaseExtension(const std::wstring& filePath)
{
    const size_t dot = filePath.find_last_of(L'.');
    if (dot == std::wstring::npos)
    {
        return {};
    }

    std::wstring extension = filePath.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });

    return extension;
}
} // namespace


/// <summary>
/// 拡張子を見て、対応するローダでモデルを読み込みます。
/// </summary>
MeshData LoadModel(const std::wstring& filePath, const ModelLoadOptions& options)
{
    const std::wstring extension = LowerCaseExtension(filePath);

    MeshData mesh;

    if (extension == L"obj")
    {
        mesh = LoadObj(filePath, options);
    }
    else if (extension == L"gltf" || extension == L"glb")
    {
        mesh = LoadGltf(filePath, options);
    }
    else
    {
        throw std::runtime_error("対応していないモデル形式です（.obj / .gltf / .glb）。");
    }

    // 接線を持っていなければ、位置と UV から計算する。
    //   OBJ には接線を書く場所が無く、glTF も TANGENT は省略できるため。
    //   ファイル側が持っているならそちらが正しいので、上書きしない。
    if (!HasTangents(mesh))
    {
        GenerateTangents(mesh);
        Log(L"接線を計算しました（モデルが持っていなかったため）。");
    }

    return mesh;
}


/// <summary>
/// モデルを原点中心へ移動し、指定した大きさに収まるよう拡大縮小します。
/// </summary>
void FitToTargetSize(MeshData& mesh, float targetSize, float groundLevel)
{
    if (mesh.vertices.empty())
    {
        return;
    }

    // (1) 全頂点を囲む直方体（バウンディングボックス）を求める。
    float minimum[3] = { std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max() };
    float maximum[3] = { std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest() };

    for (const Vertex& vertex : mesh.vertices)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
            maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
        }
    }

    // (2) 一番長い辺が targetSize になる倍率を求める。
    //   縦横で別々の倍率にすると形が歪むので、3 軸共通の 1 つの値にする。
    const float extent[3] = { maximum[0] - minimum[0],
                              maximum[1] - minimum[1],
                              maximum[2] - minimum[2] };

    const float longestEdge = std::max({ extent[0], extent[1], extent[2] });
    const float scale = (longestEdge > 1e-8f) ? (targetSize / longestEdge) : 1.0f;

    // (3) 水平方向は中心を原点へ、垂直方向は底面を groundLevel へ合わせる。
    const float centerX = (minimum[0] + maximum[0]) * 0.5f;
    const float centerZ = (minimum[2] + maximum[2]) * 0.5f;

    for (Vertex& vertex : mesh.vertices)
    {
        vertex.position[0] = (vertex.position[0] - centerX) * scale;
        vertex.position[1] = (vertex.position[1] - minimum[1]) * scale + groundLevel;
        vertex.position[2] = (vertex.position[2] - centerZ) * scale;
    }

    // 法線は「向き」なので、3 軸共通の拡大縮小では変わらない。触らなくてよい。

    // 元の大きさを残しておくと、モデルの単位や変換の誤りに気付きやすい。
    Log(std::format(L"モデルの元の大きさ: {:.3f} x {:.3f} x {:.3f} → 倍率 {:.3f}",
                    extent[0], extent[1], extent[2], scale));
}

} // namespace dx12::assets
