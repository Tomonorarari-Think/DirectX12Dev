//=============================================================================
// PostProcessPipeline.h
//   描き終えた絵を加工して画面へ出すための、一連のパス。
//
//   明るい所の抽出 → 横ぼかし → 縦ぼかし → 合成（露出・トーンマップ・ビネット）
//   詳しい解説は docs/tutorial/25_ポストプロセス.md を参照。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"
#include "ConstantBuffer.h"
#include "RenderTexture.h"

#include <DirectXMath.h>

namespace dx12
{
class DescriptorHeap;

/// <summary>
/// 合成のときにシェーダーへ渡す定数。
/// </summary>
struct PostProcessConstants
{
    /// <summary>x = 露出、y = 白点、z = ブルームの強さ、w = ビネットの強さ。</summary>
    DirectX::XMFLOAT4 params;

    /// <summary>xy = 画面の大きさ、zw = その逆数。</summary>
    DirectX::XMFLOAT4 screenSize;
};


/// <summary>
/// ブルームの各パスでシェーダーへ渡す定数。
/// </summary>
struct BloomConstants
{
    /// <summary>x = しきい値、y = 立ち上がりのなだらかさ。z, w は未使用。</summary>
    DirectX::XMFLOAT4 params;

    /// <summary>xy = ずらす向き、zw = 入力の 1 テクセルの大きさ。</summary>
    DirectX::XMFLOAT4 blurDirection;
};


/// <summary>
/// 後処理（ポストプロセス）の一連のパスをまとめたクラス。
/// </summary>
class PostProcessPipeline
{
public:
    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    PostProcessPipeline() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~PostProcessPipeline() = default;

    /// <summary>コピー構築は禁止です。</summary>
    PostProcessPipeline(const PostProcessPipeline&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    PostProcessPipeline& operator=(const PostProcessPipeline&) = delete;

    /// <summary>
    /// ルートシグネチャ・PSO・作業用テクスチャを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="descriptorHeap">SRV を登録するシェーダー可視ヒープ。</param>
    /// <param name="width">画面の幅。作業用テクスチャはこの半分になります。</param>
    /// <param name="height">画面の高さ。</param>
    /// <param name="frameCount">同時に処理するフレーム数。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void Initialize(ID3D12Device* device,
                    DescriptorHeap& descriptorHeap,
                    uint32_t width,
                    uint32_t height,
                    uint32_t frameCount);

    /// <summary>
    /// 後処理の命令を記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">使う定数のフレーム番号。</param>
    /// <param name="scene">描き終えたシーン（読める状態であること）。</param>
    /// <param name="backBufferView">最終的な書き込み先の RTV。</param>
    /// <param name="viewport">画面全体のビューポート。</param>
    /// <param name="scissor">画面全体のシザー矩形。</param>
    /// <remarks>
    /// 作業用テクスチャの状態遷移はこの中で完結します。
    /// 呼び出し側は「シーンが読める状態」であることだけ保証してください。
    /// </remarks>
    void Record(ID3D12GraphicsCommandList* commandList,
                uint32_t frameIndex,
                const RenderTexture& scene,
                D3D12_CPU_DESCRIPTOR_HANDLE backBufferView,
                const D3D12_VIEWPORT& viewport,
                const D3D12_RECT& scissor);

    /// <summary>ブルームを有効にするかを切り替えます。</summary>
    /// <param name="enabled">有効にするなら `true`。</param>
    /// <remarks>対照実験のために用意しています。</remarks>
    void SetBloomEnabled(bool enabled) noexcept { m_bloomEnabled = enabled; }

    /// <summary>ブルームが有効かを返します。</summary>
    /// <returns>有効なら `true`。</returns>
    bool IsBloomEnabled() const noexcept { return m_bloomEnabled; }

private:
    /// <summary>ルートシグネチャ（3 パス共通）。</summary>
    ComPtr<ID3D12RootSignature> m_rootSignature;

    /// <summary>明るい所を取り出すパス。</summary>
    ComPtr<ID3D12PipelineState> m_thresholdPipelineState;

    /// <summary>1 方向のぼかしを行うパス。</summary>
    ComPtr<ID3D12PipelineState> m_blurPipelineState;

    /// <summary>最終的な合成を行うパス。書き込み先だけ形式が違う。</summary>
    ComPtr<ID3D12PipelineState> m_compositePipelineState;

    /// <summary>ぼかしの往復に使う 2 枚。半分の解像度で持つ。</summary>
    RenderTexture m_bloomTexture[2];

    /// <summary>合成用の定数。</summary>
    ConstantBuffer m_compositeConstants;

    /// <summary>ブルーム用の定数。1 フレームに 3 回書き換えるのでスロットを分ける。</summary>
    ConstantBuffer m_bloomConstants;

    /// <summary>画面の幅。</summary>
    uint32_t m_width = 0;

    /// <summary>画面の高さ。</summary>
    uint32_t m_height = 0;

    /// <summary>ブルームを有効にするか。</summary>
    bool m_bloomEnabled = true;
};

} // namespace dx12
