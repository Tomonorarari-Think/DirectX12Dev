//=============================================================================
// Window.cpp
//   Window クラスの実装。Win32 API でウィンドウを 1 枚作る。
//=============================================================================
#include "Window.h"

#include <format>

namespace dx12
{
namespace
{
// ウィンドウクラス名。OS 内部でウィンドウの「種類」を識別するための名前で、
// 画面には表示されません。他アプリと衝突しないよう固有の名前にします。
constexpr const wchar_t* kWindowClassName = L"DirectX12DevWindowClass";
} // namespace


//-----------------------------------------------------------------------------
// デストラクタ
//-----------------------------------------------------------------------------
Window::~Window()
{
    if (m_hwnd != nullptr)
    {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    // 登録したウィンドウクラスも解除しておく。
    // （プロセス終了時に OS が自動で片付けてくれるが、明示するのが行儀が良い）
    if (m_instance != nullptr)
    {
        ::UnregisterClassW(kWindowClassName, m_instance);
        m_instance = nullptr;
    }
}


//-----------------------------------------------------------------------------
// Create : ウィンドウクラスを登録し、ウィンドウを生成して表示する
//-----------------------------------------------------------------------------
void Window::Create(const std::wstring& title, uint32_t width, uint32_t height)
{
    m_width  = width;
    m_height = height;

    // GetModuleHandleW(nullptr) で「自分自身の実行ファイル」のハンドルを得る。
    // Win32 ではウィンドウクラスが「どのモジュールのものか」を要求されるため必要。
    m_instance = ::GetModuleHandleW(nullptr);

    //-------------------------------------------------------------------------
    // (1) ウィンドウクラスの登録
    //
    //   「ウィンドウクラス」はウィンドウの設計図です。
    //   どのプロシージャでメッセージを処理するか、カーソルは何か、といった
    //   共通の性質をここで一度だけ登録し、その設計図から実体を作ります。
    //-------------------------------------------------------------------------
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);

    // CS_HREDRAW | CS_VREDRAW : 幅／高さが変わったらウィンドウ全体を再描画する
    // CS_OWNDC                : このウィンドウ専用のデバイスコンテキストを持つ
    //                           （描画系アプリの慣例。DirectX では必須ではない）
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;

    windowClass.lpfnWndProc   = StaticWindowProc;      // メッセージ処理関数
    windowClass.hInstance     = m_instance;
    windowClass.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClassName;

    // 背景ブラシは設定しない（nullptr）。
    //   理由: 毎フレーム DirectX がウィンドウ全体を塗り替えるため、
    //         OS 側が背景を塗ると「ちらつき」の原因になるだけで無駄。
    windowClass.hbrBackground = nullptr;

    if (::RegisterClassExW(&windowClass) == 0)
    {
        throw std::runtime_error("ウィンドウクラスの登録に失敗しました。");
    }

    //-------------------------------------------------------------------------
    // (2) ウィンドウサイズの調整
    //
    //   CreateWindowExW に渡すサイズは「タイトルバーや枠を含んだ外形サイズ」です。
    //   しかし我々が欲しいのは「描画に使えるクライアント領域が 1280x720」。
    //   AdjustWindowRect で、希望のクライアント領域に対して必要な外形サイズを
    //   逆算してもらいます。これを忘れると描画領域が枠のぶん小さくなります。
    //-------------------------------------------------------------------------
    // WS_OVERLAPPEDWINDOW = タイトルバー + 最小化/最大化ボタン + サイズ変更枠
    const DWORD windowStyle = WS_OVERLAPPEDWINDOW;

    RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    ::AdjustWindowRect(&windowRect, windowStyle, FALSE /* メニューバーなし */);

    const int adjustedWidth  = windowRect.right - windowRect.left;
    const int adjustedHeight = windowRect.bottom - windowRect.top;

    //-------------------------------------------------------------------------
    // (3) ウィンドウの生成
    //
    //   最後の引数に this を渡しているのが重要なポイント。
    //   これが WM_NCCREATE の lParam 経由で StaticWindowProc に届き、
    //   「static 関数からインスタンスを取り戻す」ための鍵になります。
    //-------------------------------------------------------------------------
    m_hwnd = ::CreateWindowExW(
        0,                   // 拡張スタイル（今回は無し）
        kWindowClassName,    // 使用するウィンドウクラス
        title.c_str(),       // タイトルバーの文字列
        windowStyle,
        CW_USEDEFAULT,       // 表示位置 X（OS におまかせ）
        CW_USEDEFAULT,       // 表示位置 Y（OS におまかせ）
        adjustedWidth,
        adjustedHeight,
        nullptr,             // 親ウィンドウ（無し）
        nullptr,             // メニュー（無し）
        m_instance,
        this);               // ★ WM_NCCREATE へ渡される任意データ

    if (m_hwnd == nullptr)
    {
        throw std::runtime_error("ウィンドウの生成に失敗しました。");
    }

    // (4) 画面に表示して、最前面に持ってくる
    ::ShowWindow(m_hwnd, SW_SHOW);
    ::UpdateWindow(m_hwnd);

    Log(std::format(L"ウィンドウを生成しました ({} x {})", m_width, m_height));
}


//-----------------------------------------------------------------------------
// ProcessMessages : メッセージキューを空になるまで処理する
//-----------------------------------------------------------------------------
bool Window::ProcessMessages()
{
    MSG message = {};

    // PM_REMOVE : 取り出したメッセージをキューから削除する
    //             （PM_NOREMOVE にすると覗くだけで消えず、無限ループになる）
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            // WM_QUIT はウィンドウ宛てではなく「スレッド宛て」に届く特別なメッセージ。
            // DispatchMessage しても WndProc には渡らないので、ここで直接判定する。
            m_shouldClose = true;
            return false;
        }

        // TranslateMessage : キー入力（WM_KEYDOWN）を文字入力（WM_CHAR）に変換する
        ::TranslateMessage(&message);

        // DispatchMessage : メッセージを対象ウィンドウのプロシージャへ配送する
        //                   → StaticWindowProc が呼ばれる
        ::DispatchMessageW(&message);
    }

    return !m_shouldClose;
}


//-----------------------------------------------------------------------------
// StaticWindowProc : OS から呼ばれる静的コールバック
//   「this の取り戻し」だけを行い、実処理は HandleMessage に委譲する。
//-----------------------------------------------------------------------------
LRESULT CALLBACK Window::StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // WM_NCCREATE はウィンドウ生成時に最初に届くメッセージ群のひとつ。
    // このタイミングでのみ、CreateWindowExW に渡した this を受け取れる。
    if (message == WM_NCCREATE)
    {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<Window*>(createStruct->lpCreateParams);

        // ウィンドウに紐づく 1 ポインタぶんの保管領域（GWLP_USERDATA）に this を保存
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

        // HWND はまだメンバに入っていないので、ここで入れておく
        self->m_hwnd = hwnd;
    }

    // 保存しておいた this を取り出す
    auto* self = reinterpret_cast<Window*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (self != nullptr)
    {
        return self->HandleMessage(hwnd, message, wParam, lParam);
    }

    // WM_NCCREATE より前に届くメッセージ（WM_GETMINMAXINFO 等）は
    // this がまだ無いので、OS の既定処理に任せる。
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}


//-----------------------------------------------------------------------------
// HandleMessage : 実際のメッセージ処理
//-----------------------------------------------------------------------------
LRESULT Window::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    //-------------------------------------------------------------------------
    // WM_CLOSE : 「×」ボタンが押された、Alt+F4 が押された
    //   ここで DestroyWindow を呼ぶと WM_DESTROY が飛んでくる。
    //-------------------------------------------------------------------------
    case WM_CLOSE:
        ::DestroyWindow(hwnd);
        return 0;

    //-------------------------------------------------------------------------
    // WM_DESTROY : ウィンドウが破棄された
    //   PostQuitMessage でスレッドのメッセージキューに WM_QUIT を積む。
    //   これを ProcessMessages が拾ってループを終了させる。
    //-------------------------------------------------------------------------
    case WM_DESTROY:
        m_hwnd = nullptr;   // 破棄済みなので二重に DestroyWindow しないようクリア
        ::PostQuitMessage(0);
        return 0;

    //-------------------------------------------------------------------------
    // WM_SIZE : ウィンドウのサイズが変わった
    //   lParam の下位 16bit に新しい幅、上位 16bit に新しい高さが入っている。
    //-------------------------------------------------------------------------
    case WM_SIZE:
    {
        // 最小化されると幅・高さが 0 になる。
        // 0 サイズでスワップチェーンを作り直すとエラーになるため無視する。
        if (wParam == SIZE_MINIMIZED)
        {
            return 0;
        }

        const uint32_t newWidth  = static_cast<uint32_t>(LOWORD(lParam));
        const uint32_t newHeight = static_cast<uint32_t>(HIWORD(lParam));

        if (newWidth == 0 || newHeight == 0)
        {
            return 0;
        }

        // 実際に値が変わったときだけ通知する（同じサイズでの連続通知を防ぐ）
        if (newWidth != m_width || newHeight != m_height)
        {
            m_width  = newWidth;
            m_height = newHeight;

            if (m_onResize)
            {
                m_onResize(m_width, m_height);
            }
        }
        return 0;
    }

    //-------------------------------------------------------------------------
    // WM_KEYDOWN : キーが押された
    //   学習中は ESC で即終了できると便利なので用意しておく。
    //-------------------------------------------------------------------------
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            ::DestroyWindow(hwnd);
        }
        return 0;

    //-------------------------------------------------------------------------
    // WM_PAINT : 再描画要求
    //   DirectX 側でメインループから毎フレーム描いているため、ここでは
    //   「描画要求を処理済みにする」だけでよい。
    //   BeginPaint/EndPaint を呼ばないと OS が延々と WM_PAINT を送り続ける。
    //-------------------------------------------------------------------------
    case WM_PAINT:
    {
        PAINTSTRUCT paint = {};
        ::BeginPaint(hwnd, &paint);
        ::EndPaint(hwnd, &paint);
        return 0;
    }

    default:
        break;
    }

    // 自分で処理しないメッセージは、必ず OS の既定処理に渡す。
    // これを忘れるとウィンドウの移動やサイズ変更ができなくなる。
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace dx12
