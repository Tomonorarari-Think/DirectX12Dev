//=============================================================================
// AutoExposure.h
//   画面の明るさを GPU 上で測り、露出を自動で合わせる仕組み。
//
//   何万ピクセルを 1 つの値にまとめる「並列リダクション」を、
//   Wave 命令を使う版と使わない版の両方で持つ（対照実験のため）。
//   詳しい解説は docs/tutorial/32_自動露出とWave命令.md を参照。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"
#include "ConstantBuffer.h"

namespace dx12
{

class DescriptorHeap;
class RenderTexture;

/// <summary>
/// 明るさを 1 つの値にまとめるやり方。
/// </summary>
enum class ReductionMode
{
    /// <summary>Wave 命令（`WaveActiveSum`）を使う。シェーダーモデル 6 以降。</summary>
    Wave,

    /// <summary>共有メモリで半分ずつ折りたたむ。従来からのやり方。</summary>
    GroupShared,
};


/// <summary>
/// 画面の明るさから露出を決めるクラス。
/// </summary>
class AutoExposure
{
public:
    /// <summary>1 グループのスレッド数（8 x 8）。</summary>
    /// <remarks>`AutoExposure.hlsl` の `THREADS_X` / `THREADS_Y` と揃えること。</remarks>
    static constexpr uint32_t kThreadsX = 8;

    /// <summary>1 グループのスレッド数（縦）。</summary>
    static constexpr uint32_t kThreadsY = 8;

    /// <summary>測るときに画面を何分の 1 に縮めるか（初期値）。</summary>
    /// <remarks>
    /// 全ピクセルを測る必要はありません。1/4 でも平均はほとんど変わらず、
    /// 読む量が 16 分の 1 になります。
    /// </remarks>
    static constexpr uint32_t kDefaultSampleDivisor = 4;

    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    AutoExposure() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~AutoExposure() = default;

    /// <summary>コピー構築は禁止です。</summary>
    AutoExposure(const AutoExposure&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    AutoExposure& operator=(const AutoExposure&) = delete;

    /// <summary>
    /// 集計用バッファ・UAV / SRV・3 つの PSO を生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="descriptorHeap">UAV と SRV を登録するシェーダー可視ヒープ。</param>
    /// <param name="width">画面の幅（ピクセル）。</param>
    /// <param name="height">画面の高さ（ピクセル）。</param>
    /// <param name="frameCount">同時に処理するフレーム数。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void Initialize(ID3D12Device* device,
                    DescriptorHeap& descriptorHeap,
                    uint32_t width,
                    uint32_t height,
                    uint32_t frameCount);

    /// <summary>
    /// 画面の大きさが変わったときに、測る範囲を作り直します。
    /// </summary>
    /// <param name="width">新しい幅（ピクセル）。</param>
    /// <param name="height">新しい高さ（ピクセル）。</param>
    void Resize(uint32_t width, uint32_t height);

    /// <summary>
    /// このフレームぶんの定数を書き込みます。
    /// </summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    /// <param name="deltaSeconds">前フレームからの経過秒。追従の速さに使います。</param>
    void Update(uint32_t frameIndex, float deltaSeconds);

    /// <summary>
    /// 明るさを測り、露出を決める命令を記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">使う定数のフレーム番号。</param>
    /// <param name="sceneTexture">測る対象（描き終えた HDR の絵）。</param>
    /// <param name="mode">まとめ方。</param>
    /// <remarks>
    /// **シーンを描き終え、テクスチャとして読める状態にしてから呼びます。**
    /// 呼び終えると、露出バッファは後処理から読める状態になっています。
    /// </remarks>
    void Record(ID3D12GraphicsCommandList* commandList,
                uint32_t frameIndex,
                const RenderTexture& sceneTexture,
                ReductionMode mode);

    /// <summary>
    /// 測る解像度を 1/4 → 1/2 → 1/1 と切り替えます。
    /// </summary>
    /// <returns>切り替えたあとの分母。</returns>
    /// <remarks>
    /// **集計の量を変えて速さを比べるための機能です。**
    /// 細かく測っても露出はほとんど変わりません。
    /// </remarks>
    uint32_t CycleSampleDivisor();

    /// <summary>いま測っている画像の幅を返します。</summary>
    /// <returns>ピクセル数。</returns>
    uint32_t SampleWidth() const noexcept { return m_sampleWidth; }

    /// <summary>いま測っている画像の高さを返します。</summary>
    /// <returns>ピクセル数。</returns>
    uint32_t SampleHeight() const noexcept { return m_sampleHeight; }

    /// <summary>露出を読むための SRV を返します。</summary>
    /// <returns>`SetGraphicsRootDescriptorTable` に渡すハンドル。</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE ShaderResourceView() const
    {
        return m_shaderResourceView;
    }

private:
    /// <summary>コンピュート用のルートシグネチャ。</summary>
    ComPtr<ID3D12RootSignature> m_rootSignature;

    /// <summary>集計（Wave 版）の PSO。</summary>
    ComPtr<ID3D12PipelineState> m_wavePipelineState;

    /// <summary>集計（共有メモリ版）の PSO。</summary>
    ComPtr<ID3D12PipelineState> m_groupSharedPipelineState;

    /// <summary>露出を決める PSO。</summary>
    ComPtr<ID3D12PipelineState> m_resolvePipelineState;

    /// <summary>合計と露出を置くバッファ。</summary>
    ComPtr<ID3D12Resource> m_stateBuffer;

    /// <summary>書き込み用（UAV）の GPU ハンドル。</summary>
    D3D12_GPU_DESCRIPTOR_HANDLE m_unorderedAccessView = {};

    /// <summary>読み出し用（SRV）の GPU ハンドル。</summary>
    D3D12_GPU_DESCRIPTOR_HANDLE m_shaderResourceView = {};

    /// <summary>定数。</summary>
    ConstantBuffer m_constantBuffer;

    /// <summary>画面の幅（ピクセル）。</summary>
    uint32_t m_screenWidth = 0;

    /// <summary>画面の高さ（ピクセル）。</summary>
    uint32_t m_screenHeight = 0;

    /// <summary>いま使っている縮小率の分母。</summary>
    uint32_t m_sampleDivisor = kDefaultSampleDivisor;

    /// <summary>測る画像の幅（ピクセル）。</summary>
    uint32_t m_sampleWidth = 0;

    /// <summary>測る画像の高さ（ピクセル）。</summary>
    uint32_t m_sampleHeight = 0;

    /// <summary>2 回目以降か。バッファの状態を戻す必要があるかの判断に使います。</summary>
    /// <remarks>
    /// 生成直後は `UNORDERED_ACCESS` で、`Record` は
    /// `PIXEL_SHADER_RESOURCE` で終わります。2 回目からは戻してから始めます。
    /// </remarks>
    bool m_recorded = false;
};

} // namespace dx12
