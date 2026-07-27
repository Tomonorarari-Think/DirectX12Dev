//=============================================================================
// MeshPipeline.h
//   メッシュを描くための「描き方」一式（ルートシグネチャ・PSO・定数バッファ）。
//
//   「何を描くか」は Mesh が持つ。このクラスは形状を知らないので、
//   同じ設定のまま何個でもメッシュを描ける。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"
#include "ConstantBuffer.h"
#include "Texture2D.h"

// DirectXMath : Windows SDK に同梱される数学ライブラリ。
#include <DirectXMath.h>

namespace dx12
{
class CommandQueue;
class DescriptorHeap;

/// <summary>
/// 1 フレームのあいだ、描くもの全てで共通の定数。
/// </summary>
/// <remarks>
/// HLSL の定数バッファは 16 バイト単位で区切られます。`float3` の直後に `float` を
/// 置くと同じ 16 バイトに詰め込まれ、C++ 側とずれます。ここで全て `XMFLOAT4` /
/// `XMFLOAT4X4` に揃えているのは、そのずれを構造的に起こさないためです。
/// </remarks>
struct FrameConstants
{
    /// <summary>ビュー行列 × 射影行列。カメラが決まれば全オブジェクトで共通。</summary>
    DirectX::XMFLOAT4X4 viewProjection;

    /// <summary>平行光源の進む向き (xyz)。正規化済み。w は未使用。</summary>
    /// <remarks>
    /// 「光が飛んでいく向き」であり「光源の方向」ではありません。符号を取り違えると
    /// 明暗が裏返ります。
    /// </remarks>
    DirectX::XMFLOAT4 lightDirection;

    /// <summary>光の色と強さ (rgb)。w は環境光の強さ。</summary>
    DirectX::XMFLOAT4 lightColor;

    /// <summary>視点のワールド座標 (xyz)。鏡面反射の計算に使います。w は未使用。</summary>
    DirectX::XMFLOAT4 cameraPosition;
};


/// <summary>
/// オブジェクト 1 個ごとに変わる定数。
/// </summary>
struct ObjectConstants
{
    /// <summary>ワールド × ビュー × 射影。CPU 側で合成済み。</summary>
    DirectX::XMFLOAT4X4 worldViewProjection;

    /// <summary>ワールド行列。法線と頂点をワールド空間へ移すために使います。</summary>
    DirectX::XMFLOAT4X4 world;
};


/// <summary>
/// メッシュを描くための描画設定一式を持つクラス。
/// </summary>
class MeshPipeline
{
public:
    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    MeshPipeline() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~MeshPipeline() = default;

    /// <summary>コピー構築は禁止です。</summary>
    MeshPipeline(const MeshPipeline&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    MeshPipeline& operator=(const MeshPipeline&) = delete;

    /// <summary>
    /// ルートシグネチャ・PSO・定数バッファ・テクスチャを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="renderTargetFormat">
    /// 描画先の形式。PSO はこれを知っている必要があります。
    /// </param>
    /// <param name="depthStencilFormat">
    /// 深度バッファの形式。`DepthBuffer::kFormat` と一致させること。
    /// </param>
    /// <param name="frameCount">
    /// 定数バッファに用意するフレーム数（通常はバックバッファの枚数）。
    /// </param>
    /// <param name="maxObjectCount">1 フレームで描くオブジェクトの上限。</param>
    /// <param name="commandQueue">
    /// テクスチャ転送に使うキュー。転送の完了まで待機します。
    /// </param>
    /// <param name="descriptorHeap">テクスチャの SRV を登録するシェーダー可視ヒープ。</param>
    /// <exception cref="HrException">いずれかの生成に失敗した場合。</exception>
    /// <exception cref="std::runtime_error">シェーダーファイルが見つからない場合。</exception>
    void Initialize(ID3D12Device* device,
                    DXGI_FORMAT renderTargetFormat,
                    DXGI_FORMAT depthStencilFormat,
                    uint32_t frameCount,
                    uint32_t maxObjectCount,
                    CommandQueue& commandQueue,
                    DescriptorHeap& descriptorHeap);

    /// <summary>
    /// このフレームの共通定数（カメラとライト）を書き込みます。
    /// </summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    /// <param name="viewProjection">カメラのビュー行列 × 射影行列。</param>
    /// <param name="cameraPosition">視点のワールド座標。</param>
    void UpdateFrameConstants(uint32_t frameIndex,
                              const DirectX::XMMATRIX& viewProjection,
                              const DirectX::XMFLOAT3& cameraPosition);

    /// <summary>
    /// オブジェクト 1 個ぶんの定数（変換行列）を書き込みます。
    /// </summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    /// <param name="objectIndex">オブジェクトの番号。0 から始まる連番。</param>
    /// <param name="world">そのオブジェクトのワールド行列。</param>
    /// <param name="viewProjection">カメラのビュー行列 × 射影行列。</param>
    /// <exception cref="std::out_of_range">`maxObjectCount` を超えた場合。</exception>
    void UpdateObjectConstants(uint32_t frameIndex,
                               uint32_t objectIndex,
                               const DirectX::XMMATRIX& world,
                               const DirectX::XMMATRIX& viewProjection);

    /// <summary>
    /// 描画の共通設定（PSO・ルートシグネチャ・フレーム定数・テクスチャ）を記録します。
    /// </summary>
    /// <param name="commandList">記録先の（Reset 済みで開いている）コマンドリスト。</param>
    /// <param name="frameIndex">使用するフレーム番号。</param>
    /// <remarks>
    /// テクスチャを結び付けるため、呼び出し側が先に `SetDescriptorHeaps` で
    /// シェーダー可視ヒープを設定しておく必要があります。
    /// </remarks>
    void Bind(ID3D12GraphicsCommandList* commandList, uint32_t frameIndex) const;

    /// <summary>
    /// これから描くオブジェクトの定数を結び付けます。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">使用するフレーム番号。</param>
    /// <param name="objectIndex">オブジェクトの番号。</param>
    /// <remarks>`Bind` を呼んだ後、メッシュを描く直前に呼びます。</remarks>
    void BindObject(ID3D12GraphicsCommandList* commandList,
                    uint32_t frameIndex,
                    uint32_t objectIndex) const;

private:
    /// <summary>
    /// ルートシグネチャを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <exception cref="HrException">シリアライズまたは生成に失敗した場合。</exception>
    void CreateRootSignature(ID3D12Device* device);

    /// <summary>
    /// HLSL をコンパイルし、パイプラインステートオブジェクトを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="renderTargetFormat">描画先の形式。</param>
    /// <param name="depthStencilFormat">深度バッファの形式。</param>
    /// <exception cref="HrException">コンパイルまたは PSO 生成に失敗した場合。</exception>
    void CreatePipelineState(ID3D12Device* device,
                             DXGI_FORMAT renderTargetFormat,
                             DXGI_FORMAT depthStencilFormat);

    /// <summary>
    /// フレーム番号とオブジェクト番号から、定数バッファのスロット番号を求めます。
    /// </summary>
    /// <param name="frameIndex">フレーム番号。</param>
    /// <param name="objectIndex">オブジェクト番号。</param>
    /// <returns>スロット番号。</returns>
    uint32_t ObjectSlot(uint32_t frameIndex, uint32_t objectIndex) const
    {
        return frameIndex * m_maxObjectCount + objectIndex;
    }

    /// <summary>
    /// HLSL ファイルをコンパイルして、GPU 用のバイトコードを得ます。
    /// </summary>
    /// <param name="filePath">.hlsl ファイルの絶対パス。</param>
    /// <param name="entryPoint">入口となる関数名（`"VSMain"` など）。</param>
    /// <param name="target">シェーダーモデル（`"vs_5_0"` など）。</param>
    /// <returns>コンパイル済みバイトコードを保持する Blob。</returns>
    /// <exception cref="HrException">コンパイルに失敗した場合。</exception>
    static ComPtr<ID3DBlob> CompileShader(const std::wstring& filePath,
                                          const char* entryPoint,
                                          const char* target);

private:
    /// <summary>
    /// シェーダーが受け取る外部入力の一覧表。
    /// </summary>
    ComPtr<ID3D12RootSignature> m_rootSignature;

    /// <summary>
    /// 描画設定を 1 つに固めたパイプラインステートオブジェクト。
    /// </summary>
    ComPtr<ID3D12PipelineState> m_pipelineState;

    /// <summary>
    /// カメラとライトを渡す定数バッファ（フレーム数ぶんのスロット）。
    /// </summary>
    ConstantBuffer m_frameConstantBuffer;

    /// <summary>
    /// 変換行列を渡す定数バッファ（フレーム数 × オブジェクト数ぶんのスロット）。
    /// </summary>
    ConstantBuffer m_objectConstantBuffer;

    /// <summary>
    /// メッシュに貼るテクスチャ（市松模様）。
    /// </summary>
    Texture2D m_texture;

    /// <summary>1 フレームで描けるオブジェクトの上限。</summary>
    uint32_t m_maxObjectCount = 0;
};

} // namespace dx12
