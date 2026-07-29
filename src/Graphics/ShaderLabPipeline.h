//=============================================================================
// ShaderLabPipeline.h
//   習作シェーダーを切り替えながら全画面に描くための仕組み。
//
//   3D モデルの描画とは独立していて、画面いっぱいの三角形 1 枚に
//   ピクセルシェーダーだけを差し替えて絵を描く。
//   詳しい解説は docs/shader-lab/README.md を参照。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"
#include "ConstantBuffer.h"

#include <DirectXMath.h>
#include <string>
#include <vector>

namespace dx12
{

/// <summary>
/// 習作シェーダーへ渡す共通の値。
/// </summary>
struct ShaderLabConstants
{
    /// <summary>x = 経過秒、y = 前フレームからの秒、z = 何番目か、w = 未使用。</summary>
    DirectX::XMFLOAT4 time;

    /// <summary>xy = 画面の大きさ、zw = その逆数。</summary>
    DirectX::XMFLOAT4 resolution;

    /// <summary>xy = マウスの位置、z = 左ボタン、w = 未使用。</summary>
    DirectX::XMFLOAT4 mouse;
};


/// <summary>
/// 習作シェーダーを 1 つ描くパイプライン。
/// </summary>
/// <remarks>
/// 起動時にすべての習作をコンパイルし、PSO を並べて持ちます。
/// 切り替えは配列の添字を変えるだけなので、フレーム内で自由に切り替えられます。
/// </remarks>
class ShaderLabPipeline
{
public:
    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    ShaderLabPipeline() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~ShaderLabPipeline() = default;

    /// <summary>コピー構築は禁止です。</summary>
    ShaderLabPipeline(const ShaderLabPipeline&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    ShaderLabPipeline& operator=(const ShaderLabPipeline&) = delete;

    /// <summary>
    /// 習作シェーダーをすべてコンパイルし、PSO を用意します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="renderTargetFormat">書き込み先の形式。</param>
    /// <param name="frameCount">同時に処理するフレーム数。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    /// <remarks>
    /// 1 本でも文法エラーがあると起動できません。習作を足すときは
    /// `kShaderFiles` に 1 行足すだけで済むようにしてあります。
    /// </remarks>
    void Initialize(ID3D12Device* device,
                    DXGI_FORMAT renderTargetFormat,
                    uint32_t frameCount);

    /// <summary>
    /// このフレームぶんの値を書き込みます。
    /// </summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    /// <param name="totalSeconds">起動からの経過秒。</param>
    /// <param name="deltaSeconds">前フレームからの経過秒。</param>
    /// <param name="width">画面の幅。</param>
    /// <param name="height">画面の高さ。</param>
    /// <param name="mouseX">マウスの X 座標（ピクセル）。</param>
    /// <param name="mouseY">マウスの Y 座標（ピクセル）。</param>
    /// <param name="mouseDown">左ボタンが押されていれば `true`。</param>
    void Update(uint32_t frameIndex,
                float totalSeconds,
                float deltaSeconds,
                uint32_t width,
                uint32_t height,
                float mouseX,
                float mouseY,
                bool mouseDown);

    /// <summary>
    /// いま選ばれている習作を描く命令を記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">使う定数のフレーム番号。</param>
    void Record(ID3D12GraphicsCommandList* commandList, uint32_t frameIndex) const;

    /// <summary>習作の数を返します。</summary>
    /// <returns>読み込めた習作の数。</returns>
    uint32_t Count() const noexcept
    {
        return static_cast<uint32_t>(m_pipelineStates.size());
    }

    /// <summary>いま選ばれている習作の番号を返します。</summary>
    /// <returns>0 から始まる番号。</returns>
    uint32_t CurrentIndex() const noexcept { return m_currentIndex; }

    /// <summary>いま選ばれている習作の名前を返します。</summary>
    /// <returns>表示用の名前。</returns>
    const std::wstring& CurrentName() const;

    /// <summary>
    /// 表示する習作を選びます。
    /// </summary>
    /// <param name="index">0 から始まる番号。範囲外なら折り返します。</param>
    void Select(int index);

    /// <summary>
    /// 表示する習作を前後に動かします。
    /// </summary>
    /// <param name="delta">+1 で次、-1 で前。</param>
    void Advance(int delta);

private:
    /// <summary>習作ごとの PSO。</summary>
    std::vector<ComPtr<ID3D12PipelineState>> m_pipelineStates;

    /// <summary>習作ごとの表示名。</summary>
    std::vector<std::wstring> m_names;

    /// <summary>ルートシグネチャ（全習作で共通）。</summary>
    ComPtr<ID3D12RootSignature> m_rootSignature;

    /// <summary>フレーム別の定数。</summary>
    ConstantBuffer m_constantBuffer;

    /// <summary>いま選ばれている習作の番号。</summary>
    uint32_t m_currentIndex = 0;
};

} // namespace dx12
