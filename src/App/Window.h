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
/// <remarks>
/// <para>
/// <b>このクラスの責務</b>
/// <list type="bullet">
///   <item>ウィンドウクラスの登録とウィンドウの生成</item>
///   <item>OS から届くメッセージ（マウス、キー、サイズ変更、閉じるボタン…）の処理</item>
///   <item>「閉じられた」「サイズが変わった」という事実を外へ通知する</item>
/// </list>
/// </para>
/// <para>
/// <b>責務でないもの（意図的に持たせないもの）</b><br/>
/// DirectX に関すること一切。
/// ウィンドウは「絵を描く紙」であって「絵の描き方」は知らなくてよい、という考えです。
/// この分離のおかげで、描画側を差し替えてもウィンドウ側は無変更で済みます。
/// </para>
/// </remarks>
class Window
{
public:
    /// <summary>
    /// サイズ変更が起きたときに呼ばれるコールバックの型。
    /// </summary>
    /// <remarks>
    /// 引数は新しいクライアント領域（枠を除いた描画可能部分）の幅・高さです。
    /// </remarks>
    using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;

    /// <summary>既定のコンストラクタ。ウィンドウはまだ生成されません。</summary>
    Window() = default;

    /// <summary>ウィンドウを破棄し、登録したウィンドウクラスを解除します。</summary>
    ~Window();

    /// <summary>コピー構築は禁止です。</summary>
    /// <remarks>
    /// このクラスは HWND という OS リソースを 1 つだけ所有します。
    /// コピーされると同じ HWND を 2 回破棄しようとして壊れるため、
    /// コンパイル時点で禁止しておきます（＝バグの芽を型で潰す）。
    /// </remarks>
    Window(const Window&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    Window& operator=(const Window&) = delete;

    /// <summary>ウィンドウを生成して表示します。</summary>
    /// <param name="title">タイトルバーに表示する文字列。</param>
    /// <param name="width">クライアント領域の幅（ピクセル）。</param>
    /// <param name="height">クライアント領域の高さ（ピクセル）。</param>
    /// <exception cref="std::runtime_error">
    /// ウィンドウクラスの登録、またはウィンドウの生成に失敗した場合。
    /// </exception>
    void Create(const std::wstring& title, uint32_t width, uint32_t height);

    /// <summary>溜まっているメッセージをすべて処理します。</summary>
    /// <returns>
    /// <c>true</c> なら実行を継続、
    /// <c>false</c> ならウィンドウが閉じられた（アプリを終了すべき）。
    /// </returns>
    /// <remarks>
    /// ゲームやリアルタイム描画では <c>GetMessage</c>（メッセージが来るまで待つ）
    /// ではなく <c>PeekMessage</c>（あれば取る／なければ即座に戻る）を使います。
    /// 待たずに戻ることで、入力が無くても毎フレーム描画を続けられます。
    /// </remarks>
    bool ProcessMessages();

    /// <summary>サイズ変更時に呼ばれる処理を登録します。</summary>
    /// <param name="callback">新しい幅・高さを受け取る関数。</param>
    void SetResizeCallback(ResizeCallback callback) { m_onResize = std::move(callback); }

    /// <summary>ウィンドウハンドルを取得します。</summary>
    /// <returns>生成済みなら HWND、未生成または破棄済みなら <c>nullptr</c>。</returns>
    HWND Handle() const noexcept { return m_hwnd; }

    /// <summary>現在のクライアント領域の幅を取得します。</summary>
    /// <returns>幅（ピクセル）。</returns>
    uint32_t Width() const noexcept { return m_width; }

    /// <summary>現在のクライアント領域の高さを取得します。</summary>
    /// <returns>高さ（ピクセル）。</returns>
    uint32_t Height() const noexcept { return m_height; }

private:
    /// <summary>OS から呼ばれる静的なウィンドウプロシージャ。</summary>
    /// <param name="hwnd">メッセージの宛先ウィンドウ。</param>
    /// <param name="message">メッセージ ID（<c>WM_*</c>）。</param>
    /// <param name="wParam">メッセージ固有のパラメータ 1。</param>
    /// <param name="lParam">メッセージ固有のパラメータ 2。</param>
    /// <returns>メッセージの処理結果。</returns>
    /// <remarks>
    /// <para>
    /// <b>なぜ static なのか</b><br/>
    /// Windows は C 言語時代からの API なので、コールバックに「関数ポインタ」
    /// しか渡せません。C++ の非静的メンバ関数は暗黙の <c>this</c> を必要とするため
    /// 関数ポインタに変換できず、static にする必要があります。
    /// </para>
    /// <para>
    /// <b>ではどうやって this を取り戻すのか</b><br/>
    /// <c>CreateWindowExW</c> の最後の引数に <c>this</c> を渡しておくと、
    /// <c>WM_NCCREATE</c> メッセージの <c>lParam</c> 経由で受け取れます。
    /// それを <c>SetWindowLongPtrW(GWLP_USERDATA)</c> でウィンドウに保存し、
    /// 以降のメッセージでは <c>GetWindowLongPtrW</c> で取り出します。
    /// これが Win32 + C++ で定番の「this の運び方」です。
    /// </para>
    /// </remarks>
    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    /// <summary>インスタンス単位の実際のメッセージ処理。</summary>
    /// <param name="hwnd">メッセージの宛先ウィンドウ。</param>
    /// <param name="message">メッセージ ID（<c>WM_*</c>）。</param>
    /// <param name="wParam">メッセージ固有のパラメータ 1。</param>
    /// <param name="lParam">メッセージ固有のパラメータ 2。</param>
    /// <returns>メッセージの処理結果。</returns>
    /// <remarks>
    /// <paramref name="hwnd"/> をメンバ (<c>m_hwnd</c>) から取らず引数で受け取っているのは、
    /// <c>WM_DESTROY</c> 以降 <c>m_hwnd</c> を <c>nullptr</c> にクリアするためです。
    /// 破棄後にも <c>WM_NCDESTROY</c> 等が届くので、そこで <c>DefWindowProcW</c> に
    /// <c>nullptr</c> を渡してしまわないよう、OS から渡された hwnd を使います。
    /// </remarks>
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    /// <summary>ウィンドウのハンドル（OS が振る識別子）。</summary>
    HWND m_hwnd = nullptr;

    /// <summary>自プロセスのインスタンスハンドル。</summary>
    HINSTANCE m_instance = nullptr;

    /// <summary>クライアント領域の幅（ピクセル）。</summary>
    uint32_t m_width = 0;

    /// <summary>クライアント領域の高さ（ピクセル）。</summary>
    uint32_t m_height = 0;

    /// <summary>閉じる要求（WM_QUIT）を受け取ったかどうか。</summary>
    bool m_shouldClose = false;

    /// <summary>サイズ変更の通知先。未設定なら何もしません。</summary>
    ResizeCallback m_onResize;
};

} // namespace dx12
