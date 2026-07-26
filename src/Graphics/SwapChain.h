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

/// @brief スワップチェーン（表と裏の絵の入れ替え）と、そのレンダーターゲットビューを管理するクラス。
///
/// **スワップチェーン (Swap Chain) とは**
///
/// 「表示用の画像」を複数枚持ち、それを入れ替え (swap) ながら表示する仕組みです。
///
/// - フロントバッファ (Front Buffer) … 今まさに画面に映っている絵
/// - バックバッファ (Back Buffer) … 次に見せるために裏で描いている絵
///
/// 描き終わったら `Present` を呼び、両者の役割を入れ替えます。
///
/// **なぜ 2 枚必要なのか（ダブルバッファリング）**
///
/// 1 枚しかないと「表示中の絵に上書きしながら描く」ことになり、描きかけの状態がそのまま画面に出ます。
/// 画面の上半分は新しい絵、下半分は古い絵、といった裂け目（ティアリング）やチラつきが起きます。裏で
/// 完成させてから一気に入れ替えることで、常に完成品だけが見えます。
///
/// **ディスクリプタとディスクリプタヒープ（DirectX 12 の重要概念）**
///
/// GPU に「このメモリをレンダーターゲットとして使え」と伝えるとき、ポインタを直接渡すことはできませ
/// ん。GPU が理解できる形式の「リソースの説明書」を作って渡す必要があります。
///
/// - **ディスクリプタ (Descriptor)** : リソース 1 個ぶんの説明書。「どのアドレスに」「どんな形式で」
///   「どういう用途で」使うかが書かれた小さなデータ構造。
/// - **ディスクリプタヒープ (Descriptor Heap)** : ディスクリプタを並べて置くための配列。DirectX 12
///   では、まずヒープを確保し、その中の何番目に書き込むかを自分で管理します。DirectX 11 が裏で自動
///   的にやっていた仕事が、明示的になったものです。
/// - **RTV (Render Target View)** : 「このリソースをレンダーターゲット（描画先）として見る」ための
///   ディスクリプタの一種。View（＝見方）という名前の通り、同じメモリでも用途ごとに別の View を作り
///   ます。
///
/// 本クラスは「バックバッファ 2 枚ぶんの RTV」を置くヒープを 1 つ作り、0 番・1 番にそれぞれの RTV
/// を書き込みます。
class SwapChain
{
public:
    /// @brief バックバッファの枚数。
    ///
    /// 2 = ダブルバッファリング。3 にするとトリプルバッファリングとなり、フレームレートの安定性が増す代
    /// わりに表示遅延が 1 フレーム分増えます。
    static constexpr uint32_t kBackBufferCount = 2;

    /// @brief バックバッファのピクセル形式。
    ///
    /// `R8G8B8A8_UNORM` = 赤緑青アルファを各 8bit（0〜255）で持つ形式。UNORM は「Unsigned NORMalized」
    /// の略で、0〜255 の整数値をシェーダー側では 0.0〜1.0 の小数として扱う、という意味です。
    static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    /// @brief 既定のコンストラクタ。まだ何も生成されません。
    SwapChain() = default;

    /// @brief デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。
    ~SwapChain() = default;

    /// @brief コピー構築は禁止です。
    SwapChain(const SwapChain&) = delete;

    /// @brief コピー代入は禁止です。
    SwapChain& operator=(const SwapChain&) = delete;

    /// @brief スワップチェーンと RTV ディスクリプタヒープを生成します。
    /// @param factory スワップチェーンを作る DXGI ファクトリ。
    /// @param device RTV を作るための D3D12 デバイス。
    /// @param commandQueue このスワップチェーンに紐づくコマンドキュー。Present のタイミングを知るために
    ///     DXGI が必要とします。
    /// @param hwnd 表示先のウィンドウ。
    /// @param width バックバッファの幅（ピクセル）。
    /// @param height バックバッファの高さ（ピクセル）。
    /// @exception HrException 生成に失敗した場合。
    void Initialize(IDXGIFactory6* factory,
                    ID3D12Device* device,
                    ID3D12CommandQueue* commandQueue,
                    HWND hwnd,
                    uint32_t width,
                    uint32_t height);

    /// @brief 画面に表示し、バックバッファを入れ替えます。
    /// @param enableVSync `true` なら垂直同期を待ちます（画面のリフレッシュに同期）。
    /// @exception HrException Present に失敗した場合。
    ///
    /// この呼び出しで `CurrentBackBufferIndex` の値が変わります。
    void Present(bool enableVSync = true);

    /// @brief ウィンドウサイズ変更に追従してバックバッファを作り直します。
    /// @param device RTV を作り直すための D3D12 デバイス。
    /// @param width 新しい幅（ピクセル）。0 なら何もしません。
    /// @param height 新しい高さ（ピクセル）。0 なら何もしません。
    /// @exception HrException ResizeBuffers に失敗した場合。
    ///
    /// 呼び出し前に必ず GPU の作業完了を待ってください（`CommandQueue::Flush`）。GPU がまだ読み書きして
    /// いるバッファを破棄すると即クラッシュします。
    void Resize(ID3D12Device* device, uint32_t width, uint32_t height);

    /// @brief 「次に描き込むべき」バックバッファの番号を取得します。
    /// @returns 0 以上 `kBackBufferCount` 未満の番号。
    uint32_t CurrentBackBufferIndex() const noexcept { return m_currentBackBufferIndex; }

    /// @brief 現在のバックバッファのリソース本体を取得します。
    /// @returns バックバッファの生ポインタ（所有権は移動しません）。
    ID3D12Resource* CurrentBackBuffer() const noexcept
    {
        return m_backBuffers[m_currentBackBufferIndex].Get();
    }

    /// @brief 現在のバックバッファに対応する RTV ディスクリプタの位置を取得します。
    /// @returns ヒープ内の CPU ディスクリプタハンドル。
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRenderTargetView() const;

    /// @brief 現在のバックバッファの幅を取得します。
    /// @returns 幅（ピクセル）。
    uint32_t Width() const noexcept { return m_width; }

    /// @brief 現在のバックバッファの高さを取得します。
    /// @returns 高さ（ピクセル）。
    uint32_t Height() const noexcept { return m_height; }

private:
    /// @brief バックバッファを取得し、それぞれの RTV をヒープに書き込みます。
    /// @param device RTV を作るための D3D12 デバイス。
    /// @exception HrException バッファの取得に失敗した場合。
    ///
    /// 初期化時とリサイズ時の両方から呼ばれます。
    void CreateRenderTargetViews(ID3D12Device* device);

private:
    /// @brief スワップチェーン本体。
    ComPtr<IDXGISwapChain4> m_swapChain;

    /// @brief バックバッファ本体（DXGI が確保したものを借りる形）。
    std::array<ComPtr<ID3D12Resource>, kBackBufferCount> m_backBuffers;

    /// @brief RTV を置くためのディスクリプタヒープ。
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    /// @brief ディスクリプタ 1 個あたりのバイト数。
    ///
    /// この値は GPU の世代・ベンダーによって異なります（32 だったり 64 だったり）。そのため「N 番目のデ
    /// ィスクリプタの位置」を求めるときは、必ず `GetDescriptorHandleIncrementSize()` で実行時に取得した
    /// 値を使います。数値をハードコードすると別の GPU で壊れます。
    uint32_t m_rtvDescriptorSize = 0;

    /// @brief 次に描き込むバックバッファの番号。
    uint32_t m_currentBackBufferIndex = 0;

    /// @brief バックバッファの幅（ピクセル）。
    uint32_t m_width = 0;

    /// @brief バックバッファの高さ（ピクセル）。
    uint32_t m_height = 0;
};

} // namespace dx12
