//=============================================================================
// ShadowMap.h
//   光源から見た深度を書き込むテクスチャ（シャドウマップ）。
//
//   1 枚のリソースを「深度バッファとして書く」と「テクスチャとして読む」の
//   両方に使う。用途が変わるたびにリソースバリアが必要になる点が要点。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

#include <DirectXMath.h>

namespace dx12
{
class DescriptorHeap;

/// <summary>
/// 光源から見た深度を保持し、影の判定に使うテクスチャ。
/// </summary>
class ShadowMap
{
public:
    /// <summary>
    /// リソース本体の形式。
    /// </summary>
    /// <remarks>
    /// 型を決めない `TYPELESS` で作ります。深度バッファとして書くときは
    /// `D32_FLOAT`、テクスチャとして読むときは `R32_FLOAT` と、
    /// ビューごとに別の解釈を与えるためです。`D32_FLOAT` で作ると SRV を作れません。
    /// </remarks>
    static constexpr DXGI_FORMAT kResourceFormat = DXGI_FORMAT_R32_TYPELESS;

    /// <summary>深度バッファとして書き込むときの形式。</summary>
    static constexpr DXGI_FORMAT kDepthStencilViewFormat = DXGI_FORMAT_D32_FLOAT;

    /// <summary>テクスチャとして読むときの形式。</summary>
    static constexpr DXGI_FORMAT kShaderResourceViewFormat = DXGI_FORMAT_R32_FLOAT;

    /// <summary>クリアに使う深度値（一番奥）。</summary>
    static constexpr float kClearDepth = 1.0f;

    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    ShadowMap() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~ShadowMap() = default;

    /// <summary>コピー構築は禁止です。</summary>
    ShadowMap(const ShadowMap&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    ShadowMap& operator=(const ShadowMap&) = delete;

    /// <summary>
    /// シャドウマップ本体・DSV・SRV を生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="descriptorHeap">SRV を登録するシェーダー可視ヒープ。</param>
    /// <param name="size">一辺のピクセル数。大きいほど影の輪郭が細かくなります。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void Initialize(ID3D12Device* device, DescriptorHeap& descriptorHeap, uint32_t size);

    /// <summary>
    /// 光源をカメラに見立てた変換行列を計算します。
    /// </summary>
    /// <param name="lightDirection">光が進む向き。正規化されていなくても構いません。</param>
    /// <param name="sceneCenter">影を落とす範囲の中心。</param>
    /// <param name="sceneRadius">影を落とす範囲の半径。</param>
    /// <remarks>
    /// 平行光源には位置がないため、範囲全体が収まる位置へ仮の視点を置きます。
    /// 遠近感を付けてはいけないので、透視投影ではなく**正射影**を使います。
    /// </remarks>
    void SetLight(const DirectX::XMFLOAT3& lightDirection,
                  const DirectX::XMFLOAT3& sceneCenter,
                  float sceneRadius);

    /// <summary>
    /// 光源から見たビュー行列 × 射影行列を返します。
    /// </summary>
    /// <returns>`SetLight` で計算した行列。</returns>
    DirectX::XMMATRIX LightViewProjection() const;

    /// <summary>
    /// シャドウマップへの描き込みを開始します。
    /// </summary>
    /// <param name="commandList">記録先の（Reset 済みで開いている）コマンドリスト。</param>
    /// <remarks>
    /// バリア（テクスチャ → 深度バッファ）、ビューポート、描画先の設定、
    /// クリアまでを行います。この後にメッシュを描くと深度が書き込まれます。
    /// </remarks>
    void BeginRender(ID3D12GraphicsCommandList* commandList) const;

    /// <summary>
    /// シャドウマップへの描き込みを終え、テクスチャとして読める状態に戻します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    void EndRender(ID3D12GraphicsCommandList* commandList) const;

    /// <summary>
    /// シェーダーから読むための GPU ディスクリプタハンドルを返します。
    /// </summary>
    /// <returns>`SetGraphicsRootDescriptorTable` に渡すハンドル。</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE ShaderResourceView() const { return m_shaderResourceView; }

    /// <summary>一辺のピクセル数を返します。</summary>
    /// <returns>初期化時に指定したサイズ。</returns>
    uint32_t Size() const noexcept { return m_size; }

private:
    /// <summary>深度テクスチャ本体（DEFAULT ヒープ）。</summary>
    ComPtr<ID3D12Resource> m_shadowMap;

    /// <summary>DSV 専用のディスクリプタヒープ（シェーダーからは見えない）。</summary>
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    /// <summary>シェーダー可視ヒープ上の SRV の位置。</summary>
    D3D12_GPU_DESCRIPTOR_HANDLE m_shaderResourceView = {};

    /// <summary>光源から見たビュー行列 × 射影行列。</summary>
    /// <remarks>
    /// `XMMATRIX` は 16 バイト境界に揃える必要があるため、
    /// メンバでは `XMFLOAT4X4` で持ち、使うときに読み込みます。
    /// </remarks>
    DirectX::XMFLOAT4X4 m_lightViewProjection = {};

    /// <summary>一辺のピクセル数。</summary>
    uint32_t m_size = 0;

    /// <summary>シャドウマップ全体を覆うビューポート。</summary>
    D3D12_VIEWPORT m_viewport = {};

    /// <summary>シャドウマップ全体を覆うシザー矩形。</summary>
    D3D12_RECT m_scissorRect = {};
};

} // namespace dx12
