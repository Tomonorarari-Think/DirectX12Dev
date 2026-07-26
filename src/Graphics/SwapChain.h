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
/// <remarks>
/// <para>
/// <b>スワップチェーン (Swap Chain) とは</b><br/>
/// 「表示用の画像」を複数枚持ち、それを入れ替え (swap) ながら表示する仕組みです。
/// <list type="bullet">
///   <item>フロントバッファ (Front Buffer) … 今まさに画面に映っている絵</item>
///   <item>バックバッファ (Back Buffer) … 次に見せるために裏で描いている絵</item>
/// </list>
/// 描き終わったら <see cref="Present"/> を呼び、両者の役割を入れ替えます。
/// </para>
/// <para>
/// <b>なぜ 2 枚必要なのか（ダブルバッファリング）</b><br/>
/// 1 枚しかないと「表示中の絵に上書きしながら描く」ことになり、
/// 描きかけの状態がそのまま画面に出ます。画面の上半分は新しい絵、
/// 下半分は古い絵、といった裂け目（ティアリング）やチラつきが起きます。
/// 裏で完成させてから一気に入れ替えることで、常に完成品だけが見えます。
/// </para>
/// <para>
/// <b>ディスクリプタとディスクリプタヒープ（DirectX 12 の重要概念）</b><br/>
/// GPU に「このメモリをレンダーターゲットとして使え」と伝えるとき、
/// ポインタを直接渡すことはできません。GPU が理解できる形式の
/// 「リソースの説明書」を作って渡す必要があります。
/// <list type="table">
///   <item>
///     <term>ディスクリプタ (Descriptor)</term>
///     <description>
///       リソース 1 個ぶんの説明書。「どのアドレスに」「どんな形式で」
///       「どういう用途で」使うかが書かれた小さなデータ構造。
///     </description>
///   </item>
///   <item>
///     <term>ディスクリプタヒープ (Descriptor Heap)</term>
///     <description>
///       ディスクリプタを並べて置くための配列。DirectX 12 では、まずヒープを
///       確保し、その中の何番目に書き込むかを自分で管理します。
///       DirectX 11 が裏で自動的にやっていた仕事が、明示的になったものです。
///     </description>
///   </item>
///   <item>
///     <term>RTV (Render Target View)</term>
///     <description>
///       「このリソースをレンダーターゲット（描画先）として見る」ための
///       ディスクリプタの一種。View（＝見方）という名前の通り、
///       同じメモリでも用途ごとに別の View を作ります。
///     </description>
///   </item>
/// </list>
/// 本クラスは「バックバッファ 2 枚ぶんの RTV」を置くヒープを 1 つ作り、
/// 0 番・1 番にそれぞれの RTV を書き込みます。
/// </para>
/// </remarks>
class SwapChain
{
public:
    /// <summary>バックバッファの枚数。</summary>
    /// <remarks>
    /// 2 = ダブルバッファリング。3 にするとトリプルバッファリングとなり、
    /// フレームレートの安定性が増す代わりに表示遅延が 1 フレーム分増えます。
    /// </remarks>
    static constexpr uint32_t kBackBufferCount = 2;

    /// <summary>バックバッファのピクセル形式。</summary>
    /// <remarks>
    /// <c>R8G8B8A8_UNORM</c> = 赤緑青アルファを各 8bit（0〜255）で持つ形式。
    /// UNORM は「Unsigned NORMalized」の略で、0〜255 の整数値を
    /// シェーダー側では 0.0〜1.0 の小数として扱う、という意味です。
    /// </remarks>
    static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    SwapChain() = default;

    /// <summary>デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。</summary>
    ~SwapChain() = default;

    /// <summary>コピー構築は禁止です。</summary>
    SwapChain(const SwapChain&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    SwapChain& operator=(const SwapChain&) = delete;

    /// <summary>スワップチェーンと RTV ディスクリプタヒープを生成します。</summary>
    /// <param name="factory">スワップチェーンを作る DXGI ファクトリ。</param>
    /// <param name="device">RTV を作るための D3D12 デバイス。</param>
    /// <param name="commandQueue">
    /// このスワップチェーンに紐づくコマンドキュー。
    /// Present のタイミングを知るために DXGI が必要とします。
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

    /// <summary>画面に表示し、バックバッファを入れ替えます。</summary>
    /// <param name="enableVSync">
    /// <c>true</c> なら垂直同期を待ちます（画面のリフレッシュに同期）。
    /// </param>
    /// <exception cref="HrException">Present に失敗した場合。</exception>
    /// <remarks>この呼び出しで <see cref="CurrentBackBufferIndex"/> の値が変わります。</remarks>
    void Present(bool enableVSync = true);

    /// <summary>ウィンドウサイズ変更に追従してバックバッファを作り直します。</summary>
    /// <param name="device">RTV を作り直すための D3D12 デバイス。</param>
    /// <param name="width">新しい幅（ピクセル）。0 なら何もしません。</param>
    /// <param name="height">新しい高さ（ピクセル）。0 なら何もしません。</param>
    /// <exception cref="HrException">ResizeBuffers に失敗した場合。</exception>
    /// <remarks>
    /// 呼び出し前に必ず GPU の作業完了を待ってください
    /// （<see cref="CommandQueue::Flush"/>）。
    /// GPU がまだ読み書きしているバッファを破棄すると即クラッシュします。
    /// </remarks>
    void Resize(ID3D12Device* device, uint32_t width, uint32_t height);

    /// <summary>「次に描き込むべき」バックバッファの番号を取得します。</summary>
    /// <returns>0 以上 <see cref="kBackBufferCount"/> 未満の番号。</returns>
    uint32_t CurrentBackBufferIndex() const noexcept { return m_currentBackBufferIndex; }

    /// <summary>現在のバックバッファのリソース本体を取得します。</summary>
    /// <returns>バックバッファの生ポインタ（所有権は移動しません）。</returns>
    ID3D12Resource* CurrentBackBuffer() const noexcept
    {
        return m_backBuffers[m_currentBackBufferIndex].Get();
    }

    /// <summary>現在のバックバッファに対応する RTV ディスクリプタの位置を取得します。</summary>
    /// <returns>ヒープ内の CPU ディスクリプタハンドル。</returns>
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRenderTargetView() const;

    /// <summary>現在のバックバッファの幅を取得します。</summary>
    /// <returns>幅（ピクセル）。</returns>
    uint32_t Width() const noexcept { return m_width; }

    /// <summary>現在のバックバッファの高さを取得します。</summary>
    /// <returns>高さ（ピクセル）。</returns>
    uint32_t Height() const noexcept { return m_height; }

private:
    /// <summary>バックバッファを取得し、それぞれの RTV をヒープに書き込みます。</summary>
    /// <param name="device">RTV を作るための D3D12 デバイス。</param>
    /// <exception cref="HrException">バッファの取得に失敗した場合。</exception>
    /// <remarks>初期化時とリサイズ時の両方から呼ばれます。</remarks>
    void CreateRenderTargetViews(ID3D12Device* device);

private:
    /// <summary>スワップチェーン本体。</summary>
    ComPtr<IDXGISwapChain4> m_swapChain;

    /// <summary>バックバッファ本体（DXGI が確保したものを借りる形）。</summary>
    std::array<ComPtr<ID3D12Resource>, kBackBufferCount> m_backBuffers;

    /// <summary>RTV を置くためのディスクリプタヒープ。</summary>
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    /// <summary>ディスクリプタ 1 個あたりのバイト数。</summary>
    /// <remarks>
    /// この値は GPU の世代・ベンダーによって異なります（32 だったり 64 だったり）。
    /// そのため「N 番目のディスクリプタの位置」を求めるときは、
    /// 必ず <c>GetDescriptorHandleIncrementSize()</c> で実行時に取得した値を使います。
    /// 数値をハードコードすると別の GPU で壊れます。
    /// </remarks>
    uint32_t m_rtvDescriptorSize = 0;

    /// <summary>次に描き込むバックバッファの番号。</summary>
    uint32_t m_currentBackBufferIndex = 0;

    /// <summary>バックバッファの幅（ピクセル）。</summary>
    uint32_t m_width = 0;

    /// <summary>バックバッファの高さ（ピクセル）。</summary>
    uint32_t m_height = 0;
};

} // namespace dx12
