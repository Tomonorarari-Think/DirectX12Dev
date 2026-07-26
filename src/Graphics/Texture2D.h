//=============================================================================
// Texture2D.h
//   2D テクスチャの生成、GPU への転送、SRV の作成。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

#include <cstdint>
#include <vector>

namespace dx12
{
class CommandQueue;
class DescriptorHeap;

/// @brief 2D テクスチャを GPU 上に用意し、シェーダーから読めるようにするクラス。
///
/// **これまでのバッファとの決定的な違い：DEFAULT ヒープを使う**
///
/// 頂点バッファ・定数バッファは UPLOAD ヒープに直接置いていました。
/// CPU から書けるので手軽ですが、GPU からのアクセスは DEFAULT ヒープより遅くなります。
///
/// テクスチャは容量が大きく、毎フレーム GPU が何万回も読むため、
/// GPU 専用の高速メモリ（DEFAULT ヒープ）に置くのが基本です。
/// しかし DEFAULT ヒープは **CPU から直接書き込めません**。
/// そこで次の手順を踏みます。
///
/// 1. DEFAULT ヒープにテクスチャ本体を作る（CPU からは書けない）
/// 2. UPLOAD ヒープに一時的な「中継バッファ」を作る（CPU から書ける）
/// 3. CPU から中継バッファへピクセルを書く
/// 4. **GPU に「中継バッファ → テクスチャ」のコピーを実行させる**
/// 5. コピー完了を待ってから、中継バッファを捨てる
///
/// この「ステージング（中継）」は DirectX 12 で最も頻出するパターンのひとつです。
/// 頂点バッファを DEFAULT ヒープに置く場合もまったく同じ手順になります。
///
/// **行ピッチ（RowPitch）の落とし穴**
///
/// テクスチャを中継バッファに置くとき、1 行のバイト数は
/// 256 バイト境界に揃っている必要があります
/// （`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`）。
///
/// 例えば横 100 ピクセル・RGBA 8bit なら 1 行 400 バイトですが、
/// 中継バッファ上では 512 バイトぶんの場所を取り、
/// 余った 112 バイトは詰め物になります。
/// そのため `memcpy` は画像全体を一度に行えず、**1 行ずつコピー**します。
///
/// 必要なサイズや詰め物の量は自分で計算せず、
/// `ID3D12Device::GetCopyableFootprints()` に問い合わせるのが正解です。
class Texture2D
{
public:
    /// @brief テクスチャのピクセル形式。
    ///
    /// バックバッファと同じ RGBA 各 8bit。
    /// `_UNORM` は 0〜255 の整数をシェーダー側で 0.0〜1.0 として扱う指定です。
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    /// @brief 1 ピクセルあたりのバイト数（RGBA 各 1 バイト）。
    static constexpr uint32_t kBytesPerPixel = 4;

    /// @brief 既定のコンストラクタ。まだ何も生成されません。
    Texture2D() = default;

    /// @brief デストラクタ。ComPtr により COM オブジェクトが自動解放されます。
    ~Texture2D() = default;

    /// @brief コピー構築は禁止です。
    Texture2D(const Texture2D&) = delete;

    /// @brief コピー代入は禁止です。
    Texture2D& operator=(const Texture2D&) = delete;

    /// @brief ピクセルデータから GPU 上にテクスチャを作り、SRV を登録します。
    /// @param device 生成に使う D3D12 デバイス。
    /// @param commandQueue 転送コマンドを実行するキュー。完了まで待機します。
    /// @param descriptorHeap SRV を登録するシェーダー可視ヒープ。
    /// @param width テクスチャの幅（ピクセル）。
    /// @param height テクスチャの高さ（ピクセル）。
    /// @param pixels RGBA 順に並んだピクセル列。要素数は width × height × 4。
    /// @exception HrException リソースの生成または転送に失敗した場合。
    ///
    /// @note この関数は内部で GPU の完了を待ちます（同期処理）。
    /// 起動時に一度だけ呼ぶことを想定しており、毎フレーム呼ぶものではありません。
    void Initialize(ID3D12Device* device,
                    CommandQueue& commandQueue,
                    DescriptorHeap& descriptorHeap,
                    uint32_t width,
                    uint32_t height,
                    const std::vector<uint8_t>& pixels);

    /// @brief シェーダーへ渡す GPU ディスクリプタハンドルを取得します。
    /// @returns `SetGraphicsRootDescriptorTable` に渡すハンドル。
    D3D12_GPU_DESCRIPTOR_HANDLE ShaderResourceView() const noexcept { return m_srvGpuHandle; }

    /// @brief テクスチャの幅を取得します。
    /// @returns 幅（ピクセル）。
    uint32_t Width() const noexcept { return m_width; }

    /// @brief テクスチャの高さを取得します。
    /// @returns 高さ（ピクセル）。
    uint32_t Height() const noexcept { return m_height; }

private:
    /// @brief DEFAULT ヒープ上のテクスチャ本体。
    ///
    /// CPU からは書けません。GPU がコピー命令で書き込みます。
    ComPtr<ID3D12Resource> m_texture;

    /// @brief SRV の GPU ハンドル。
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpuHandle = {};

    /// @brief 幅（ピクセル）。
    uint32_t m_width = 0;

    /// @brief 高さ（ピクセル）。
    uint32_t m_height = 0;
};


/// @brief 市松模様（チェッカーボード）のピクセル列を生成します。
/// @param width 生成する幅（ピクセル）。
/// @param height 生成する高さ（ピクセル）。
/// @param cellSize 1 マスの大きさ（ピクセル）。
/// @returns RGBA 順に並んだピクセル列（要素数は width × height × 4）。
///
/// 画像ファイルを読み込むには、対応形式ごとのデコーダ（PNG / JPEG …）が必要で、
/// それだけで学習の主題から外れてしまいます。
/// テクスチャの「貼られ方」を確認するのが目的なので、
/// 模様をコードで作って使います。市松模様は UV のずれや上下反転に気付きやすく、
/// 動作確認用のテクスチャとして定番です。
std::vector<uint8_t> CreateCheckerboardPixels(uint32_t width, uint32_t height, uint32_t cellSize);

} // namespace dx12
