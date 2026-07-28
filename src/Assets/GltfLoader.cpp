//=============================================================================
// GltfLoader.cpp
//   GltfLoader の実装。
//=============================================================================
#include "GltfLoader.h"

#include "../Common/GraphicsCommon.h"
#include "ImageLoader.h"
#include "Json.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace DirectX;

namespace dx12::assets
{
namespace
{
/// <summary>GLB の先頭 4 バイト。リトルエンディアンで読むと "glTF"。</summary>
constexpr uint32_t kGlbMagic = 0x46546C67;

/// <summary>GLB の JSON チャンクの種類。</summary>
constexpr uint32_t kGlbChunkJson = 0x4E4F534A;

/// <summary>GLB のバイナリチャンクの種類。</summary>
constexpr uint32_t kGlbChunkBinary = 0x004E4942;

/// <summary>アクセサの componentType（glTF が定める OpenGL 由来の番号）。</summary>
enum ComponentType : int
{
    kByte          = 5120,
    kUnsignedByte  = 5121,
    kShort         = 5122,
    kUnsignedShort = 5123,
    kUnsignedInt   = 5125,
    kFloat         = 5126,
};


/// <summary>
/// 読み込んだ glTF 全体。JSON と、buffers の実体をまとめて持ちます。
/// </summary>
struct Document
{
    /// <summary>JSON の中身。</summary>
    json::Value root;

    /// <summary>`buffers` の実体。番号は JSON の buffers と同じ並び。</summary>
    std::vector<std::vector<uint8_t>> buffers;
};


/// <summary>ファイル全体をバイト列として読み込みます。</summary>
std::vector<uint8_t> ReadAllBytes(const std::wstring& filePath)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("glTF ファイルを開けませんでした。");
    }

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        throw std::runtime_error("glTF ファイルの読み込みに失敗しました。");
    }

    return bytes;
}


/// <summary>Base64 の文字列をバイト列に戻します。</summary>
std::vector<uint8_t> DecodeBase64(const std::string& encoded)
{
    // 文字 → 6 ビットの値。'=' は詰め物なので -1 として弾く。
    const auto valueOf = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') { return c - 'A'; }
        if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
        if (c >= '0' && c <= '9') { return c - '0' + 52; }
        if (c == '+') { return 62; }
        if (c == '/') { return 63; }
        return -1;
    };

    std::vector<uint8_t> bytes;
    bytes.reserve(encoded.size() * 3 / 4);

    uint32_t accumulator = 0;
    int bitsHeld = 0;

    for (const char c : encoded)
    {
        const int value = valueOf(c);
        if (value < 0)
        {
            continue;   // '=' や改行
        }

        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bitsHeld += 6;

        // 8 ビット溜まるたびに 1 バイト取り出す。
        if (bitsHeld >= 8)
        {
            bitsHeld -= 8;
            bytes.push_back(static_cast<uint8_t>((accumulator >> bitsHeld) & 0xFF));
        }
    }

    return bytes;
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


/// <summary>URI の %xx を元の文字に戻します。</summary>
std::string DecodePercentEscapes(const std::string& uri)
{
    std::string result;
    result.reserve(uri.size());

    for (size_t i = 0; i < uri.size(); ++i)
    {
        if (uri[i] == '%' && i + 2 < uri.size())
        {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') { return c - '0'; }
                if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
                if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
                return -1;
            };

            const int high = hex(uri[i + 1]);
            const int low  = hex(uri[i + 2]);

            if (high >= 0 && low >= 0)
            {
                result.push_back(static_cast<char>(high * 16 + low));
                i += 2;
                continue;
            }
        }
        result.push_back(uri[i]);
    }

    return result;
}


/// <summary>
/// `buffers` の実体を用意します。
/// </summary>
/// <param name="document">JSON を読み込み済みのドキュメント。</param>
/// <param name="baseDirectory">.gltf が置かれているフォルダ。</param>
/// <param name="glbBinary">GLB のバイナリチャンク。無ければ空。</param>
/// <remarks>
/// buffer の実体には 3 通りの置き場所があります。
/// GLB のバイナリチャンク、`data:` で始まる埋め込み、外部ファイルです。
/// </remarks>
void ResolveBuffers(Document& document,
                    const std::wstring& baseDirectory,
                    const std::vector<uint8_t>& glbBinary)
{
    const json::Value* buffers = document.root.Member("buffers");
    if (buffers == nullptr)
    {
        return;
    }

    for (size_t i = 0; i < buffers->Size(); ++i)
    {
        const json::Value& buffer = buffers->At(i);
        const json::Value* uri = buffer.Member("uri");

        if (uri == nullptr)
        {
            // uri が無い buffer は GLB のバイナリチャンクを指す（仕様上 0 番のみ）。
            document.buffers.push_back(glbBinary);
            continue;
        }

        const std::string& text = uri->AsString();

        // data URI : 中身が base64 で直接書かれている。
        constexpr const char* kDataPrefix = "data:";
        if (text.compare(0, 5, kDataPrefix) == 0)
        {
            const size_t comma = text.find(',');
            if (comma == std::string::npos)
            {
                throw std::runtime_error("glTF の data URI が壊れています。");
            }
            document.buffers.push_back(DecodeBase64(text.substr(comma + 1)));
            continue;
        }

        // 外部ファイル。.gltf からの相対パスとして開く。
        const std::string decoded = DecodePercentEscapes(text);
        std::wstring relative(decoded.begin(), decoded.end());

        for (wchar_t& c : relative)
        {
            if (c == L'/') { c = L'\\'; }
        }

        document.buffers.push_back(ReadAllBytes(baseDirectory + relative));
    }
}


/// <summary>アクセサの `type` 文字列から、要素あたりの成分数を求めます。</summary>
int ComponentCountOf(const std::string& type)
{
    if (type == "SCALAR") { return 1; }
    if (type == "VEC2")   { return 2; }
    if (type == "VEC3")   { return 3; }
    if (type == "VEC4")   { return 4; }
    if (type == "MAT4")   { return 16; }
    throw std::runtime_error("glTF に未対応のアクセサ型があります。");
}


/// <summary>componentType から 1 成分のバイト数を求めます。</summary>
int ComponentSizeOf(int componentType)
{
    switch (componentType)
    {
    case kByte:
    case kUnsignedByte:  return 1;
    case kShort:
    case kUnsignedShort: return 2;
    case kUnsignedInt:
    case kFloat:         return 4;
    default:
        throw std::runtime_error("glTF に未対応の componentType があります。");
    }
}


/// <summary>
/// アクセサ 1 つを float の配列として読み出します。
/// </summary>
/// <param name="document">読み込み済みのドキュメント。</param>
/// <param name="accessorIndex">アクセサの番号。</param>
/// <param name="componentCount">1 要素あたりの成分数を受け取ります。</param>
/// <returns>要素数 × 成分数 の float 配列。</returns>
/// <remarks>
/// アクセサは「どのバッファのどこから、どんな型で、いくつ読むか」の指示書です。
/// 実データはこう辿ります: accessor → bufferView → buffer。
/// </remarks>
std::vector<float> ReadAccessorAsFloats(const Document& document,
                                        int accessorIndex,
                                        int& componentCount)
{
    const json::Value* accessors = document.root.Member("accessors");
    if (accessors == nullptr || accessorIndex < 0)
    {
        throw std::runtime_error("glTF のアクセサが見つかりません。");
    }

    const json::Value& accessor = accessors->At(static_cast<size_t>(accessorIndex));

    const int count         = accessor.Member("count")
                                ? accessor.Member("count")->AsInt(0) : 0;
    const int componentType = accessor.Member("componentType")
                                ? accessor.Member("componentType")->AsInt(kFloat) : kFloat;
    const bool normalized   = accessor.Member("normalized")
                                && accessor.Member("normalized")->boolean;

    componentCount = ComponentCountOf(
        accessor.Member("type") ? accessor.Member("type")->AsString() : std::string("SCALAR"));

    std::vector<float> values(static_cast<size_t>(count) * componentCount, 0.0f);

    const json::Value* bufferViewIndex = accessor.Member("bufferView");
    if (bufferViewIndex == nullptr)
    {
        // bufferView が無いアクセサは全て 0 という意味（疎なアクセサの土台）。
        return values;
    }

    const json::Value* bufferViews = document.root.Member("bufferViews");
    const json::Value& bufferView =
        bufferViews->At(static_cast<size_t>(bufferViewIndex->AsInt(0)));

    const int bufferIndex = bufferView.Member("buffer")
                              ? bufferView.Member("buffer")->AsInt(0) : 0;

    if (static_cast<size_t>(bufferIndex) >= document.buffers.size())
    {
        throw std::runtime_error("glTF の buffer 番号が範囲外です。");
    }

    const std::vector<uint8_t>& bytes = document.buffers[static_cast<size_t>(bufferIndex)];

    const size_t viewOffset = bufferView.Member("byteOffset")
                                ? static_cast<size_t>(bufferView.Member("byteOffset")->AsInt(0))
                                : 0;
    const size_t accessorOffset = accessor.Member("byteOffset")
                                ? static_cast<size_t>(accessor.Member("byteOffset")->AsInt(0))
                                : 0;

    const int componentSize = ComponentSizeOf(componentType);
    const size_t tightStride = static_cast<size_t>(componentSize) * componentCount;

    // byteStride があれば、要素が飛び飛びに並んでいる（インターリーブ）。
    const size_t stride = bufferView.Member("byteStride")
                            ? static_cast<size_t>(bufferView.Member("byteStride")->AsInt(0))
                            : tightStride;

    const size_t base = viewOffset + accessorOffset;

    for (int element = 0; element < count; ++element)
    {
        const size_t elementStart = base + static_cast<size_t>(element) * stride;

        for (int component = 0; component < componentCount; ++component)
        {
            const size_t at = elementStart + static_cast<size_t>(component) * componentSize;
            if (at + componentSize > bytes.size())
            {
                throw std::runtime_error("glTF のアクセサがバッファの外を指しています。");
            }

            const uint8_t* source = bytes.data() + at;
            float value = 0.0f;

            switch (componentType)
            {
            case kFloat:
            {
                float raw = 0.0f;
                std::memcpy(&raw, source, sizeof(raw));
                value = raw;
                break;
            }
            case kUnsignedByte:
            {
                const uint8_t raw = *source;
                value = normalized ? raw / 255.0f : static_cast<float>(raw);
                break;
            }
            case kByte:
            {
                int8_t raw = 0;
                std::memcpy(&raw, source, sizeof(raw));
                value = normalized ? std::max(raw / 127.0f, -1.0f) : static_cast<float>(raw);
                break;
            }
            case kUnsignedShort:
            {
                uint16_t raw = 0;
                std::memcpy(&raw, source, sizeof(raw));
                value = normalized ? raw / 65535.0f : static_cast<float>(raw);
                break;
            }
            case kShort:
            {
                int16_t raw = 0;
                std::memcpy(&raw, source, sizeof(raw));
                value = normalized ? std::max(raw / 32767.0f, -1.0f) : static_cast<float>(raw);
                break;
            }
            case kUnsignedInt:
            {
                uint32_t raw = 0;
                std::memcpy(&raw, source, sizeof(raw));
                value = static_cast<float>(raw);
                break;
            }
            default:
                throw std::runtime_error("glTF に未対応の componentType があります。");
            }

            values[static_cast<size_t>(element) * componentCount + component] = value;
        }
    }

    return values;
}


/// <summary>
/// `images` の 1 枚を展開します。
/// </summary>
/// <param name="document">読み込み済みのドキュメント。</param>
/// <param name="imageIndex">画像の番号。</param>
/// <param name="baseDirectory">.gltf が置かれているフォルダ。</param>
/// <returns>RGBA8 に展開した画像。読めなければ空。</returns>
/// <remarks>
/// 画像の置き場所は buffers と同じく 3 通りあります。
/// `bufferView`（ファイル内に埋め込み）、`data:` URI、外部ファイルです。
/// </remarks>
assets::ImageData LoadImageAt(const Document& document,
                              int imageIndex,
                              const std::wstring& baseDirectory)
{
    const json::Value* images = document.root.Member("images");
    if (images == nullptr || imageIndex < 0)
    {
        return {};
    }

    const json::Value& image = images->At(static_cast<size_t>(imageIndex));

    // (a) ファイル内に埋め込まれている場合。
    if (const json::Value* viewIndex = image.Member("bufferView"))
    {
        const json::Value* bufferViews = document.root.Member("bufferViews");
        if (bufferViews == nullptr)
        {
            return {};
        }

        const json::Value& view = bufferViews->At(static_cast<size_t>(viewIndex->AsInt(0)));

        const int bufferIndex = view.Member("buffer") ? view.Member("buffer")->AsInt(0) : 0;
        if (static_cast<size_t>(bufferIndex) >= document.buffers.size())
        {
            return {};
        }

        const std::vector<uint8_t>& bytes = document.buffers[static_cast<size_t>(bufferIndex)];

        const size_t offset = view.Member("byteOffset")
                                ? static_cast<size_t>(view.Member("byteOffset")->AsInt(0)) : 0;
        const size_t length = view.Member("byteLength")
                                ? static_cast<size_t>(view.Member("byteLength")->AsInt(0)) : 0;

        if (offset + length > bytes.size())
        {
            return {};
        }

        // PNG / JPEG のバイト列がそのまま入っている。デコードは WIC に任せる。
        return assets::DecodeImageBytes(bytes.data() + offset, length);
    }

    const json::Value* uri = image.Member("uri");
    if (uri == nullptr)
    {
        return {};
    }

    const std::string& text = uri->AsString();

    // (b) data URI に base64 で書かれている場合。
    if (text.compare(0, 5, "data:") == 0)
    {
        const size_t comma = text.find(',');
        if (comma == std::string::npos)
        {
            return {};
        }

        const std::vector<uint8_t> decoded = DecodeBase64(text.substr(comma + 1));
        return assets::DecodeImageBytes(decoded.data(), decoded.size());
    }

    // (c) 外部ファイル。
    const std::string decoded = DecodePercentEscapes(text);
    std::wstring relative(decoded.begin(), decoded.end());

    for (wchar_t& c : relative)
    {
        if (c == L'/') { c = L'\\'; }
    }

    return assets::LoadImageFile(baseDirectory + relative);
}


/// <summary>
/// `materials` を読み込みます。
/// </summary>
/// <param name="document">読み込み済みのドキュメント。</param>
/// <param name="baseDirectory">.gltf が置かれているフォルダ。</param>
/// <returns>材質の一覧。</returns>
/// <remarks>
/// 読むのは PBR の基本色（`baseColorFactor` と `baseColorTexture`）だけです。
/// 金属度・粗さ・法線マップなどは読み飛ばします。
/// </remarks>
std::vector<MaterialData> LoadMaterials(const Document& document,
                                        const std::wstring& baseDirectory)
{
    std::vector<MaterialData> result;

    const json::Value* materials = document.root.Member("materials");
    if (materials == nullptr)
    {
        return result;
    }

    const json::Value* textures = document.root.Member("textures");

    for (size_t i = 0; i < materials->Size(); ++i)
    {
        const json::Value& source = materials->At(i);

        MaterialData material;
        if (const json::Value* name = source.Member("name"))
        {
            material.name = name->AsString();
        }

        const json::Value* pbr = source.Member("pbrMetallicRoughness");
        if (pbr != nullptr)
        {
            if (const json::Value* factor = pbr->Member("baseColorFactor"))
            {
                for (size_t c = 0; c < 4 && c < factor->Size(); ++c)
                {
                    material.baseColorFactor[c] =
                        static_cast<float>(factor->At(c).AsNumber(1.0));
                }
            }

            // ★ glTF の既定値はどちらも 1.0。
            //   つまり「何も書かなければ、粗い金属」という扱いになる。
            //   書き出し側は必ず指定するので実害は無いが、規格どおりに合わせておく。
            material.metallicFactor = static_cast<float>(
                pbr->Member("metallicFactor")
                    ? pbr->Member("metallicFactor")->AsNumber(1.0) : 1.0);

            material.roughnessFactor = static_cast<float>(
                pbr->Member("roughnessFactor")
                    ? pbr->Member("roughnessFactor")->AsNumber(1.0) : 1.0);

            // baseColorTexture は textures を経由して images に辿り着く。
            if (const json::Value* baseColorTexture = pbr->Member("baseColorTexture"))
            {
                const int textureIndex = baseColorTexture->Member("index")
                                           ? baseColorTexture->Member("index")->AsInt(-1) : -1;

                if (textures != nullptr && textureIndex >= 0)
                {
                    const json::Value& texture =
                        textures->At(static_cast<size_t>(textureIndex));

                    const int imageIndex = texture.Member("source")
                                             ? texture.Member("source")->AsInt(-1) : -1;

                    try
                    {
                        material.baseColorTexture =
                            LoadImageAt(document, imageIndex, baseDirectory);
                    }
                    catch (const std::exception&)
                    {
                        // 画像が読めなくても、基本色だけで描き続けられるようにする。
                        LogError(L"glTF の画像を展開できませんでした。基本色のみ使います。");
                    }
                }
            }

            // 金属らしさと粗さのテクスチャ。緑が粗さ、青が金属らしさ。
            if (const json::Value* mrTexture = pbr->Member("metallicRoughnessTexture"))
            {
                const int textureIndex = mrTexture->Member("index")
                                           ? mrTexture->Member("index")->AsInt(-1) : -1;

                if (textures != nullptr && textureIndex >= 0)
                {
                    const json::Value& texture =
                        textures->At(static_cast<size_t>(textureIndex));

                    const int imageIndex = texture.Member("source")
                                             ? texture.Member("source")->AsInt(-1) : -1;

                    try
                    {
                        material.metallicRoughnessTexture =
                            LoadImageAt(document, imageIndex, baseDirectory);
                    }
                    catch (const std::exception&)
                    {
                        LogError(L"glTF の金属らしさ・粗さの画像を展開できませんでした。");
                    }
                }
            }
        }

        // ★ normalTexture は pbrMetallicRoughness の「外側」にある。
        //   基本色や金属度と並びで書かれていると思って探すと見つからない。
        if (const json::Value* normalTexture = source.Member("normalTexture"))
        {
            const int textureIndex = normalTexture->Member("index")
                                       ? normalTexture->Member("index")->AsInt(-1) : -1;

            // scale は法線マップの効き具合。省略時は 1.0。
            if (const json::Value* scale = normalTexture->Member("scale"))
            {
                material.normalScale = static_cast<float>(scale->AsNumber(1.0));
            }

            if (textures != nullptr && textureIndex >= 0)
            {
                const json::Value& texture =
                    textures->At(static_cast<size_t>(textureIndex));

                const int imageIndex = texture.Member("source")
                                         ? texture.Member("source")->AsInt(-1) : -1;

                try
                {
                    material.normalTexture =
                        LoadImageAt(document, imageIndex, baseDirectory);
                }
                catch (const std::exception&)
                {
                    LogError(L"glTF の法線マップを展開できませんでした。");
                }
            }
        }

        result.push_back(std::move(material));
    }

    return result;
}


/// <summary>ノードのローカル変換行列を求めます。</summary>
/// <remarks>
/// `matrix` が直接書かれている場合と、`translation` / `rotation` / `scale` に
/// 分けて書かれている場合があります。両方に対応します。
/// </remarks>
XMMATRIX LocalTransformOf(const json::Value& node)
{
    if (const json::Value* matrix = node.Member("matrix"))
    {
        // glTF の行列は列優先で 16 個並んでいる。
        float m[16] = {};
        for (size_t i = 0; i < 16 && i < matrix->Size(); ++i)
        {
            m[i] = static_cast<float>(matrix->At(i).AsNumber());
        }

        // 列優先の並びを、行優先の XMMATRIX に置き換える（＝転置して読む）。
        return XMMatrixSet(m[0],  m[1],  m[2],  m[3],
                           m[4],  m[5],  m[6],  m[7],
                           m[8],  m[9],  m[10], m[11],
                           m[12], m[13], m[14], m[15]);
    }

    XMVECTOR scale       = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    XMVECTOR rotation    = XMQuaternionIdentity();
    XMVECTOR translation = XMVectorZero();

    if (const json::Value* value = node.Member("scale"))
    {
        scale = XMVectorSet(static_cast<float>(value->At(0).AsNumber(1.0)),
                            static_cast<float>(value->At(1).AsNumber(1.0)),
                            static_cast<float>(value->At(2).AsNumber(1.0)), 0.0f);
    }

    if (const json::Value* value = node.Member("rotation"))
    {
        // glTF のクォータニオンは (x, y, z, w) の順。DirectXMath も同じ順序。
        rotation = XMVectorSet(static_cast<float>(value->At(0).AsNumber()),
                               static_cast<float>(value->At(1).AsNumber()),
                               static_cast<float>(value->At(2).AsNumber()),
                               static_cast<float>(value->At(3).AsNumber(1.0)));
    }

    if (const json::Value* value = node.Member("translation"))
    {
        translation = XMVectorSet(static_cast<float>(value->At(0).AsNumber()),
                                  static_cast<float>(value->At(1).AsNumber()),
                                  static_cast<float>(value->At(2).AsNumber()), 0.0f);
    }

    // glTF は「スケール → 回転 → 平行移動」の順に適用すると定めている。
    return XMMatrixScalingFromVector(scale)
         * XMMatrixRotationQuaternion(rotation)
         * XMMatrixTranslationFromVector(translation);
}
} // namespace


/// <summary>
/// glTF 2.0 のモデルを読み込みます。
/// </summary>
MeshData LoadGltf(const std::wstring& filePath, const ModelLoadOptions& options)
{
    const std::vector<uint8_t> fileBytes = ReadAllBytes(filePath);

    std::string jsonText;
    std::vector<uint8_t> glbBinary;
    bool isBinaryContainer = false;

    // 先頭 4 バイトが "glTF" なら GLB（バイナリ 1 ファイル形式）。
    if (fileBytes.size() >= 12)
    {
        uint32_t magic = 0;
        std::memcpy(&magic, fileBytes.data(), sizeof(magic));
        isBinaryContainer = (magic == kGlbMagic);
    }

    if (isBinaryContainer)
    {
        // ヘッダ 12 バイトの後ろに、チャンクが並ぶ。
        //   [長さ 4][種類 4][中身…] の繰り返し。
        size_t offset = 12;
        while (offset + 8 <= fileBytes.size())
        {
            uint32_t chunkLength = 0;
            uint32_t chunkType   = 0;
            std::memcpy(&chunkLength, fileBytes.data() + offset, 4);
            std::memcpy(&chunkType,   fileBytes.data() + offset + 4, 4);
            offset += 8;

            if (offset + chunkLength > fileBytes.size())
            {
                throw std::runtime_error("GLB のチャンク長がファイルサイズを超えています。");
            }

            if (chunkType == kGlbChunkJson)
            {
                jsonText.assign(reinterpret_cast<const char*>(fileBytes.data() + offset),
                                chunkLength);
            }
            else if (chunkType == kGlbChunkBinary)
            {
                glbBinary.assign(fileBytes.data() + offset,
                                 fileBytes.data() + offset + chunkLength);
            }

            // チャンクは 4 バイト境界に揃えられている。
            offset += (chunkLength + 3) & ~3u;
        }

        if (jsonText.empty())
        {
            throw std::runtime_error("GLB に JSON チャンクがありません。");
        }
    }
    else
    {
        jsonText.assign(reinterpret_cast<const char*>(fileBytes.data()), fileBytes.size());
    }

    Document document;
    document.root = json::Parse(jsonText);

    ResolveBuffers(document, DirectoryOf(filePath), glbBinary);

    const json::Value* meshes = document.root.Member("meshes");
    const json::Value* nodes  = document.root.Member("nodes");

    if (meshes == nullptr || meshes->Size() == 0)
    {
        throw std::runtime_error("glTF にメッシュが含まれていません。");
    }

    MeshData mesh;
    mesh.materials = LoadMaterials(document, DirectoryOf(filePath));

    // 材質を持たないファイルでも描けるよう、白 1 色の材質を末尾に足しておく。
    const uint32_t defaultMaterialIndex = static_cast<uint32_t>(mesh.materials.size());
    {
        MaterialData fallback;
        fallback.name = "default";
        mesh.materials.push_back(std::move(fallback));
    }

    uint32_t primitiveCount = 0;

    // ノードを辿って、メッシュを持つノードだけ取り出す再帰処理。
    //   ノードは入れ子にでき、変換は親から子へ積み重なる。
    const std::function<void(int, const XMMATRIX&)> visitNode =
        [&](int nodeIndex, const XMMATRIX& parentTransform)
    {
        if (nodes == nullptr || nodeIndex < 0)
        {
            return;
        }

        const json::Value& node = nodes->At(static_cast<size_t>(nodeIndex));
        const XMMATRIX transform = LocalTransformOf(node) * parentTransform;

        if (const json::Value* meshIndex = node.Member("mesh"))
        {
            const json::Value& meshNode = meshes->At(static_cast<size_t>(meshIndex->AsInt(0)));
            const json::Value* primitives = meshNode.Member("primitives");

            // 法線は「向き」なので、位置と同じ行列では正しく変換できない。
            //   拡大縮小が軸ごとに違う場合に備え、逆行列の転置を使う。
            XMMATRIX normalTransform = XMMatrixTranspose(XMMatrixInverse(nullptr, transform));

            for (size_t p = 0; primitives != nullptr && p < primitives->Size(); ++p)
            {
                const json::Value& primitive = primitives->At(p);

                // mode が無ければ 4（三角形）。それ以外は扱わない。
                const int mode = primitive.Member("mode")
                                   ? primitive.Member("mode")->AsInt(4) : 4;
                if (mode != 4)
                {
                    continue;
                }

                const json::Value* attributes = primitive.Member("attributes");
                if (attributes == nullptr)
                {
                    continue;
                }

                const json::Value* positionAccessor = attributes->Member("POSITION");
                if (positionAccessor == nullptr)
                {
                    continue;   // POSITION は必須。無ければ描けない
                }

                int positionComponents = 0;
                const std::vector<float> positions =
                    ReadAccessorAsFloats(document, positionAccessor->AsInt(-1),
                                         positionComponents);

                const size_t vertexCount =
                    positions.size() / static_cast<size_t>(positionComponents);

                std::vector<float> normals;
                int normalComponents = 0;
                if (const json::Value* accessor = attributes->Member("NORMAL"))
                {
                    normals = ReadAccessorAsFloats(document, accessor->AsInt(-1),
                                                   normalComponents);
                }

                std::vector<float> texCoords;
                int texCoordComponents = 0;
                if (const json::Value* accessor = attributes->Member("TEXCOORD_0"))
                {
                    texCoords = ReadAccessorAsFloats(document, accessor->AsInt(-1),
                                                     texCoordComponents);
                }

                // 接線。glTF では vec4 で、w が従接線の符号（+1 か -1）。
                //   持っていないファイルも多いので、その場合は後から計算する。
                std::vector<float> tangents;
                int tangentComponents = 0;
                if (const json::Value* accessor = attributes->Member("TANGENT"))
                {
                    tangents = ReadAccessorAsFloats(document, accessor->AsInt(-1),
                                                    tangentComponents);
                }

                const uint16_t vertexBase = static_cast<uint16_t>(mesh.vertices.size());
                const uint32_t indexBase  = static_cast<uint32_t>(mesh.indices.size());

                if (mesh.vertices.size() + vertexCount > UINT16_MAX)
                {
                    throw std::runtime_error(
                        "頂点が多すぎます。インデックスを 32bit にしてください。");
                }

                for (size_t v = 0; v < vertexCount; ++v)
                {
                    Vertex vertex = {};

                    XMVECTOR position = XMVectorSet(
                        positions[v * positionComponents + 0],
                        positions[v * positionComponents + 1],
                        positionComponents > 2 ? positions[v * positionComponents + 2] : 0.0f,
                        1.0f);

                    position = XMVector3TransformCoord(position, transform);

                    // ★ 右手座標系 → 左手座標系。OBJ と同じく Z の符号を反転する。
                    //   glTF は +Y が上、-Z が前方の右手系。
                    vertex.position[0] =  XMVectorGetX(position);
                    vertex.position[1] =  XMVectorGetY(position);
                    vertex.position[2] = -XMVectorGetZ(position);

                    if (!normals.empty() && normalComponents >= 3)
                    {
                        XMVECTOR normal = XMVectorSet(
                            normals[v * normalComponents + 0],
                            normals[v * normalComponents + 1],
                            normals[v * normalComponents + 2],
                            0.0f);

                        normal = XMVector3Normalize(
                            XMVector3TransformNormal(normal, normalTransform));

                        vertex.normal[0] =  XMVectorGetX(normal);
                        vertex.normal[1] =  XMVectorGetY(normal);
                        vertex.normal[2] = -XMVectorGetZ(normal);
                    }
                    else
                    {
                        vertex.normal[1] = 1.0f;
                    }

                    if (!texCoords.empty() && texCoordComponents >= 2)
                    {
                        // ★ glTF の UV は左上が原点で V が下向き。DirectX と同じなので
                        //   OBJ のような反転は不要。
                        vertex.uv[0] = texCoords[v * texCoordComponents + 0];
                        vertex.uv[1] = texCoords[v * texCoordComponents + 1];
                    }

                    if (!tangents.empty() && tangentComponents >= 4)
                    {
                        XMVECTOR tangent = XMVectorSet(
                            tangents[v * tangentComponents + 0],
                            tangents[v * tangentComponents + 1],
                            tangents[v * tangentComponents + 2],
                            0.0f);

                        tangent = XMVector3Normalize(
                            XMVector3TransformNormal(tangent, normalTransform));

                        vertex.tangent[0] =  XMVectorGetX(tangent);
                        vertex.tangent[1] =  XMVectorGetY(tangent);
                        vertex.tangent[2] = -XMVectorGetZ(tangent);

                        // ★ 右手系から左手系へ移すと、従接線の向きも裏返る。
                        //   Z を反転したぶん、符号も反転させないと法線マップが凹凸逆になる。
                        vertex.tangent[3] = -tangents[v * tangentComponents + 3];
                    }

                    // 色は材質が持つので、頂点カラーは白にしておく。
                    //   掛け合わせても変わらないので、材質の色がそのまま出る。
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        vertex.color[channel] = 1.0f;
                    }

                    mesh.vertices.push_back(vertex);
                }

                // インデックスが無い場合は、頂点が並んだ順に三角形を作る決まり。
                if (const json::Value* indexAccessor = primitive.Member("indices"))
                {
                    int indexComponents = 0;
                    const std::vector<float> indices =
                        ReadAccessorAsFloats(document, indexAccessor->AsInt(-1),
                                             indexComponents);

                    for (const float index : indices)
                    {
                        mesh.indices.push_back(
                            static_cast<uint16_t>(vertexBase + static_cast<uint32_t>(index)));
                    }
                }
                else
                {
                    for (size_t v = 0; v < vertexCount; ++v)
                    {
                        mesh.indices.push_back(static_cast<uint16_t>(vertexBase + v));
                    }
                }

                // プリミティブ 1 個 = サブメッシュ 1 個。材質はここで決まる。
                SubMesh subMesh;
                subMesh.indexOffset = indexBase;
                subMesh.indexCount  = static_cast<uint32_t>(mesh.indices.size()) - indexBase;

                const json::Value* materialIndex = primitive.Member("material");
                subMesh.materialIndex =
                    (materialIndex != nullptr && materialIndex->AsInt(-1) >= 0)
                        ? static_cast<uint32_t>(materialIndex->AsInt(0))
                        : defaultMaterialIndex;

                mesh.subMeshes.push_back(subMesh);

                ++primitiveCount;
            }
        }

        if (const json::Value* children = node.Member("children"))
        {
            for (size_t i = 0; i < children->Size(); ++i)
            {
                visitNode(children->At(i).AsInt(-1), transform);
            }
        }
    };

    // scene / scenes があればそこから、無ければ全ノードを辿る。
    const json::Value* scenes = document.root.Member("scenes");
    const int sceneIndex = document.root.Member("scene")
                             ? document.root.Member("scene")->AsInt(0) : 0;

    if (scenes != nullptr && scenes->Size() > 0)
    {
        const json::Value& scene = scenes->At(static_cast<size_t>(sceneIndex));
        if (const json::Value* roots = scene.Member("nodes"))
        {
            for (size_t i = 0; i < roots->Size(); ++i)
            {
                visitNode(roots->At(i).AsInt(-1), XMMatrixIdentity());
            }
        }
    }
    else if (nodes != nullptr)
    {
        for (size_t i = 0; i < nodes->Size(); ++i)
        {
            visitNode(static_cast<int>(i), XMMatrixIdentity());
        }
    }

    if (mesh.vertices.empty() || mesh.indices.empty())
    {
        throw std::runtime_error("glTF から三角形を取り出せませんでした。");
    }

    if (options.fitToTargetSize)
    {
        FitToTargetSize(mesh, options.targetSize, options.groundLevel);
    }

    Log(std::format(
        L"glTF を読み込みました（{}形式 / プリミティブ {} 個 / 頂点 {} 個 / 三角形 {} 枚 "
        L"/ 材質 {} 個）",
        isBinaryContainer ? L"GLB バイナリ" : L"JSON テキスト",
        primitiveCount,
        mesh.vertices.size(),
        mesh.indices.size() / 3,
        mesh.materials.size()));

    return mesh;
}

} // namespace dx12::assets
