//=============================================================================
// GpuParticleSystem.h
//   パーティクルを GPU 上だけで動かす仕組み。
//
//   位置も速度も CPU へ戻さない。コンピュートシェーダーが更新した
//   構造化バッファを、頂点シェーダーがそのまま読んで描く。
//   詳しい解説は docs/tutorial/29_GPUパーティクル.md を参照。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"
#include "ConstantBuffer.h"

#include <DirectXMath.h>

namespace dx12
{

class DescriptorHeap;

/// <summary>
/// GPU 上のパーティクル 1 個ぶんの並び。`GpuParticle.hlsli` と一致させること。
/// </summary>
struct GpuParticle
{
    /// <summary>xyz = 位置（ワールド）、w = 大きさ（半径）。</summary>
    DirectX::XMFLOAT4 positionSize;

    /// <summary>xyz = 速度（毎秒）、w = 残り寿命（秒）。</summary>
    DirectX::XMFLOAT4 velocityLife;

    /// <summary>rgb = 色（リニア）、a = 最初の寿命（秒）。</summary>
    DirectX::XMFLOAT4 colorLifetime;
};


/// <summary>
/// パーティクルの更新と描画を GPU 上で完結させるクラス。
/// </summary>
class GpuParticleSystem
{
public:
    /// <summary>確保するパーティクルの最大数。</summary>
    /// <remarks>
    /// 定数バッファ（64 KB）と違い、構造化バッファに実用上の上限はありません。
    /// 1 個 48 バイトなので、この数でも 12 MB ほどです。
    /// どこで遅くなるかを測るために、わざと多めに取ってあります。
    /// </remarks>
    static constexpr uint32_t kMaxParticles = 262144;

    /// <summary>コンピュートシェーダーの 1 グループあたりのスレッド数。</summary>
    /// <remarks>`ParticleUpdate.hlsl` の `THREAD_GROUP_SIZE` と一致させること。</remarks>
    static constexpr uint32_t kThreadGroupSize = 64;

    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    GpuParticleSystem() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~GpuParticleSystem() = default;

    /// <summary>コピー構築は禁止です。</summary>
    GpuParticleSystem(const GpuParticleSystem&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    GpuParticleSystem& operator=(const GpuParticleSystem&) = delete;

    /// <summary>
    /// 構造化バッファ・UAV / SRV・2 つの PSO・定数バッファを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="descriptorHeap">UAV と SRV を登録するシェーダー可視ヒープ。</param>
    /// <param name="renderTargetFormat">書き込み先の形式。</param>
    /// <param name="depthStencilFormat">深度バッファの形式。</param>
    /// <param name="frameCount">同時に処理するフレーム数。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void Initialize(ID3D12Device* device,
                    DescriptorHeap& descriptorHeap,
                    DXGI_FORMAT renderTargetFormat,
                    DXGI_FORMAT depthStencilFormat,
                    uint32_t frameCount);

    /// <summary>
    /// このフレームぶんの定数を書き込みます。
    /// </summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    /// <param name="deltaTime">前フレームからの経過秒。</param>
    /// <param name="time">開始からの経過秒。乱数の種に使います。</param>
    /// <param name="emitter">湧き出し口の位置。</param>
    /// <param name="viewProjection">ビュー行列 × 射影行列。</param>
    /// <param name="cameraRight">カメラの右方向（ワールド空間）。</param>
    /// <param name="cameraUp">カメラの上方向（ワールド空間）。</param>
    /// <param name="projection">射影行列。深度を距離へ戻すのに使います。</param>
    /// <param name="softFadeDistance">
    /// 後ろの物からこの距離まで近づくと消えます。0 以下でソフト化を切ります。
    /// </param>
    /// <param name="count">動かすパーティクルの数。</param>
    void Update(uint32_t frameIndex,
                float deltaTime,
                float time,
                const DirectX::XMFLOAT3& emitter,
                const DirectX::XMMATRIX& viewProjection,
                const DirectX::XMFLOAT3& cameraRight,
                const DirectX::XMFLOAT3& cameraUp,
                const DirectX::XMMATRIX& projection,
                float softFadeDistance,
                uint32_t count);

    /// <summary>
    /// パーティクルを 1 フレームぶん進める命令を記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">使う定数のフレーム番号。</param>
    /// <param name="count">動かすパーティクルの数。</param>
    /// <remarks>
    /// **描画より前に呼びます。** 呼び終えた時点で、バッファは
    /// 頂点シェーダーから読める状態になっています。
    /// </remarks>
    void RecordUpdate(ID3D12GraphicsCommandList* commandList,
                      uint32_t frameIndex,
                      uint32_t count);

    /// <summary>
    /// パーティクルを描く命令を記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">使う定数のフレーム番号。</param>
    /// <param name="sceneDepth">深度バッファの SRV。</param>
    /// <param name="count">描くパーティクルの数。</param>
    /// <remarks>
    /// 呼び終えると、バッファは次の更新に備えて `UNORDERED_ACCESS` へ戻ります。
    /// </remarks>
    void RecordDraw(ID3D12GraphicsCommandList* commandList,
                    uint32_t frameIndex,
                    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth,
                    uint32_t count);

private:
    /// <summary>更新（コンピュート）用のルートシグネチャ。</summary>
    ComPtr<ID3D12RootSignature> m_computeRootSignature;

    /// <summary>更新用の PSO。</summary>
    ComPtr<ID3D12PipelineState> m_computePipelineState;

    /// <summary>描画用のルートシグネチャ。</summary>
    ComPtr<ID3D12RootSignature> m_graphicsRootSignature;

    /// <summary>描画用の PSO。</summary>
    ComPtr<ID3D12PipelineState> m_graphicsPipelineState;

    /// <summary>パーティクルを置く構造化バッファ。</summary>
    ComPtr<ID3D12Resource> m_particleBuffer;

    /// <summary>書き込み用（UAV）の GPU ハンドル。</summary>
    D3D12_GPU_DESCRIPTOR_HANDLE m_unorderedAccessView = {};

    /// <summary>読み出し用（SRV）の GPU ハンドル。</summary>
    D3D12_GPU_DESCRIPTOR_HANDLE m_shaderResourceView = {};

    /// <summary>更新用の定数。</summary>
    ConstantBuffer m_updateConstants;

    /// <summary>描画用の定数。</summary>
    ConstantBuffer m_drawConstants;
};

} // namespace dx12
