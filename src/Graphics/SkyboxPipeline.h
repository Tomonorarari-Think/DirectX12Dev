//=============================================================================
// SkyboxPipeline.h
//   背景に環境マップを描くためのルートシグネチャ・PSO・定数バッファ。
//
//   頂点バッファもインデックスバッファも持たない。頂点 ID から
//   画面いっぱいの三角形を組み立てるため。
//   詳しい解説は docs/tutorial/23_スカイボックス.md を参照。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"
#include "../App/Camera.h"
#include "ConstantBuffer.h"

#include <DirectXMath.h>

namespace dx12
{

/// <summary>
/// 背景を描くときにシェーダーへ渡す定数。
/// </summary>
/// <remarks>
/// 16 バイト単位で区切られるため `XMFLOAT4` に揃えています。
/// </remarks>
struct SkyboxConstants
{
    /// <summary>カメラの右方向（ワールド空間、正規化済み）。w は未使用。</summary>
    DirectX::XMFLOAT4 cameraRight;

    /// <summary>カメラの上方向（ワールド空間、正規化済み）。w は未使用。</summary>
    DirectX::XMFLOAT4 cameraUp;

    /// <summary>カメラの前方向（ワールド空間、正規化済み）。w は未使用。</summary>
    DirectX::XMFLOAT4 cameraForward;

    /// <summary>
    /// x = 画角の半分の正接、y = 縦横比、z = 環境光の強さ。w は未使用。
    /// </summary>
    DirectX::XMFLOAT4 params;
};


/// <summary>
/// 背景に環境マップを描くパイプライン。
/// </summary>
class SkyboxPipeline
{
public:
    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    SkyboxPipeline() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~SkyboxPipeline() = default;

    /// <summary>コピー構築は禁止です。</summary>
    SkyboxPipeline(const SkyboxPipeline&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    SkyboxPipeline& operator=(const SkyboxPipeline&) = delete;

    /// <summary>
    /// ルートシグネチャ・PSO・定数バッファを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="frameCount">同時に処理するフレーム数。定数のスロット数になります。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void Initialize(ID3D12Device* device, uint32_t frameCount);

    /// <summary>
    /// このフレームぶんの定数を書き込みます。
    /// </summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    /// <param name="camera">向きと画角を読み取るカメラ。位置は使いません。</param>
    /// <param name="ambientIntensity">環境光の強さ。物体側と同じ値を渡します。</param>
    /// <remarks>
    /// 背景は「無限に遠い」ものとして描くので、**視点の位置は使いません**。
    /// 使うと、カメラを動かしたときに背景が流れてしまいます。
    /// </remarks>
    void Update(uint32_t frameIndex,
                const Camera& camera,
                float ambientIntensity);

    /// <summary>
    /// 背景を描く命令を記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">使う定数のフレーム番号。</param>
    /// <param name="environmentView">環境マップの SRV。</param>
    /// <remarks>
    /// **物体を描いたあとに呼びます。** 背景は最も奥に置かれるので、
    /// 先に物体を描いておけば、隠れる部分のピクセルシェーダーが動きません。
    /// </remarks>
    void Record(ID3D12GraphicsCommandList* commandList,
                uint32_t frameIndex,
                D3D12_GPU_DESCRIPTOR_HANDLE environmentView) const;

private:
    /// <summary>ルートシグネチャ。</summary>
    ComPtr<ID3D12RootSignature> m_rootSignature;

    /// <summary>パイプラインステート。</summary>
    ComPtr<ID3D12PipelineState> m_pipelineState;

    /// <summary>フレーム別の定数。</summary>
    ConstantBuffer m_constantBuffer;
};

} // namespace dx12
