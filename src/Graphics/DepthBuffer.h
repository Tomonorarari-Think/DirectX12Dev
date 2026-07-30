//=============================================================================
// DepthBuffer.h
//   奥行き判定（深度テスト）に使うバッファと、その DSV。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

class DescriptorHeap;

/// <summary>
/// 深度バッファ（Z バッファ）と、その深度ステンシルビューを管理するクラス。
/// </summary>
class DepthBuffer
{
public:
    /// <summary>
    /// リソース本体の形式。
    /// </summary>
    /// <remarks>
    /// 型を決めない `TYPELESS` で作ります。深度バッファとして書くときは
    /// `D32_FLOAT`、テクスチャとして読むときは `R32_FLOAT` と、ビューごとに
    /// 別の解釈を与えるためです。`D32_FLOAT` で作ると SRV を作れません。
    /// </remarks>
    static constexpr DXGI_FORMAT kResourceFormat = DXGI_FORMAT_R32_TYPELESS;

    /// <summary>
    /// 深度バッファのフォーマット（DSV と PSO で使う形式）。
    /// </summary>
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_D32_FLOAT;

    /// <summary>テクスチャとして読むときの形式。</summary>
    static constexpr DXGI_FORMAT kShaderResourceViewFormat = DXGI_FORMAT_R32_FLOAT;

    /// <summary>
    /// 毎フレームのクリアに使う深度値。
    /// </summary>
    static constexpr float kClearDepth = 1.0f;

    /// <summary>
    /// 既定のコンストラクタ。まだ何も生成されません。
    /// </summary>
    DepthBuffer() = default;

    /// <summary>
    /// デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。
    /// </summary>
    ~DepthBuffer() = default;

    /// <summary>
    /// コピー構築は禁止です。
    /// </summary>
    DepthBuffer(const DepthBuffer&) = delete;

    /// <summary>
    /// コピー代入は禁止です。
    /// </summary>
    DepthBuffer& operator=(const DepthBuffer&) = delete;

    /// <summary>
    /// DSV ディスクリプタヒープと深度バッファ本体を生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="descriptorHeap">SRV を登録するシェーダー可視ヒープ。</param>
    /// <param name="width">
    /// 深度バッファの幅（ピクセル）。レンダーターゲットと同じにすること。
    /// </param>
    /// <param name="height">
    /// 深度バッファの高さ（ピクセル）。レンダーターゲットと同じにすること。
    /// </param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void Initialize(ID3D12Device* device, DescriptorHeap& descriptorHeap,
                    uint32_t width, uint32_t height);

    /// <summary>
    /// ウィンドウサイズ変更に追従して深度バッファを作り直します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="width">新しい幅（ピクセル）。0 なら何もしません。</param>
    /// <param name="height">新しい高さ（ピクセル）。0 なら何もしません。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    /// <remarks>
    /// 呼び出し前に必ず GPU の作業完了を待ってください。 GPU がまだ書き込んでいるバッファを破棄
    /// すると即クラッシュします。
    /// </remarks>
    void Resize(ID3D12Device* device, uint32_t width, uint32_t height);

    /// <summary>
    /// 深度ステンシルビュー（DSV）の位置を取得します。
    /// </summary>
    /// <returns>ヒープ内の CPU ディスクリプタハンドル。</returns>
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const
    {
        return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    }

    /// <summary>
    /// 書き込みを禁じた深度ステンシルビュー（DSV）の位置を取得します。
    /// </summary>
    /// <returns>ヒープ内の CPU ディスクリプタハンドル。</returns>
    /// <remarks>
    /// **同じ深度バッファを、深度テストとテクスチャ読みで同時に使うため**の
    /// ビューです。書き込みができない状態にして初めて、`DEPTH_READ` と
    /// `PIXEL_SHADER_RESOURCE` を同時に立てられます。
    /// </remarks>
    D3D12_CPU_DESCRIPTOR_HANDLE ReadOnlyDepthStencilView() const;

    /// <summary>
    /// シェーダーから深度を読むための SRV を取得します。
    /// </summary>
    /// <returns>GPU ディスクリプタハンドル。</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE ShaderResourceView() const
    {
        return m_shaderResourceView;
    }

    /// <summary>
    /// 深度バッファのリソースを取得します。
    /// </summary>
    /// <returns>リソースの生ポインタ（所有権は移動しません）。</returns>
    ID3D12Resource* Resource() const noexcept { return m_depthBuffer.Get(); }

private:
    /// <summary>
    /// 深度バッファのリソースを作り、DSV をヒープに書き込みます。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="width">幅（ピクセル）。</param>
    /// <param name="height">高さ（ピクセル）。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void CreateResourceAndView(ID3D12Device* device, uint32_t width, uint32_t height);

private:
    /// <summary>
    /// DSV を 2 個（書き込み可・書き込み禁止）置くためのディスクリプタヒープ。
    /// </summary>
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    /// <summary>DSV 1 個あたりのバイト数（GPU ごとに異なる）。</summary>
    uint32_t m_dsvDescriptorSize = 0;

    /// <summary>SRV を置いたシェーダー可視ヒープ。リサイズ時に作り直します。</summary>
    DescriptorHeap* m_descriptorHeap = nullptr;

    /// <summary>シェーダー可視ヒープ内での SRV の番号。</summary>
    uint32_t m_shaderResourceViewIndex = 0;

    /// <summary>SRV の GPU ハンドル。</summary>
    D3D12_GPU_DESCRIPTOR_HANDLE m_shaderResourceView = {};

    /// <summary>
    /// 深度値を記録するテクスチャ本体。
    /// </summary>
    ComPtr<ID3D12Resource> m_depthBuffer;

    /// <summary>
    /// 現在の幅（ピクセル）。
    /// </summary>
    uint32_t m_width = 0;

    /// <summary>
    /// 現在の高さ（ピクセル）。
    /// </summary>
    uint32_t m_height = 0;
};

} // namespace dx12
