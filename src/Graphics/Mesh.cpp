//=============================================================================
// Mesh.cpp
//   Mesh の実装。
//=============================================================================
#include "Mesh.h"

#include "CommandQueue.h"
#include "UploadHelper.h"

#include <format>
#include <stdexcept>

namespace dx12
{

/// <summary>
/// 形状データを DEFAULT ヒープへ転送し、描画できる状態にします。
/// </summary>
void Mesh::Initialize(ID3D12Device* device,
                      CommandQueue& commandQueue,
                      const MeshData& meshData,
                      const wchar_t* debugName)
{
    if (meshData.vertices.empty() || meshData.indices.empty())
    {
        throw std::runtime_error("空の形状データは GPU へ載せられません。");
    }

    // インデックスは R16_UINT で読むため、頂点は 65536 個未満でなければならない。
    if (meshData.vertices.size() > UINT16_MAX)
    {
        throw std::runtime_error("頂点が多すぎます。インデックスを 32bit にしてください。");
    }

    const UINT vertexBufferSize =
        static_cast<UINT>(meshData.vertices.size() * sizeof(Vertex));
    const UINT indexBufferSize =
        static_cast<UINT>(meshData.indices.size() * sizeof(uint16_t));

    // どちらも一度書いたら変わらないので、GPU 専用の DEFAULT ヒープに置く。
    m_vertexBuffer = upload::CreateBufferWithData(
        device, commandQueue, meshData.vertices.data(), vertexBufferSize,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    m_indexBuffer = upload::CreateBufferWithData(
        device, commandQueue, meshData.indices.data(), indexBufferSize,
        D3D12_RESOURCE_STATE_INDEX_BUFFER);

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes  = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes    = vertexBufferSize;

    // インデックスは形式を指定する。頂点が 65536 個未満なら R16_UINT で足りる。
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format         = DXGI_FORMAT_R16_UINT;
    m_indexBufferView.SizeInBytes    = indexBufferSize;

    m_indexCount = static_cast<uint32_t>(meshData.indices.size());
    m_subMeshes  = meshData.subMeshes;

    Log(std::format(
        L"メッシュ「{}」を作成しました（頂点 {} 個 / インデックス {} 個 / サブメッシュ {} 個）",
        debugName, meshData.vertices.size(), m_indexCount, m_subMeshes.size()));
}


/// <summary>
/// この形状を描く命令をコマンドリストに記録します。
/// </summary>
void Mesh::RecordDrawCommands(ID3D12GraphicsCommandList* commandList) const
{
    // IA は Input Assembler（入力アセンブラ）の略で、
    // 頂点データを組み立ててシェーダーに送り込む GPU の最初の段のこと。
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

    // インデックスバッファは 1 本だけ設定する（頂点バッファのような複数スロットは無い）
    commandList->IASetIndexBuffer(&m_indexBufferView);

    // インデックスの順に頂点を引いて描く。
    commandList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
}


/// <summary>
/// この形状の一部（サブメッシュ）を描く命令を記録します。
/// </summary>
void Mesh::RecordDrawCommands(ID3D12GraphicsCommandList* commandList,
                              uint32_t indexOffset,
                              uint32_t indexCount) const
{
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList->IASetIndexBuffer(&m_indexBufferView);

    // 第 3 引数が「何番目のインデックスから始めるか」。
    //   頂点バッファは共有したまま、描く範囲だけを変えられる。
    commandList->DrawIndexedInstanced(indexCount, 1, indexOffset, 0, 0);
}

} // namespace dx12
