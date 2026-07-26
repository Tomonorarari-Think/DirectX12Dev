//=============================================================================
// SwapChain.cpp
//   SwapChain の実装。
//=============================================================================
#include "SwapChain.h"

#include <format>

namespace dx12
{
/// <summary>スワップチェーンと RTV ディスクリプタヒープを生成します。</summary>
void SwapChain::Initialize(IDXGIFactory6* factory,
                           ID3D12Device* device,
                           ID3D12CommandQueue* commandQueue,
                           HWND hwnd,
                           uint32_t width,
                           uint32_t height)
{
    m_width  = width;
    m_height = height;

    //-------------------------------------------------------------------------
    // (1) スワップチェーンの設定
    //-------------------------------------------------------------------------
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width  = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = kBackBufferFormat;

    // Stereo : 立体視（左右の目で別の絵）を使うか。今回は不要。
    swapChainDesc.Stereo = FALSE;

    // SampleDesc : マルチサンプルアンチエイリアス (MSAA) の設定。
    //   Count = 1 は「MSAA 無し」を意味します。
    //   ※ FLIP 系スワップエフェクト（後述）では MSAA 付きバックバッファを
    //     直接作れない決まりがあるため、ここは必ず 1 にします。
    //     MSAA を使う場合は別途 MSAA 用テクスチャに描いてから解決(Resolve)します。
    swapChainDesc.SampleDesc.Count   = 1;
    swapChainDesc.SampleDesc.Quality = 0;

    // BufferUsage : このバッファを何に使うか。描画先なのでレンダーターゲット出力。
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    swapChainDesc.BufferCount = kBackBufferCount;

    // Scaling : ウィンドウとバッファのサイズが食い違ったときの引き伸ばし方。
    //   STRETCH = 引き伸ばして合わせる（リサイズ中の見た目が自然）
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;

    //-------------------------------------------------------------------------
    // SwapEffect : バッファの入れ替え方式。DirectX 12 では最重要の設定のひとつ。
    //
    //   FLIP_DISCARD (今回採用)
    //     「バッファのポインタを差し替える」方式。中身のコピーが発生しないため
    //     高速で、Windows 10 以降で推奨される方式です。
    //     DISCARD は「表示後に古い内容を保持しない」＝毎フレーム全画面を
    //     描き直す前提、という意味です。
    //
    //   FLIP_SEQUENTIAL
    //     内容を保持する版。部分更新をしたい場合に使います。
    //
    //   DISCARD / SEQUENTIAL（FLIP なし）
    //     DirectX 11 以前の互換用の古い方式。DirectX 12 では使用できません。
    //-------------------------------------------------------------------------
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags     = 0;

    //-------------------------------------------------------------------------
    // (2) スワップチェーンの生成
    //
    //   ★ 第 1 引数がデバイスではなく「コマンドキュー」であることに注目。
    //     DirectX 11 まではデバイスを渡していました。DirectX 12 では
    //     「どのキューの処理が終わったら表示してよいか」を DXGI が知る必要が
    //     あるため、キューを渡す設計に変わっています。
    //     この一点に「明示的な同期」という DX12 の思想がよく表れています。
    //-------------------------------------------------------------------------
    ComPtr<IDXGISwapChain1> swapChain1;
    DX_CHECK(factory->CreateSwapChainForHwnd(
        commandQueue,
        hwnd,
        &swapChainDesc,
        nullptr,      // フルスクリーン設定（nullptr = ウィンドウモード）
        nullptr,      // 出力先モニタの制限（nullptr = 制限なし）
        &swapChain1));

    //-------------------------------------------------------------------------
    // (3) 新しい版のインターフェースへ変換
    //   CreateSwapChainForHwnd が返すのは IDXGISwapChain1 ですが、
    //   GetCurrentBackBufferIndex() を持つのは IDXGISwapChain3 以降です。
    //   As() で「より新しいインターフェースを持っているか」を問い合わせます。
    //-------------------------------------------------------------------------
    DX_CHECK(swapChain1.As(&m_swapChain));

    //-------------------------------------------------------------------------
    // (4) Alt + Enter による自動フルスクリーン切り替えを無効化する
    //
    //   DXGI は既定で Alt+Enter を横取りし、勝手にフルスクリーンにします。
    //   その際スワップチェーンが作り直されるため、こちらが対応していないと
    //   壊れます。学習中は無効にしておくのが安全です。
    //-------------------------------------------------------------------------
    DX_CHECK(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

    //-------------------------------------------------------------------------
    // (5) RTV 用ディスクリプタヒープの生成
    //-------------------------------------------------------------------------
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = kBackBufferCount;

    // Flags : SHADER_VISIBLE を付けると「シェーダーから参照できるヒープ」になる。
    //   RTV は出力先の指定であってシェーダーが読むものではないため NONE。
    //   （テクスチャを読むための SRV ヒープでは SHADER_VISIBLE が必要になります）
    rtvHeapDesc.Flags    = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;

    DX_CHECK(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

    // ディスクリプタ 1 個のサイズを取得しておく（GPU ごとに異なる）
    m_rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    //-------------------------------------------------------------------------
    // (6) バックバッファを取得して RTV を作る
    //-------------------------------------------------------------------------
    CreateRenderTargetViews(device);

    Log(std::format(L"スワップチェーンを生成しました ({} x {}, バッファ {} 枚)",
                    m_width, m_height, kBackBufferCount));
}


/// <summary>バックバッファを取得し、それぞれの RTV をヒープに書き込みます。</summary>
void SwapChain::CreateRenderTargetViews(ID3D12Device* device)
{
    //-------------------------------------------------------------------------
    // ディスクリプタヒープの先頭位置を取得する
    //
    //   D3D12_CPU_DESCRIPTOR_HANDLE は実質的に「メモリアドレスを包んだ構造体」で、
    //   .ptr メンバに SIZE_T のアドレスが入っています。
    //   N 番目の位置を求めるには、先頭アドレスに
    //   「ディスクリプタサイズ × N」を足します（配列の添字計算と同じ考え方）。
    //
    //   CPU ハンドルと GPU ハンドルの違い:
    //     CPU ハンドル … CPU からディスクリプタを「書き込む」ための住所
    //     GPU ハンドル … GPU がディスクリプタを「読む」ための住所
    //   同じディスクリプタを指していても値は別です。RTV の設定は CPU 側で行うため、
    //   ここでは CPU ハンドルを使います。
    //-------------------------------------------------------------------------
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < kBackBufferCount; ++i)
    {
        // スワップチェーンが内部で確保しているバッファへの参照を取得する。
        // 自分で CreateCommittedResource するのではなく「借りる」点がポイント。
        DX_CHECK(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])));

        // RTV を作成して、ヒープの i 番目に書き込む。
        //   第 2 引数 nullptr = 「リソースが持っている形式・設定をそのまま使う」。
        //   形式を変えて解釈したい場合のみ D3D12_RENDER_TARGET_VIEW_DESC を渡します。
        device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtvHandle);

        // 次のディスクリプタの位置へ進める
        rtvHandle.ptr += m_rtvDescriptorSize;
    }

    // 現在のバックバッファ番号を DXGI から取得して同期しておく
    m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}


/// <summary>現在のバックバッファに対応する RTV ディスクリプタの位置を返します。</summary>
D3D12_CPU_DESCRIPTOR_HANDLE SwapChain::CurrentRenderTargetView() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    // 先頭 + サイズ × 現在の番号
    handle.ptr += static_cast<SIZE_T>(m_currentBackBufferIndex) * m_rtvDescriptorSize;

    return handle;
}


/// <summary>画面に表示し、バックバッファを入れ替えます。</summary>
void SwapChain::Present(bool enableVSync)
{
    //-------------------------------------------------------------------------
    // 第 1 引数 SyncInterval : 垂直同期の待ちフレーム数
    //   1 … モニタのリフレッシュ 1 回ぶん待つ（60Hz なら最大 60fps）
    //   0 … 待たない。fps は上がるが、画面の書き換え途中で切り替わるため
    //        「ティアリング（画面の裂け目）」が発生することがある
    //
    // 第 2 引数 Flags : 追加オプション。今回は無し。
    //-------------------------------------------------------------------------
    const UINT syncInterval = enableVSync ? 1 : 0;

    DX_CHECK(m_swapChain->Present(syncInterval, 0));

    //-------------------------------------------------------------------------
    // 表示が終わったので、次に描くべきバッファ番号を更新する。
    //
    //   自分で「(index + 1) % 2」と計算しても大抵は合いますが、
    //   DXGI 側の都合で番号の進み方が変わる場合があるため、
    //   必ず GetCurrentBackBufferIndex() で問い合わせるのが正しい作法です。
    //-------------------------------------------------------------------------
    m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}


/// <summary>ウィンドウサイズ変更に追従してバックバッファを作り直します。</summary>
void SwapChain::Resize(ID3D12Device* device, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return; // 最小化時など。0 サイズでは作り直せない。
    }

    if (width == m_width && height == m_height)
    {
        return; // 変化なし
    }

    //-------------------------------------------------------------------------
    // ★ 重要 : ResizeBuffers を呼ぶ前に、バックバッファへの参照を
    //          「すべて」手放さなければなりません。
    //
    //   ComPtr の Reset() は参照カウントを 1 減らします。
    //   1 つでも参照が残っていると ResizeBuffers は
    //   DXGI_ERROR_INVALID_CALL で失敗します。
    //   （加えて、呼び出し側で GPU の作業完了を待っておくことも必須です）
    //-------------------------------------------------------------------------
    for (auto& backBuffer : m_backBuffers)
    {
        backBuffer.Reset();
    }

    //-------------------------------------------------------------------------
    // バッファを新しいサイズで作り直す
    //   0 を渡した引数は「現在の設定を維持する」という意味になります。
    //-------------------------------------------------------------------------
    DX_CHECK(m_swapChain->ResizeBuffers(
        kBackBufferCount,   // バッファ枚数（0 なら現状維持）
        width,
        height,
        kBackBufferFormat,  // 形式（DXGI_FORMAT_UNKNOWN なら現状維持）
        0));                // フラグ

    m_width  = width;
    m_height = height;

    // 新しいバッファに対して RTV を作り直す
    CreateRenderTargetViews(device);

    Log(std::format(L"スワップチェーンをリサイズしました ({} x {})", m_width, m_height));
}

} // namespace dx12
