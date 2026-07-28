//=============================================================================
// ObjLoader.cpp
//   ObjLoader の実装。
//=============================================================================
#include "ObjLoader.h"

#include "../Common/GraphicsCommon.h"
#include "ImageLoader.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <format>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace dx12::assets
{
namespace
{
/// <summary>
/// 面が参照する「座標・UV・法線」の 3 つ組。OBJ の `f` に書かれる `1/2/3` のこと。
/// </summary>
/// <remarks>
/// 未指定の要素は -1 にします。この 3 つ組が同じものだけが同じ頂点になります。
/// </remarks>
struct FaceVertexKey
{
    /// <summary>座標の番号（0 始まりに直したもの）。</summary>
    int position = -1;

    /// <summary>UV の番号。無ければ -1。</summary>
    int texCoord = -1;

    /// <summary>法線の番号。無ければ -1。</summary>
    int normal = -1;

    /// <summary>3 つとも一致するかを判定します。</summary>
    bool operator==(const FaceVertexKey& other) const noexcept
    {
        return position == other.position
            && texCoord == other.texCoord
            && normal == other.normal;
    }
};


/// <summary>
/// `FaceVertexKey` をハッシュ表の鍵に使うための関数オブジェクト。
/// </summary>
struct FaceVertexKeyHash
{
    /// <summary>3 つの番号を混ぜ合わせてハッシュ値にします。</summary>
    size_t operator()(const FaceVertexKey& key) const noexcept
    {
        // 適当な素数を掛けながら混ぜる、よくある手法。
        size_t hash = static_cast<size_t>(key.position);
        hash = hash * 31 + static_cast<size_t>(key.texCoord + 1);
        hash = hash * 31 + static_cast<size_t>(key.normal + 1);
        return hash;
    }
};


/// <summary>
/// OBJ の索引（1 始まり、負なら末尾からの相対）を 0 始まりに直します。
/// </summary>
/// <param name="value">ファイルに書かれていた値。</param>
/// <param name="count">その要素が現在いくつ読み込まれているか。</param>
/// <returns>0 始まりの番号。範囲外なら -1。</returns>
/// <remarks>
/// OBJ の索引は 1 始まりです。さらに負の値は「末尾から数えて何番目」を意味します。
/// -1 が最後の要素で、この相対指定は**その行までに読んだ数**が基準になります。
/// </remarks>
int ResolveIndex(int value, size_t count)
{
    if (value > 0)
    {
        const int index = value - 1;
        return (static_cast<size_t>(index) < count) ? index : -1;
    }

    if (value < 0)
    {
        const int index = static_cast<int>(count) + value;
        return (index >= 0) ? index : -1;
    }

    return -1;   // 0 は未指定を表す
}


/// <summary>
/// `12/34/56` の形をした面頂点の記述を分解します。
/// </summary>
/// <param name="token">分解する文字列。</param>
/// <param name="positionCount">ここまでに読んだ座標の数。</param>
/// <param name="texCoordCount">ここまでに読んだ UV の数。</param>
/// <param name="normalCount">ここまでに読んだ法線の数。</param>
/// <returns>0 始まりに直した 3 つ組。</returns>
FaceVertexKey ParseFaceVertex(const std::string& token,
                              size_t positionCount,
                              size_t texCoordCount,
                              size_t normalCount)
{
    // `/` で最大 3 つに割る。`12//56` のように途中が空のこともある。
    std::array<int, 3> raw = { 0, 0, 0 };

    size_t start = 0;
    for (int slot = 0; slot < 3 && start <= token.size(); ++slot)
    {
        const size_t separator = token.find('/', start);
        const size_t end = (separator == std::string::npos) ? token.size() : separator;

        if (end > start)
        {
            raw[slot] = std::atoi(token.substr(start, end - start).c_str());
        }

        if (separator == std::string::npos)
        {
            break;
        }
        start = separator + 1;
    }

    FaceVertexKey key;
    key.position = ResolveIndex(raw[0], positionCount);
    key.texCoord = ResolveIndex(raw[1], texCoordCount);
    key.normal   = ResolveIndex(raw[2], normalCount);
    return key;
}


/// <summary>
/// パスからフォルダの部分（末尾の区切りを含む）を取り出します。
/// </summary>
std::wstring DirectoryOf(const std::wstring& filePath)
{
    const size_t separator = filePath.find_last_of(L"\\/");
    return (separator == std::wstring::npos) ? std::wstring()
                                             : filePath.substr(0, separator + 1);
}


/// <summary>
/// マテリアルライブラリ (.mtl) を読み込みます。
/// </summary>
/// <param name="filePath">.mtl ファイルの絶対パス。</param>
/// <param name="baseDirectory">画像を探す基準のフォルダ。</param>
/// <param name="materials">読み込んだ材質を追加する先。</param>
/// <param name="indexByName">材質名から番号を引く表。ここへも登録します。</param>
/// <remarks>
/// 読むのは `newmtl` / `Kd` / `map_Kd` / `Pm` / `Pr` / `Ns` /
/// `map_Bump`（法線マップ）だけです。
/// 鏡面色 `Ks` や透明度 `d` などは読み飛ばします。
/// </remarks>
void LoadMaterialLibrary(const std::wstring& filePath,
                         const std::wstring& baseDirectory,
                         std::vector<MaterialData>& materials,
                         std::unordered_map<std::string, uint32_t>& indexByName)
{
    std::ifstream file(filePath);
    if (!file)
    {
        LogError(L"mtl ファイルを開けませんでした: " + filePath);
        return;
    }

    MaterialData* current = nullptr;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        const size_t comment = line.find('#');
        if (comment != std::string::npos)
        {
            line.erase(comment);
        }

        std::istringstream stream(line);
        std::string keyword;
        if (!(stream >> keyword))
        {
            continue;
        }

        if (keyword == "newmtl")
        {
            std::string name;
            stream >> name;

            indexByName[name] = static_cast<uint32_t>(materials.size());

            MaterialData material;
            material.name = name;
            materials.push_back(std::move(material));

            current = &materials.back();
        }
        else if (keyword == "Kd" && current != nullptr)
        {
            // 拡散色。0〜1 の 3 成分。
            stream >> current->baseColorFactor[0]
                   >> current->baseColorFactor[1]
                   >> current->baseColorFactor[2];
        }
        else if (keyword == "Pm" && current != nullptr)
        {
            // PBR 拡張。標準の MTL には無いが、広く使われている。
            stream >> current->metallicFactor;
        }
        else if (keyword == "Pr" && current != nullptr)
        {
            stream >> current->roughnessFactor;
        }
        else if (keyword == "Ns" && current != nullptr)
        {
            // 旧来の鏡面指数。Pr が書かれていないファイル向けの近似。
            //   指数が大きいほど鋭い＝粗くない、という関係を素朴に写す。
            float shininess = 0.0f;
            stream >> shininess;

            const float normalized = std::sqrt(std::max(shininess, 0.0f) / 1000.0f);
            current->roughnessFactor = 1.0f - std::min(normalized, 1.0f);
        }
        else if ((keyword == "map_Bump" || keyword == "bump" || keyword == "norm")
                 && current != nullptr)
        {
            // 法線マップ。表記が 3 通りあるのは、規格が拡張されてきた経緯による。
            //   map_Bump / bump は本来グレースケールの高さマップを指すが、
            //   実際には法線マップが置かれていることが多い。norm は PBR 拡張。
            std::string name;
            std::getline(stream, name);

            const size_t first = name.find_first_not_of(" \t");
            if (first == std::string::npos)
            {
                continue;
            }
            name = name.substr(first);

            std::wstring relative(name.begin(), name.end());
            for (wchar_t& c : relative)
            {
                if (c == L'/') { c = L'\\'; }
            }

            try
            {
                current->normalTexture = LoadImageFile(baseDirectory + relative);
            }
            catch (const std::exception&)
            {
                LogError(L"mtl の法線マップを読めませんでした: " + relative);
            }
        }
        else if (keyword == "map_Kd" && current != nullptr)
        {
            // 行の残り全部がファイル名。空白を含む名前もあり得る。
            std::string name;
            std::getline(stream, name);

            // 先頭の空白を落とす。
            const size_t first = name.find_first_not_of(" \t");
            if (first == std::string::npos)
            {
                continue;
            }
            name = name.substr(first);

            std::wstring relative(name.begin(), name.end());
            for (wchar_t& c : relative)
            {
                if (c == L'/') { c = L'\\'; }
            }

            try
            {
                current->baseColorTexture = LoadImageFile(baseDirectory + relative);
            }
            catch (const std::exception&)
            {
                LogError(L"mtl のテクスチャを読めませんでした: " + relative);
            }
        }
    }
}


/// <summary>
/// 法線が 1 つも書かれていなかった場合に、面の向きから法線を作ります。
/// </summary>
/// <param name="mesh">対象のメッシュ。法線が書き換わります。</param>
/// <param name="vertexPositionIndex">各頂点がどの座標を参照しているか。</param>
/// <param name="positionCount">座標の総数。</param>
/// <remarks>
/// 座標が同じ頂点どうしで足し合わせてから正規化します（スムーズシェーディング）。
/// UV の継ぎ目で頂点が分かれていても、法線は同じ向きになります。
/// </remarks>
void GenerateSmoothNormals(MeshData& mesh,
                           const std::vector<int>& vertexPositionIndex,
                           size_t positionCount)
{
    std::vector<std::array<float, 3>> accumulated(positionCount, { 0.0f, 0.0f, 0.0f });

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const Vertex& a = mesh.vertices[mesh.indices[i + 0]];
        const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[i + 2]];

        // 2 辺の外積が面の法線。長さは三角形の面積の 2 倍になるので、
        // 足し合わせると自然に「大きい面ほど強く効く」重み付けになる。
        const float e1[3] = { b.position[0] - a.position[0],
                              b.position[1] - a.position[1],
                              b.position[2] - a.position[2] };
        const float e2[3] = { c.position[0] - a.position[0],
                              c.position[1] - a.position[1],
                              c.position[2] - a.position[2] };

        const float faceNormal[3] = { e1[1] * e2[2] - e1[2] * e2[1],
                                      e1[2] * e2[0] - e1[0] * e2[2],
                                      e1[0] * e2[1] - e1[1] * e2[0] };

        for (int corner = 0; corner < 3; ++corner)
        {
            const int positionIndex = vertexPositionIndex[mesh.indices[i + corner]];
            if (positionIndex < 0)
            {
                continue;
            }

            for (int axis = 0; axis < 3; ++axis)
            {
                accumulated[positionIndex][axis] += faceNormal[axis];
            }
        }
    }

    for (size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        const int positionIndex = vertexPositionIndex[i];
        if (positionIndex < 0)
        {
            continue;
        }

        const std::array<float, 3>& sum = accumulated[positionIndex];
        const float length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);

        if (length > 1e-8f)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                mesh.vertices[i].normal[axis] = sum[axis] / length;
            }
        }
        else
        {
            // 面積 0 の三角形しか無い頂点。上向きにしておく。
            mesh.vertices[i].normal[0] = 0.0f;
            mesh.vertices[i].normal[1] = 1.0f;
            mesh.vertices[i].normal[2] = 0.0f;
        }
    }
}
} // namespace


/// <summary>
/// Wavefront OBJ ファイルを読み込みます。
/// </summary>
MeshData LoadObj(const std::wstring& filePath, const ModelLoadOptions& options)
{
    std::ifstream file(filePath);
    if (!file)
    {
        throw std::runtime_error("OBJ ファイルを開けませんでした。");
    }

    // ファイルに書かれている生の配列。面はここへの索引で書かれる。
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 2>> texCoords;
    std::vector<std::array<float, 3>> normals;

    MeshData mesh;

    // 同じ 3 つ組が何度出てきても、頂点は 1 個で済ませるための対応表。
    std::unordered_map<FaceVertexKey, uint16_t, FaceVertexKeyHash> vertexLookup;

    // 各頂点がどの座標を参照しているか。法線を後から作るときに使う。
    std::vector<int> vertexPositionIndex;

    bool hasAnyNormal = false;

    // 材質。mtllib で追加され、usemtl で切り替わる。
    std::unordered_map<std::string, uint32_t> materialIndexByName;
    const std::wstring baseDirectory = DirectoryOf(filePath);

    // いま書き込み中のサブメッシュ。材質が変わるたびに区切る。
    uint32_t currentMaterial = UINT32_MAX;
    uint32_t currentStart    = 0;

    // 直前のサブメッシュを閉じる処理。
    const auto closeSubMesh = [&]() {
        const uint32_t here = static_cast<uint32_t>(mesh.indices.size());
        if (currentMaterial != UINT32_MAX && here > currentStart)
        {
            SubMesh subMesh;
            subMesh.indexOffset   = currentStart;
            subMesh.indexCount    = here - currentStart;
            subMesh.materialIndex = currentMaterial;
            mesh.subMeshes.push_back(subMesh);
        }
        currentStart = here;
    };

    std::string line;
    while (std::getline(file, line))
    {
        // 行末の CR（Windows 改行をテキストモード以外で読んだ場合）を落とす。
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        // `#` から先はコメント。
        const size_t comment = line.find('#');
        if (comment != std::string::npos)
        {
            line.erase(comment);
        }

        std::istringstream stream(line);
        std::string keyword;
        if (!(stream >> keyword))
        {
            continue;   // 空行
        }

        if (keyword == "v")
        {
            std::array<float, 3> position = {};
            stream >> position[0] >> position[1] >> position[2];

            // ★ 右手座標系 → 左手座標系
            //   OBJ は +Z が手前を向く右手系。本プロジェクトは +Z が奥の左手系。
            //   Z の符号を反転しないと、前後が鏡写しになったモデルが表示される。
            //
            //   面の並び順（インデックスの順序）は変えなくてよい。視線に沿った鏡映は
            //   画面上での回り順を変えないため、OBJ の「外から見て反時計回り」は
            //   そのまま D3D の画面座標（Y が下向き）では時計回りになり、
            //   「時計回りが表」という本プロジェクトの設定にちょうど一致する。
            position[2] = -position[2];

            positions.push_back(position);
        }
        else if (keyword == "vt")
        {
            std::array<float, 2> uv = {};
            stream >> uv[0] >> uv[1];

            // OBJ の V は下から上へ増える。DirectX の V は上から下なので反転する。
            uv[1] = 1.0f - uv[1];

            texCoords.push_back(uv);
        }
        else if (keyword == "vn")
        {
            std::array<float, 3> normal = {};
            stream >> normal[0] >> normal[1] >> normal[2];

            // 座標と同じ理由で Z を反転する。
            normal[2] = -normal[2];

            normals.push_back(normal);
            hasAnyNormal = true;
        }
        else if (keyword == "f")
        {
            // 1 行に 3 個とは限らない。四角形や、それ以上の多角形も書ける。
            std::vector<uint16_t> faceIndices;

            std::string token;
            while (stream >> token)
            {
                const FaceVertexKey key =
                    ParseFaceVertex(token, positions.size(), texCoords.size(), normals.size());

                if (key.position < 0)
                {
                    continue;   // 壊れた索引は読み飛ばす
                }

                const auto found = vertexLookup.find(key);
                if (found != vertexLookup.end())
                {
                    faceIndices.push_back(found->second);
                    continue;
                }

                if (mesh.vertices.size() >= UINT16_MAX)
                {
                    throw std::runtime_error(
                        "頂点が多すぎます。インデックスを 32bit にしてください。");
                }

                Vertex vertex = {};

                const std::array<float, 3>& position = positions[key.position];
                vertex.position[0] = position[0];
                vertex.position[1] = position[1];
                vertex.position[2] = position[2];

                if (key.normal >= 0)
                {
                    const std::array<float, 3>& normal = normals[key.normal];
                    vertex.normal[0] = normal[0];
                    vertex.normal[1] = normal[1];
                    vertex.normal[2] = normal[2];
                }

                if (key.texCoord >= 0)
                {
                    vertex.uv[0] = texCoords[key.texCoord][0];
                    vertex.uv[1] = texCoords[key.texCoord][1];
                }

                // 色は材質が持つので、頂点カラーは白にしておく。
                for (int channel = 0; channel < 4; ++channel)
                {
                    vertex.color[channel] = 1.0f;
                }

                const uint16_t newIndex = static_cast<uint16_t>(mesh.vertices.size());
                mesh.vertices.push_back(vertex);
                vertexPositionIndex.push_back(key.position);
                vertexLookup.emplace(key, newIndex);

                faceIndices.push_back(newIndex);
            }

            // 多角形を三角形に割る（ファン分割）。
            //   0-1-2、0-2-3、0-3-4 … と、先頭の頂点を軸に扇形へ広げる。
            //   凸多角形でしか正しくないが、OBJ の面はほぼ凸なので実用上は足りる。
            for (size_t i = 1; i + 1 < faceIndices.size(); ++i)
            {
                mesh.indices.push_back(faceIndices[0]);
                mesh.indices.push_back(faceIndices[i]);
                mesh.indices.push_back(faceIndices[i + 1]);
            }
        }
        else if (keyword == "mtllib")
        {
            // 材質の定義は別ファイルに置かれている。
            std::string name;
            std::getline(stream, name);

            const size_t first = name.find_first_not_of(" \t");
            if (first == std::string::npos)
            {
                continue;
            }

            const std::string trimmed = name.substr(first);
            std::wstring relative(trimmed.begin(), trimmed.end());

            LoadMaterialLibrary(baseDirectory + relative, baseDirectory,
                                mesh.materials, materialIndexByName);
        }
        else if (keyword == "usemtl")
        {
            // ここから先の面は別の材質で描く。いったん区切る。
            std::string name;
            stream >> name;

            closeSubMesh();

            const auto found = materialIndexByName.find(name);
            currentMaterial = (found != materialIndexByName.end())
                                ? found->second : UINT32_MAX;
        }
        // それ以外（g / o / s など）は読み飛ばす。
    }

    closeSubMesh();

    if (mesh.vertices.empty() || mesh.indices.empty())
    {
        throw std::runtime_error("OBJ に面が 1 つも含まれていませんでした。");
    }

    // usemtl が 1 度も出てこなかった場合は、options.color の材質を 1 つ作る。
    if (mesh.subMeshes.empty())
    {
        MaterialData material;
        material.name = "default";
        for (int channel = 0; channel < 4; ++channel)
        {
            material.baseColorFactor[channel] = options.color[channel];
        }

        mesh.materials.push_back(std::move(material));

        SubMesh subMesh;
        subMesh.indexOffset   = 0;
        subMesh.indexCount    = static_cast<uint32_t>(mesh.indices.size());
        subMesh.materialIndex = static_cast<uint32_t>(mesh.materials.size() - 1);
        mesh.subMeshes.push_back(subMesh);
    }

    if (!hasAnyNormal)
    {
        GenerateSmoothNormals(mesh, vertexPositionIndex, positions.size());
    }

    if (options.fitToTargetSize)
    {
        FitToTargetSize(mesh, options.targetSize, options.groundLevel);
    }

    Log(std::format(
        L"OBJ を読み込みました（頂点 {} 個 / 三角形 {} 枚 / 材質 {} 個 / 法線は{}）",
        mesh.vertices.size(),
        mesh.indices.size() / 3,
        mesh.materials.size(),
        hasAnyNormal ? L"ファイル内の値" : L"自動生成"));

    return mesh;
}

} // namespace dx12::assets
