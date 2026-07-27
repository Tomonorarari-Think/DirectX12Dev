//=============================================================================
// Window.h
//   Win32 ウィンドウの生成とメッセージ処理。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

#include <cstdint>
#include <functional>
#include <string>

namespace dx12
{

/// <summary>
/// Win32 のウィンドウを 1 枚作り、メッセージ処理を行うクラス。
/// </summary>
class Window
{
public:
    /// <summary>
    /// サイズ変更が起きたときに呼ばれるコールバックの型。
    /// </summary>
    using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;

    /// <summary>
    /// 既定のコンストラクタ。ウィンドウはまだ生成されません。
    /// </summary>
    Window() = default;

    /// <summary>
    /// ウィンドウを破棄し、登録したウィンドウクラスを解除します。
    /// </summary>
    ~Window();

    /// <summary>
    /// コピー構築は禁止です。
    /// </summary>
    Window(const Window&) = delete;

    /// <summary>
    /// コピー代入は禁止です。
    /// </summary>
    Window& operator=(const Window&) = delete;

    /// <summary>
    /// ウィンドウを生成して表示します。
    /// </summary>
    /// <param name="title">タイトルバーに表示する文字列。</param>
    /// <param name="width">クライアント領域の幅（ピクセル）。</param>
    /// <param name="height">クライアント領域の高さ（ピクセル）。</param>
    /// <exception cref="std::runtime_error">
    /// ウィンドウクラスの登録、またはウィンドウの生成に失敗した場合。
    /// </exception>
    void Create(const std::wstring& title, uint32_t width, uint32_t height);

    /// <summary>
    /// 溜まっているメッセージをすべて処理します。
    /// </summary>
    /// <returns>
    /// `true` なら実行を継続、`false` ならウィンドウが閉じられた（アプリを終了すべき）。
    /// </returns>
    bool ProcessMessages();

    /// <summary>
    /// サイズ変更時に呼ばれる処理を登録します。
    /// </summary>
    /// <param name="callback">新しい幅・高さを受け取る関数。</param>
    void SetResizeCallback(ResizeCallback callback) { m_onResize = std::move(callback); }

    /// <summary>
    /// ウィンドウハンドルを取得します。
    /// </summary>
    /// <returns>生成済みなら HWND、未生成または破棄済みなら `nullptr`。</returns>
    HWND Handle() const noexcept { return m_hwnd; }

    /// <summary>
    /// 現在のクライアント領域の幅を取得します。
    /// </summary>
    /// <returns>幅（ピクセル）。</returns>
    uint32_t Width() const noexcept { return m_width; }

    /// <summary>
    /// 現在のクライアント領域の高さを取得します。
    /// </summary>
    /// <returns>高さ（ピクセル）。</returns>
    uint32_t Height() const noexcept { return m_height; }

private:
    /// <summary>
    /// OS から呼ばれる静的なウィンドウプロシージャ。
    /// </summary>
    /// <param name="hwnd">メッセージの宛先ウィンドウ。</param>
    /// <param name="message">メッセージ ID（`WM_*`）。</param>
    /// <param name="wParam">メッセージ固有のパラメータ 1。</param>
    /// <param name="lParam">メッセージ固有のパラメータ 2。</param>
    /// <returns>メッセージの処理結果。</returns>
    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    /// <summary>
    /// インスタンス単位の実際のメッセージ処理。
    /// </summary>
    /// <param name="hwnd">メッセージの宛先ウィンドウ。</param>
    /// <param name="message">メッセージ ID（`WM_*`）。</param>
    /// <param name="wParam">メッセージ固有のパラメータ 1。</param>
    /// <param name="lParam">メッセージ固有のパラメータ 2。</param>
    /// <returns>メッセージの処理結果。</returns>
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    /// <summary>
    /// ウィンドウのハンドル（OS が振る識別子）。
    /// </summary>
    HWND m_hwnd = nullptr;

    /// <summary>
    /// 自プロセスのインスタンスハンドル。
    /// </summary>
    HINSTANCE m_instance = nullptr;

    /// <summary>
    /// クライアント領域の幅（ピクセル）。
    /// </summary>
    uint32_t m_width = 0;

    /// <summary>
    /// クライアント領域の高さ（ピクセル）。
    /// </summary>
    uint32_t m_height = 0;

    /// <summary>
    /// 閉じる要求（WM_QUIT）を受け取ったかどうか。
    /// </summary>
    bool m_shouldClose = false;

    /// <summary>
    /// サイズ変更の通知先。未設定なら何もしません。
    /// </summary>
    ResizeCallback m_onResize;
};

} // namespace dx12
