//=============================================================================
// ConstantBuffer.h
//   シェーダーへ毎フレーム値を渡すための定数バッファ。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// @brief フレームごとに書き換えられる定数バッファ（Constant Buffer）を管理するクラス。
///
/// **定数バッファとは**
///
/// シェーダーに「頂点ごとではなく、描画全体で共通の値」を渡すための入れ物です。変換行列、時間、ライ
/// トの色や向き、カメラ位置などがこれにあたります。頂点バッファが「頂点ごとに違う値」を運ぶのに対し、
/// 定数バッファは「その描画の間ずっと同じ値」を運びます。
///
/// **ルール 1: サイズは 256 バイト単位に切り上げる**
///
/// DirectX 12 の仕様で、定数バッファの先頭アドレスは 256
/// バイト境界に揃っていなければなりません（`D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT`）。64
/// バイトの行列 1 個しか入れなくても、確保するのは 256 バイトになります。揃っていないとデバッグレイ
/// ヤーがエラーを出します。
///
/// **ルール 2: フレームごとに別の領域を使う（重要）**
///
/// CPU が定数バッファへ書き込んでいるとき、GPU がまだ前のフレームの描画で同じ場所を読んでいる可能性
/// があります。1 個の領域を使い回すと、描画途中で値が書き換わり、絵が壊れます。
///
/// フレームバッファリング（`Renderer` のコマンドアロケータと同じ考え方）でバックバッファの枚数ぶん
/// の領域を用意し、フレームごとに使い分けます。
///
/// ```
/// 1 本のバッファの中身（frameCount = 2 の場合）
/// +-------------------+-------------------+
/// | フレーム 0 用      | フレーム 1 用      |
/// | 256 バイト         | 256 バイト         |
/// +-------------------+-------------------+
///  ↑ GpuAddress(0)     ↑ GpuAddress(1)
/// ```
///
/// リソースを 2 個作らず 1 本にまとめているのは、「アドレス計算だけで切り替えられる」ぶん扱いが簡単
/// なためです。
///
/// **ルール 3: UPLOAD ヒープに置き、マップしっぱなしにする**
///
/// 毎フレーム CPU から書き換えるため、CPU から書ける UPLOAD ヒープを使います。また `Map` / `Unmap`
/// は毎回呼ぶ必要がありません。生成時に一度 `Map` して、破棄までポインタを持ち続ける（＝永続マップ）
/// のが定石です。毎フレームの `Map` は無駄なコストです。
class ConstantBuffer
{
public:
    /// @brief 定数バッファのアドレス境界（バイト）。
    ///
    /// DirectX 12 が要求する 256 バイト境界。`D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT` と同じ値
    /// です。
    static constexpr uint32_t kAlignment = 256;

    /// @brief 既定のコンストラクタ。まだ何も生成されません。
    ConstantBuffer() = default;

    /// @brief デストラクタ。マップを解除します。
    ~ConstantBuffer();

    /// @brief コピー構築は禁止です。
    ConstantBuffer(const ConstantBuffer&) = delete;

    /// @brief コピー代入は禁止です。
    ConstantBuffer& operator=(const ConstantBuffer&) = delete;

    /// @brief フレーム数ぶんの領域を持つ定数バッファを生成します。
    /// @param device 生成に使う D3D12 デバイス。
    /// @param sizeInBytes 1 フレームぶんのデータサイズ（切り上げ前）。
    /// @param frameCount 用意するフレーム数（通常はバックバッファの枚数）。
    /// @exception HrException リソースの生成またはマップに失敗した場合。
    void Initialize(ID3D12Device* device, uint32_t sizeInBytes, uint32_t frameCount);

    /// @brief 指定フレームの領域へデータを書き込みます。
    /// @param frameIndex 書き込み先のフレーム番号。
    /// @param data 書き込むデータの先頭アドレス。
    /// @param sizeInBytes 書き込むバイト数。生成時のサイズ以下である必要があります。
    ///
    /// 呼び出す前に、そのフレーム番号の GPU 処理が完了していることを確認してください（`Renderer::Render
    /// ` の先頭でフェンスを待っています）。
    void Update(uint32_t frameIndex, const void* data, uint32_t sizeInBytes);

    /// @brief 指定フレームの領域の GPU アドレスを取得します。
    /// @param frameIndex 取得するフレーム番号。
    /// @returns `SetGraphicsRootConstantBufferView` に渡す GPU 仮想アドレス。
    D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(uint32_t frameIndex) const;

    /// @brief サイズを 256 バイト境界へ切り上げます。
    /// @param sizeInBytes 元のサイズ（バイト）。
    /// @returns 256 の倍数に切り上げたサイズ。
    ///
    /// `(size + 255) & ~255` は「切り上げ」の定番の書き方です。255 を足してから下位 8 ビットを 0 にする
    /// ことで、割り算を使わずに 256 の倍数へ丸められます。
    static constexpr uint32_t Align(uint32_t sizeInBytes)
    {
        return (sizeInBytes + (kAlignment - 1)) & ~(kAlignment - 1);
    }

private:
    /// @brief 全フレームぶんをまとめて確保した UPLOAD ヒープ上のバッファ。
    ComPtr<ID3D12Resource> m_resource;

    /// @brief マップされた CPU 側の先頭アドレス（永続マップ）。
    uint8_t* m_mappedData = nullptr;

    /// @brief 1 フレームぶんの領域サイズ（256 バイト境界へ切り上げ済み）。
    uint32_t m_alignedSize = 0;

    /// @brief 確保したフレーム数。
    uint32_t m_frameCount = 0;
};

} // namespace dx12
