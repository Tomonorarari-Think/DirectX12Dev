//=============================================================================
// ConstantBuffer.h
//   シェーダーへ毎フレーム値を渡すための定数バッファ。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// <summary>
/// フレームごとに書き換えられる定数バッファ（Constant Buffer）を管理するクラス。
/// </summary>
/// <remarks>
/// <para>
/// <b>定数バッファとは</b><br/>
/// シェーダーに「頂点ごとではなく、描画全体で共通の値」を渡すための入れ物です。
/// 変換行列、時間、ライトの色や向き、カメラ位置などがこれにあたります。
/// 頂点バッファが「頂点ごとに違う値」を運ぶのに対し、
/// 定数バッファは「その描画の間ずっと同じ値」を運びます。
/// </para>
///
/// <para>
/// <b>ルール 1: サイズは 256 バイト単位に切り上げる</b><br/>
/// DirectX 12 の仕様で、定数バッファの先頭アドレスは 256 バイト境界に
/// 揃っていなければなりません（<c>D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT</c>）。
/// 64 バイトの行列 1 個しか入れなくても、確保するのは 256 バイトになります。
/// 揃っていないとデバッグレイヤーがエラーを出します。
/// </para>
///
/// <para>
/// <b>ルール 2: フレームごとに別の領域を使う（重要）</b><br/>
/// CPU が定数バッファへ書き込んでいるとき、GPU がまだ前のフレームの描画で
/// 同じ場所を読んでいる可能性があります。1 個の領域を使い回すと、
/// 描画途中で値が書き換わり、絵が壊れます。
/// </para>
/// <para>
/// フレームバッファリング（<c>Renderer</c> のコマンドアロケータと同じ考え方）で
/// バックバッファの枚数ぶんの領域を用意し、フレームごとに使い分けます。
/// <code>
/// 1 本のバッファの中身（frameCount = 2 の場合）
/// +-------------------+-------------------+
/// | フレーム 0 用      | フレーム 1 用      |
/// | 256 バイト         | 256 バイト         |
/// +-------------------+-------------------+
///  ↑ GpuAddress(0)     ↑ GpuAddress(1)
/// </code>
/// リソースを 2 個作らず 1 本にまとめているのは、
/// 「アドレス計算だけで切り替えられる」ぶん扱いが簡単なためです。
/// </para>
///
/// <para>
/// <b>ルール 3: UPLOAD ヒープに置き、マップしっぱなしにする</b><br/>
/// 毎フレーム CPU から書き換えるため、CPU から書ける UPLOAD ヒープを使います。
/// また <c>Map</c> / <c>Unmap</c> は毎回呼ぶ必要がありません。
/// 生成時に一度 <c>Map</c> して、破棄までポインタを持ち続ける
/// （＝永続マップ）のが定石です。毎フレームの <c>Map</c> は無駄なコストです。
/// </para>
/// </remarks>
class ConstantBuffer
{
public:
    /// <summary>定数バッファのアドレス境界（バイト）。</summary>
    /// <remarks>
    /// DirectX 12 が要求する 256 バイト境界。
    /// <c>D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT</c> と同じ値です。
    /// </remarks>
    static constexpr uint32_t kAlignment = 256;

    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    ConstantBuffer() = default;

    /// <summary>デストラクタ。マップを解除します。</summary>
    ~ConstantBuffer();

    /// <summary>コピー構築は禁止です。</summary>
    ConstantBuffer(const ConstantBuffer&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    ConstantBuffer& operator=(const ConstantBuffer&) = delete;

    /// <summary>フレーム数ぶんの領域を持つ定数バッファを生成します。</summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="sizeInBytes">1 フレームぶんのデータサイズ（切り上げ前）。</param>
    /// <param name="frameCount">用意するフレーム数（通常はバックバッファの枚数）。</param>
    /// <exception cref="HrException">リソースの生成またはマップに失敗した場合。</exception>
    void Initialize(ID3D12Device* device, uint32_t sizeInBytes, uint32_t frameCount);

    /// <summary>指定フレームの領域へデータを書き込みます。</summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    /// <param name="data">書き込むデータの先頭アドレス。</param>
    /// <param name="sizeInBytes">書き込むバイト数。生成時のサイズ以下である必要があります。</param>
    /// <remarks>
    /// 呼び出す前に、そのフレーム番号の GPU 処理が完了していることを
    /// 確認してください（<c>Renderer::Render</c> の先頭でフェンスを待っています）。
    /// </remarks>
    void Update(uint32_t frameIndex, const void* data, uint32_t sizeInBytes);

    /// <summary>指定フレームの領域の GPU アドレスを取得します。</summary>
    /// <param name="frameIndex">取得するフレーム番号。</param>
    /// <returns>
    /// <c>SetGraphicsRootConstantBufferView</c> に渡す GPU 仮想アドレス。
    /// </returns>
    D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(uint32_t frameIndex) const;

    /// <summary>サイズを 256 バイト境界へ切り上げます。</summary>
    /// <param name="sizeInBytes">元のサイズ（バイト）。</param>
    /// <returns>256 の倍数に切り上げたサイズ。</returns>
    /// <remarks>
    /// <c>(size + 255) &amp; ~255</c> は「切り上げ」の定番の書き方です。
    /// 255 を足してから下位 8 ビットを 0 にすることで、
    /// 割り算を使わずに 256 の倍数へ丸められます。
    /// </remarks>
    static constexpr uint32_t Align(uint32_t sizeInBytes)
    {
        return (sizeInBytes + (kAlignment - 1)) & ~(kAlignment - 1);
    }

private:
    /// <summary>全フレームぶんをまとめて確保した UPLOAD ヒープ上のバッファ。</summary>
    ComPtr<ID3D12Resource> m_resource;

    /// <summary>マップされた CPU 側の先頭アドレス（永続マップ）。</summary>
    uint8_t* m_mappedData = nullptr;

    /// <summary>1 フレームぶんの領域サイズ（256 バイト境界へ切り上げ済み）。</summary>
    uint32_t m_alignedSize = 0;

    /// <summary>確保したフレーム数。</summary>
    uint32_t m_frameCount = 0;
};

} // namespace dx12
