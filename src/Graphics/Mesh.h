//=============================================================================
// Mesh.h
//   1 つの形状ぶんの頂点バッファとインデックスバッファ。
//
//   「何を描くか」だけを持ち、「どう描くか」（PSO・ルートシグネチャ）は
//   MeshPipeline が持つ。この分離により、同じ設定で複数の形状を描ける。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"
#include "Geometry.h"

namespace dx12
{
class CommandQueue;

/// <summary>
/// GPU 上に置かれた 1 つの形状（頂点バッファ＋インデックスバッファ）。
/// </summary>
class Mesh
{
public:
    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    Mesh() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~Mesh() = default;

    /// <summary>コピー構築は禁止です。</summary>
    Mesh(const Mesh&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    Mesh& operator=(const Mesh&) = delete;

    /// <summary>
    /// 形状データを DEFAULT ヒープへ転送し、描画できる状態にします。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="commandQueue">転送コマンドを実行するキュー。完了まで待機します。</param>
    /// <param name="meshData">頂点とインデックスの配列。</param>
    /// <param name="debugName">ログに出す名前。</param>
    /// <exception cref="HrException">リソースの生成または転送に失敗した場合。</exception>
    void Initialize(ID3D12Device* device,
                    CommandQueue& commandQueue,
                    const MeshData& meshData,
                    const wchar_t* debugName);

    /// <summary>
    /// この形状を描く命令をコマンドリストに記録します。
    /// </summary>
    /// <param name="commandList">記録先の（Reset 済みで開いている）コマンドリスト。</param>
    /// <remarks>
    /// PSO・ルートシグネチャ・定数バッファは、呼び出し側が先に設定しておくこと。
    /// </remarks>
    void RecordDrawCommands(ID3D12GraphicsCommandList* commandList) const;

    /// <summary>描画するインデックスの個数を返します。</summary>
    /// <returns>インデックス数。</returns>
    uint32_t IndexCount() const { return m_indexCount; }

private:
    /// <summary>頂点データを置く GPU 上のメモリ領域（DEFAULT ヒープ）。</summary>
    ComPtr<ID3D12Resource> m_vertexBuffer;

    /// <summary>インデックスデータを置く GPU 上のメモリ領域（DEFAULT ヒープ）。</summary>
    ComPtr<ID3D12Resource> m_indexBuffer;

    /// <summary>頂点バッファの読み取り方を GPU に伝える構造体。</summary>
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};

    /// <summary>インデックスバッファの読み取り方を GPU に伝える構造体。</summary>
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};

    /// <summary>描画するインデックスの個数。</summary>
    uint32_t m_indexCount = 0;
};

} // namespace dx12
