//=============================================================================
// DepthBuffer.h
//   奥行き判定（深度テスト）に使うバッファと、その DSV。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// <summary>
/// 深度バッファ（Z バッファ）と、その深度ステンシルビューを管理するクラス。
/// </summary>
class DepthBuffer
{
public:
    /// <summary>
    /// 深度バッファのフォーマット。
    /// </summary>
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_D32_FLOAT;

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
    /// <param name="width">
    /// 深度バッファの幅（ピクセル）。レンダーターゲットと同じにすること。
    /// </param>
    /// <param name="height">
    /// 深度バッファの高さ（ピクセル）。レンダーターゲットと同じにすること。
    /// </param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void Initialize(ID3D12Device* device, uint32_t width, uint32_t height);

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
    /// DSV を 1 個だけ置くためのディスクリプタヒープ。
    /// </summary>
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

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
