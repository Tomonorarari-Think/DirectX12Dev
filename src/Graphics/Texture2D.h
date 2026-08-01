//=============================================================================
// Texture2D.h
//   2D テクスチャの生成、GPU への転送、SRV の作成。
//=============================================================================
#pragma once

#include "../Assets/ImageLoader.h"
#include "../Common/GraphicsCommon.h"

#include <cstdint>
#include <vector>

namespace dx12
{
class CommandQueue;
class DescriptorHeap;

/// <summary>
/// 2D テクスチャを GPU 上に用意し、シェーダーから読めるようにするクラス。
/// </summary>
class Texture2D
{
public:
    /// <summary>
    /// テクスチャのピクセル形式。
    /// </summary>
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    /// <summary>
    /// 色として扱う画像のピクセル形式。
    /// </summary>
    /// <remarks>
    /// PNG や JPEG の色は sRGB で記録されています。この形式で読むと、
    /// GPU がシェーダーへ渡す前にリニアへ戻してくれます。
    /// 法線マップや粗さのように「色ではない」画像には使ってはいけません。
    /// </remarks>
    static constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    /// <summary>
    /// 1 ピクセルあたりのバイト数（RGBA 各 1 バイト）。
    /// </summary>
    static constexpr uint32_t kBytesPerPixel = 4;

    /// <summary>
    /// 既定のコンストラクタ。まだ何も生成されません。
    /// </summary>
    Texture2D() = default;

    /// <summary>
    /// デストラクタ。ComPtr により COM オブジェクトが自動解放されます。
    /// </summary>
    ~Texture2D() = default;

    /// <summary>
    /// コピー構築は禁止です。
    /// </summary>
    Texture2D(const Texture2D&) = delete;

    /// <summary>
    /// コピー代入は禁止です。
    /// </summary>
    Texture2D& operator=(const Texture2D&) = delete;

    /// <summary>
    /// ピクセルデータから GPU 上にテクスチャを作り、SRV を登録します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="commandQueue">転送コマンドを実行するキュー。完了まで待機します。</param>
    /// <param name="descriptorHeap">SRV を登録するシェーダー可視ヒープ。</param>
    /// <param name="width">テクスチャの幅（ピクセル）。</param>
    /// <param name="height">テクスチャの高さ（ピクセル）。</param>
    /// <param name="pixels">RGBA 順に並んだピクセル列。要素数は width × height × 4。</param>
    /// <param name="isColorTexture">
    /// 見た目の色を表す画像なら `true`。sRGB として読み、リニアへ戻します。
    /// 法線マップなど数値として使う画像は `false` にしてください。
    /// </param>
    /// <exception cref="HrException">リソースの生成または転送に失敗した場合。</exception>
    /// <remarks>
    /// この関数は内部で GPU の完了を待ちます（同期処理）。 起動時に一度だけ呼ぶことを想定してお
    /// り、毎フレーム呼ぶものではありません。
    /// </remarks>
    void Initialize(ID3D12Device* device,
                    CommandQueue& commandQueue,
                    DescriptorHeap& descriptorHeap,
                    uint32_t width,
                    uint32_t height,
                    const std::vector<uint8_t>& pixels,
                    bool isColorTexture = true);

    /// <summary>
    /// 縮小版を何段も持つテクスチャ（ミップ列）を生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="commandQueue">転送コマンドを実行するキュー。完了まで待機します。</param>
    /// <param name="descriptorHeap">SRV を登録するシェーダー可視ヒープ。</param>
    /// <param name="mipChain">鮮明な順に並んだ画像。先頭が原寸です。</param>
    /// <param name="isColorTexture">見た目の色なら `true`。</param>
    /// <exception cref="HrException">生成または転送に失敗した場合。</exception>
    /// <remarks>
    /// 段ごとに転送先が分かれます（サブリソース）。1 本の中継バッファに
    /// 全段を詰め、`CopyTextureRegion` を段の数だけ呼びます。
    /// </remarks>
    void Initialize(ID3D12Device* device,
                    CommandQueue& commandQueue,
                    DescriptorHeap& descriptorHeap,
                    const std::vector<assets::ImageData>& mipChain,
                    bool isColorTexture = true);

    /// <summary>
    /// シェーダーへ渡す GPU ディスクリプタハンドルを取得します。
    /// </summary>
    /// <returns>`SetGraphicsRootDescriptorTable` に渡すハンドル。</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE ShaderResourceView() const noexcept { return m_srvGpuHandle; }

    /// <summary>
    /// シェーダー可視ヒープの中で、この SRV が何番目かを返します。
    /// </summary>
    /// <returns>0 から始まる番号。</returns>
    /// <remarks>
    /// **ビンドレスで使う番号です。** ディスクリプタテーブルを結び付ける
    /// 代わりに、この番号を定数として渡し、シェーダーが
    /// `ResourceDescriptorHeap[番号]` で直接引きます
    /// （[33 章](../../docs/tutorial/33_ビンドレス.md)）。
    /// </remarks>
    uint32_t DescriptorIndex() const noexcept { return m_descriptorIndex; }

    /// <summary>
    /// テクスチャの幅を取得します。
    /// </summary>
    /// <returns>幅（ピクセル）。</returns>
    uint32_t Width() const noexcept { return m_width; }

    /// <summary>
    /// テクスチャの高さを取得します。
    /// </summary>
    /// <returns>高さ（ピクセル）。</returns>
    uint32_t Height() const noexcept { return m_height; }

private:
    /// <summary>
    /// DEFAULT ヒープ上のテクスチャ本体。
    /// </summary>
    ComPtr<ID3D12Resource> m_texture;

    /// <summary>
    /// SRV の GPU ハンドル。
    /// </summary>
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpuHandle = {};

    /// <summary>
    /// 幅（ピクセル）。
    /// </summary>
    /// <summary>シェーダー可視ヒープの中での SRV の番号。</summary>
    uint32_t m_descriptorIndex = 0;

    uint32_t m_width = 0;

    /// <summary>
    /// 高さ（ピクセル）。
    /// </summary>
    uint32_t m_height = 0;
};


/// <summary>
/// 市松模様（チェッカーボード）のピクセル列を生成します。
/// </summary>
/// <param name="width">生成する幅（ピクセル）。</param>
/// <param name="height">生成する高さ（ピクセル）。</param>
/// <param name="cellSize">1 マスの大きさ（ピクセル）。</param>
/// <returns>RGBA 順に並んだピクセル列（要素数は width × height × 4）。</returns>
std::vector<uint8_t> CreateCheckerboardPixels(uint32_t width, uint32_t height,
                                              uint32_t cellSize);

} // namespace dx12
