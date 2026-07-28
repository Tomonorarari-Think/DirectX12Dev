//=============================================================================
// SwapChain.h
//   描いた絵をウィンドウへ表示する仕組みと、RTV ディスクリプタの管理。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

#include <array>

namespace dx12
{
class CommandQueue;

/// <summary>
/// スワップチェーン（表と裏の絵の入れ替え）と、そのレンダーターゲットビューを管理するクラス。
/// </summary>
class SwapChain
{
public:
    /// <summary>
    /// バックバッファの枚数。
    /// </summary>
    static constexpr uint32_t kBackBufferCount = 2;

    /// <summary>
    /// バックバッファのピクセル形式。
    /// </summary>
    static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    /// <summary>
    /// バックバッファを描画先として見るときの形式。
    /// </summary>
    /// <remarks>
    /// ★ スワップチェーン本体とは**わざと違う形式**にしています。
    /// `_SRGB` を付けると、シェーダーが書いたリニアの値を GPU が sRGB へ
    /// 変換してから格納してくれます。
    ///
    /// スワップチェーン本体に `_SRGB` を指定できないのは、フリップモデル
    /// (`FLIP_DISCARD`) の制約です。RTV 側で指定するのが定石になっています。
    /// </remarks>
    static constexpr DXGI_FORMAT kRenderTargetViewFormat =
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    /// <summary>
    /// 既定のコンストラクタ。まだ何も生成されません。
    /// </summary>
    SwapChain() = default;

    /// <summary>
    /// デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。
    /// </summary>
    ~SwapChain() = default;

    /// <summary>
    /// コピー構築は禁止です。
    /// </summary>
    SwapChain(const SwapChain&) = delete;

    /// <summary>
    /// コピー代入は禁止です。
    /// </summary>
    SwapChain& operator=(const SwapChain&) = delete;

    /// <summary>
    /// スワップチェーンと RTV ディスクリプタヒープを生成します。
    /// </summary>
    /// <param name="factory">スワップチェーンを作る DXGI ファクトリ。</param>
    /// <param name="device">RTV を作るための D3D12 デバイス。</param>
    /// <param name="commandQueue">
    /// このスワップチェーンに紐づくコマンドキュー。Present のタイミングを知るために DXGI が必要
    /// とします。
    /// </param>
    /// <param name="hwnd">表示先のウィンドウ。</param>
    /// <param name="width">バックバッファの幅（ピクセル）。</param>
    /// <param name="height">バックバッファの高さ（ピクセル）。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void Initialize(IDXGIFactory6* factory,
                    ID3D12Device* device,
                    ID3D12CommandQueue* commandQueue,
                    HWND hwnd,
                    uint32_t width,
                    uint32_t height);

    /// <summary>
    /// 画面に表示し、バックバッファを入れ替えます。
    /// </summary>
    /// <param name="enableVSync">
    /// `true` なら垂直同期を待ちます（画面のリフレッシュに同期）。
    /// </param>
    /// <exception cref="HrException">Present に失敗した場合。</exception>
    void Present(bool enableVSync = true);

    /// <summary>
    /// ウィンドウサイズ変更に追従してバックバッファを作り直します。
    /// </summary>
    /// <param name="device">RTV を作り直すための D3D12 デバイス。</param>
    /// <param name="width">新しい幅（ピクセル）。0 なら何もしません。</param>
    /// <param name="height">新しい高さ（ピクセル）。0 なら何もしません。</param>
    /// <exception cref="HrException">ResizeBuffers に失敗した場合。</exception>
    void Resize(ID3D12Device* device, uint32_t width, uint32_t height);

    /// <summary>
    /// 「次に描き込むべき」バックバッファの番号を取得します。
    /// </summary>
    /// <returns>0 以上 `kBackBufferCount` 未満の番号。</returns>
    uint32_t CurrentBackBufferIndex() const noexcept { return m_currentBackBufferIndex; }

    /// <summary>
    /// 現在のバックバッファのリソース本体を取得します。
    /// </summary>
    /// <returns>バックバッファの生ポインタ（所有権は移動しません）。</returns>
    ID3D12Resource* CurrentBackBuffer() const noexcept
    {
        return m_backBuffers[m_currentBackBufferIndex].Get();
    }

    /// <summary>
    /// 現在のバックバッファに対応する RTV ディスクリプタの位置を取得します。
    /// </summary>
    /// <returns>ヒープ内の CPU ディスクリプタハンドル。</returns>
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRenderTargetView() const;

    /// <summary>
    /// 現在のバックバッファの幅を取得します。
    /// </summary>
    /// <returns>幅（ピクセル）。</returns>
    uint32_t Width() const noexcept { return m_width; }

    /// <summary>
    /// 現在のバックバッファの高さを取得します。
    /// </summary>
    /// <returns>高さ（ピクセル）。</returns>
    uint32_t Height() const noexcept { return m_height; }

private:
    /// <summary>
    /// バックバッファを取得し、それぞれの RTV をヒープに書き込みます。
    /// </summary>
    /// <param name="device">RTV を作るための D3D12 デバイス。</param>
    /// <exception cref="HrException">バッファの取得に失敗した場合。</exception>
    void CreateRenderTargetViews(ID3D12Device* device);

private:
    /// <summary>
    /// スワップチェーン本体。
    /// </summary>
    ComPtr<IDXGISwapChain4> m_swapChain;

    /// <summary>
    /// バックバッファ本体（DXGI が確保したものを借りる形）。
    /// </summary>
    std::array<ComPtr<ID3D12Resource>, kBackBufferCount> m_backBuffers;

    /// <summary>
    /// RTV を置くためのディスクリプタヒープ。
    /// </summary>
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    /// <summary>
    /// ディスクリプタ 1 個あたりのバイト数。
    /// </summary>
    uint32_t m_rtvDescriptorSize = 0;

    /// <summary>
    /// 次に描き込むバックバッファの番号。
    /// </summary>
    uint32_t m_currentBackBufferIndex = 0;

    /// <summary>
    /// バックバッファの幅（ピクセル）。
    /// </summary>
    uint32_t m_width = 0;

    /// <summary>
    /// バックバッファの高さ（ピクセル）。
    /// </summary>
    uint32_t m_height = 0;
};

} // namespace dx12
