//=============================================================================
// SwapChain.h
//   描いた絵を実際にウィンドウへ表示するための仕組み。
//
//   ■ スワップチェーン (Swap Chain) とは
//     「表示用の画像」を複数枚持ち、それを入れ替え(swap)ながら表示する仕組みです。
//
//       フロントバッファ (Front Buffer) … 今まさに画面に映っている絵
//       バックバッファ   (Back Buffer)  … 次に見せるために裏で描いている絵
//
//     描き終わったら Present() を呼び、両者の役割を入れ替えます。
//
//   ■ なぜ 2 枚必要なのか（ダブルバッファリング）
//     1 枚しかないと「表示中の絵に上書きしながら描く」ことになり、
//     描きかけの状態がそのまま画面に出ます。画面の上半分は新しい絵、
//     下半分は古い絵、といった裂け目（ティアリング）やチラつきが起きます。
//     裏で完成させてから一気に入れ替えることで、常に完成品だけが見えます。
//
//   ■ ディスクリプタとディスクリプタヒープ（DirectX 12 の重要概念）
//
//     GPU に「このメモリをレンダーターゲットとして使え」と伝えるとき、
//     ポインタを直接渡すことはできません。GPU が理解できる形式の
//     「リソースの説明書」を作って渡す必要があります。これがディスクリプタです。
//
//       ディスクリプタ (Descriptor)
//         リソース 1 個ぶんの説明書。
//         「どのアドレスに」「どんな形式で」「どういう用途で」使うかが書かれた
//         小さなデータ構造。GPU が読める形式でメモリ上に置かれます。
//
//       ディスクリプタヒープ (Descriptor Heap)
//         ディスクリプタを並べて置くための配列。
//         DirectX 12 では、まずヒープ（配列）を確保し、
//         その中の何番目にディスクリプタを書き込むかを自分で管理します。
//         DirectX 11 が裏で自動的にやっていた仕事が、明示的になったものです。
//
//       RTV (Render Target View)
//         「このリソースをレンダーターゲット（描画先）として見る」ための
//         ディスクリプタの一種。View（＝見方）という名前の通り、
//         同じメモリでも用途ごとに別の View を作ります。
//
//     本クラスは「バックバッファ 2 枚ぶんの RTV」を置くヒープを 1 つ作り、
//     0 番・1 番にそれぞれの RTV を書き込みます。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

#include <array>

namespace dx12
{
class CommandQueue;

class SwapChain
{
public:
    //-------------------------------------------------------------------------
    // バックバッファの枚数
    //   2 = ダブルバッファリング。3 にするとトリプルバッファリングとなり、
    //   フレームレートの安定性が増す代わりに表示遅延が 1 フレーム分増えます。
    //-------------------------------------------------------------------------
    static constexpr uint32_t kBackBufferCount = 2;

    //-------------------------------------------------------------------------
    // バックバッファのピクセル形式
    //   R8G8B8A8_UNORM = 赤緑青アルファを各 8bit（0〜255）で持つ形式。
    //   UNORM は「Unsigned NORMalized」の略で、0〜255 の整数値を
    //   シェーダー側では 0.0〜1.0 の小数として扱う、という意味です。
    //-------------------------------------------------------------------------
    static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    SwapChain() = default;
    ~SwapChain() = default;

    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    //-------------------------------------------------------------------------
    // 初期化
    //   @param factory      スワップチェーンを作る DXGI ファクトリ
    //   @param device       RTV を作るための D3D12 デバイス
    //   @param commandQueue このスワップチェーンに紐づくコマンドキュー
    //                       （Present のタイミングを知るために DXGI が必要とする）
    //   @param hwnd         表示先のウィンドウ
    //   @param width/height バックバッファの解像度
    //-------------------------------------------------------------------------
    void Initialize(IDXGIFactory6* factory,
                    ID3D12Device* device,
                    ID3D12CommandQueue* commandQueue,
                    HWND hwnd,
                    uint32_t width,
                    uint32_t height);

    //-------------------------------------------------------------------------
    // 画面に表示し、バックバッファを入れ替える
    //   @param enableVSync true なら垂直同期を待つ（画面のリフレッシュに同期）
    //-------------------------------------------------------------------------
    void Present(bool enableVSync = true);

    //-------------------------------------------------------------------------
    // ウィンドウサイズ変更に追従してバックバッファを作り直す
    //
    //   ★ 呼び出し前に必ず GPU の作業完了を待つこと（CommandQueue::Flush）。
    //     GPU がまだ読み書きしているバッファを破棄すると即クラッシュします。
    //-------------------------------------------------------------------------
    void Resize(ID3D12Device* device, uint32_t width, uint32_t height);

    // --- 参照用アクセサ ------------------------------------------------------

    // 「次に描き込むべき」バックバッファの番号（0 or 1）
    uint32_t CurrentBackBufferIndex() const noexcept { return m_currentBackBufferIndex; }

    // 現在のバックバッファのリソース本体
    ID3D12Resource* CurrentBackBuffer() const noexcept
    {
        return m_backBuffers[m_currentBackBufferIndex].Get();
    }

    // 現在のバックバッファに対応する RTV ディスクリプタの位置
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRenderTargetView() const;

    uint32_t Width()  const noexcept { return m_width; }
    uint32_t Height() const noexcept { return m_height; }

private:
    // バックバッファのリソースを取得し、RTV を作り直す（初期化とリサイズで共用）
    void CreateRenderTargetViews(ID3D12Device* device);

private:
    ComPtr<IDXGISwapChain4> m_swapChain;

    // バックバッファ本体（GPU 上のテクスチャ）。DXGI が確保したものを借りる形。
    std::array<ComPtr<ID3D12Resource>, kBackBufferCount> m_backBuffers;

    // RTV を置くためのディスクリプタヒープ
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    //-------------------------------------------------------------------------
    // ディスクリプタ 1 個あたりのバイト数
    //
    //   この値は GPU の世代・ベンダーによって異なります（32 だったり 64 だったり）。
    //   そのため「N 番目のディスクリプタの位置」を求めるときは、
    //   必ず GetDescriptorHandleIncrementSize() で実行時に取得した値を使います。
    //   数値をハードコードすると別の GPU で壊れます。
    //-------------------------------------------------------------------------
    uint32_t m_rtvDescriptorSize = 0;

    uint32_t m_currentBackBufferIndex = 0;
    uint32_t m_width  = 0;
    uint32_t m_height = 0;
};

} // namespace dx12
