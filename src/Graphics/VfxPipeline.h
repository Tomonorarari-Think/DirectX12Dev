//=============================================================================
// VfxPipeline.h
//   半透明のビルボードを描くためのパイプライン。
//
//   アルファ合成と加算合成を、同じシェーダーのまま PSO の差し替えで切り替える。
//   詳しい解説は docs/tutorial/27_半透明とブレンディング.md を参照。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"
#include "ConstantBuffer.h"

#include <DirectXMath.h>
#include <vector>

namespace dx12
{

/// <summary>
/// 合成の仕方。
/// </summary>
enum class BlendMode
{
    /// <summary>アルファ合成。下の色を「隠す」。煙・ガラス向け。</summary>
    Alpha,

    /// <summary>加算合成。下の色に「足す」。光・炎向け。暗くならない。</summary>
    Additive,
};


/// <summary>
/// 半透明の板 1 枚ぶんの情報。
/// </summary>
struct VfxParticle
{
    /// <summary>xyz = ワールド座標、w = 大きさ（半径）。</summary>
    DirectX::XMFLOAT4 positionSize;

    /// <summary>rgb = 色（リニア）、a = 不透明度。</summary>
    DirectX::XMFLOAT4 color;

    /// <summary>x = やわらかさ、y = 回転（ラジアン）。z, w は未使用。</summary>
    DirectX::XMFLOAT4 params;
};


/// <summary>
/// 半透明のビルボードを描くパイプライン。
/// </summary>
class VfxPipeline
{
public:
    /// <summary>1 回の描画で扱える板の上限。</summary>
    /// <remarks>
    /// 定数バッファの大きさの上限（64 KB）に収まる範囲で決めています。
    /// これを超える数を扱うなら、構造化バッファか頂点バッファへ移します。
    /// </remarks>
    static constexpr uint32_t kMaxParticles = 64;

    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    VfxPipeline() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~VfxPipeline() = default;

    /// <summary>コピー構築は禁止です。</summary>
    VfxPipeline(const VfxPipeline&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    VfxPipeline& operator=(const VfxPipeline&) = delete;

    /// <summary>
    /// ルートシグネチャ・PSO（2 種類）・定数バッファを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="renderTargetFormat">書き込み先の形式。</param>
    /// <param name="depthStencilFormat">深度バッファの形式。</param>
    /// <param name="frameCount">同時に処理するフレーム数。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void Initialize(ID3D12Device* device,
                    DXGI_FORMAT renderTargetFormat,
                    DXGI_FORMAT depthStencilFormat,
                    uint32_t frameCount);

    /// <summary>
    /// このフレームぶんの板を書き込みます。
    /// </summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    /// <param name="slot">合成の仕方ごとのスロット（0 か 1）。</param>
    /// <param name="viewProjection">ビュー行列 × 射影行列。</param>
    /// <param name="cameraRight">カメラの右方向（ワールド空間）。</param>
    /// <param name="cameraUp">カメラの上方向（ワールド空間）。</param>
    /// <param name="particles">描く板。`kMaxParticles` まで。</param>
    void Update(uint32_t frameIndex,
                uint32_t slot,
                const DirectX::XMMATRIX& viewProjection,
                const DirectX::XMFLOAT3& cameraRight,
                const DirectX::XMFLOAT3& cameraUp,
                const std::vector<VfxParticle>& particles);

    /// <summary>
    /// 板を描く命令を記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">使う定数のフレーム番号。</param>
    /// <param name="slot">`Update` で使ったスロット。</param>
    /// <param name="mode">合成の仕方。</param>
    /// <param name="count">描く板の数。</param>
    /// <remarks>
    /// **不透明な物をすべて描いたあとに呼びます。**
    /// 半透明は深度を書かないので、先に描くと後ろの物に上書きされます。
    /// </remarks>
    void Record(ID3D12GraphicsCommandList* commandList,
                uint32_t frameIndex,
                uint32_t slot,
                BlendMode mode,
                uint32_t count) const;

    /// <summary>1 フレームで使うスロット数。</summary>
    static constexpr uint32_t kSlotCount = 2;

private:
    /// <summary>ルートシグネチャ。</summary>
    ComPtr<ID3D12RootSignature> m_rootSignature;

    /// <summary>アルファ合成用の PSO。</summary>
    ComPtr<ID3D12PipelineState> m_alphaPipelineState;

    /// <summary>加算合成用の PSO。</summary>
    ComPtr<ID3D12PipelineState> m_additivePipelineState;

    /// <summary>フレーム別・スロット別の定数。</summary>
    ConstantBuffer m_constantBuffer;
};

} // namespace dx12
