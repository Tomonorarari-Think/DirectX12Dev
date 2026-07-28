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

// DirectXMath : Windows SDK に同梱される数学ライブラリ。
#include <DirectXMath.h>

#include <vector>

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

    /// <summary>光源から見たビュー行列 × 射影行列。影の判定に使います。</summary>
    /// <remarks>
    /// シャドウマップを描くときは変換行列として、画面を描くときは
    /// 「この点が影の中かどうか」を調べる座標変換として、同じ行列を 2 度使います。
    /// </remarks>
    DirectX::XMFLOAT4X4 lightViewProjection;

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
    /// <param name="shadowMapFormat">シャドウマップの深度形式。</param>
    /// <param name="commandQueue">
    /// テクスチャ転送に使うキュー。転送の完了まで待機します。
    /// </param>
    /// <exception cref="HrException">いずれかの生成に失敗した場合。</exception>
    /// <exception cref="std::runtime_error">シェーダーファイルが見つからない場合。</exception>
    /// <remarks>
    /// テクスチャはこのクラスが持ちません。材質ごとに違うため `MaterialSet` が持ち、
    /// 描く直前に `BindMaterial` で差し替えます。
    /// </remarks>
    void Initialize(ID3D12Device* device,
                    DXGI_FORMAT renderTargetFormat,
                    DXGI_FORMAT depthStencilFormat,
                    uint32_t frameCount,
                    uint32_t maxObjectCount,
                    DXGI_FORMAT shadowMapFormat);

    /// <summary>
    /// 平行光源が進む向きを返します。
    /// </summary>
    /// <returns>正規化していない向きベクトル。</returns>
    /// <remarks>
    /// シャドウマップの視点を決めるのに `Renderer` が必要とするため公開しています。
    /// ライトの設定をここ 1 か所に保つのが目的です。
    /// </remarks>
    static DirectX::XMFLOAT3 LightDirection();

    /// <summary>
    /// このフレームの共通定数（カメラとライト）を書き込みます。
    /// </summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    /// <param name="viewProjection">カメラのビュー行列 × 射影行列。</param>
    /// <param name="cameraPosition">視点のワールド座標。</param>
    /// <param name="lightViewProjection">光源から見たビュー行列 × 射影行列。</param>
    void UpdateFrameConstants(uint32_t frameIndex,
                              const DirectX::XMMATRIX& viewProjection,
                              const DirectX::XMFLOAT3& cameraPosition,
                              const DirectX::XMMATRIX& lightViewProjection);

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
    /// <param name="shadowMapView">シャドウマップの SRV。</param>
    /// <remarks>
    /// テクスチャを結び付けるため、呼び出し側が先に `SetDescriptorHeaps` で
    /// シェーダー可視ヒープを設定しておく必要があります。
    /// </remarks>
    void Bind(ID3D12GraphicsCommandList* commandList,
              uint32_t frameIndex,
              D3D12_GPU_DESCRIPTOR_HANDLE shadowMapView) const;

    /// <summary>
    /// シャドウマップを描くための共通設定を記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">使用するフレーム番号。</param>
    /// <remarks>
    /// 影の形しか要らないので、ピクセルシェーダーもテクスチャも使いません。
    /// この後は `Bind` のときと同じく `BindObject` を挟んでメッシュを描きます。
    /// </remarks>
    void BindShadowPass(ID3D12GraphicsCommandList* commandList, uint32_t frameIndex) const;

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

    /// <summary>
    /// これから描くサブメッシュの材質を結び付けます。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="constantAddress">材質の定数バッファの GPU アドレス。</param>
    /// <param name="textureView">材質の基本色テクスチャの SRV。</param>
    /// <remarks>
    /// 材質が変わるたびに呼びます。差し替えるのは定数バッファ 1 本と
    /// ディスクリプタテーブル 1 つだけなので、PSO の切り替えより遥かに軽い処理です。
    /// </remarks>
    void BindMaterial(ID3D12GraphicsCommandList* commandList,
                      D3D12_GPU_VIRTUAL_ADDRESS constantAddress,
                      D3D12_GPU_DESCRIPTOR_HANDLE textureView) const;

private:
    /// <summary>
    /// ルートシグネチャを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <exception cref="HrException">シリアライズまたは生成に失敗した場合。</exception>
    void CreateRootSignature(ID3D12Device* device);

    /// <summary>
    /// シャドウマップ描画用の、より狭いルートシグネチャを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <exception cref="HrException">シリアライズまたは生成に失敗した場合。</exception>
    void CreateShadowRootSignature(ID3D12Device* device);

    /// <summary>
    /// シャドウマップ描画用の PSO（深度だけを書く設定）を生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="shadowMapFormat">シャドウマップの深度形式。</param>
    /// <param name="inputElements">入力レイアウト。画面描画と共通のものを使います。</param>
    /// <param name="inputElementCount">入力レイアウトの要素数。</param>
    /// <exception cref="HrException">コンパイルまたは PSO 生成に失敗した場合。</exception>
    void CreateShadowPipelineState(ID3D12Device* device,
                                   DXGI_FORMAT shadowMapFormat,
                                   const D3D12_INPUT_ELEMENT_DESC* inputElements,
                                   uint32_t inputElementCount);

    /// <summary>
    /// HLSL をコンパイルし、パイプラインステートオブジェクトを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="renderTargetFormat">描画先の形式。</param>
    /// <param name="depthStencilFormat">深度バッファの形式。</param>
    /// <param name="shadowMapFormat">
    /// シャドウマップの深度形式。入力レイアウトを共有するため、
    /// 影用の PSO もこの中で作ります。
    /// </param>
    /// <exception cref="HrException">コンパイルまたは PSO 生成に失敗した場合。</exception>
    void CreatePipelineState(ID3D12Device* device,
                             DXGI_FORMAT renderTargetFormat,
                             DXGI_FORMAT depthStencilFormat,
                             DXGI_FORMAT shadowMapFormat);

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
    /// シャドウマップ描画用のルートシグネチャ（ピクセルシェーダーを拒否する設定）。
    /// </summary>
    ComPtr<ID3D12RootSignature> m_shadowRootSignature;

    /// <summary>
    /// シャドウマップ描画用の PSO（深度だけを書く）。
    /// </summary>
    ComPtr<ID3D12PipelineState> m_shadowPipelineState;

    /// <summary>
    /// カメラとライトを渡す定数バッファ（フレーム数ぶんのスロット）。
    /// </summary>
    ConstantBuffer m_frameConstantBuffer;

    /// <summary>
    /// 変換行列を渡す定数バッファ（フレーム数 × オブジェクト数ぶんのスロット）。
    /// </summary>
    ConstantBuffer m_objectConstantBuffer;

    /// <summary>1 フレームで描けるオブジェクトの上限。</summary>
    uint32_t m_maxObjectCount = 0;
};

} // namespace dx12
