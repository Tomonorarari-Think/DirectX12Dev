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

/// @brief Win32 のウィンドウを 1 枚作り、メッセージ処理を行うクラス。
///
/// **このクラスの責務**
///
/// - ウィンドウクラスの登録とウィンドウの生成
/// - OS から届くメッセージ（マウス、キー、サイズ変更、閉じるボタン…）の処理
/// - 「閉じられた」「サイズが変わった」という事実を外へ通知する
///
/// **責務でないもの（意図的に持たせないもの）**
///
/// DirectX に関すること一切。ウィンドウは「絵を描く紙」であって「絵の描き方」は知らなくてよい、とい
/// う考えです。この分離のおかげで、描画側を差し替えてもウィンドウ側は無変更で済みます。
class Window
{
public:
    /// @brief サイズ変更が起きたときに呼ばれるコールバックの型。
    ///
    /// 引数は新しいクライアント領域（枠を除いた描画可能部分）の幅・高さです。
    using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;

    /// @brief 既定のコンストラクタ。ウィンドウはまだ生成されません。
    Window() = default;

    /// @brief ウィンドウを破棄し、登録したウィンドウクラスを解除します。
    ~Window();

    /// @brief コピー構築は禁止です。
    ///
    /// このクラスは HWND という OS リソースを 1 つだけ所有します。コピーされると同じ HWND を 2 回破棄し
    /// ようとして壊れるため、コンパイル時点で禁止しておきます（＝バグの芽を型で潰す）。
    Window(const Window&) = delete;

    /// @brief コピー代入は禁止です。
    Window& operator=(const Window&) = delete;

    /// @brief ウィンドウを生成して表示します。
    /// @param title タイトルバーに表示する文字列。
    /// @param width クライアント領域の幅（ピクセル）。
    /// @param height クライアント領域の高さ（ピクセル）。
    /// @exception std::runtime_error ウィンドウクラスの登録、またはウィンドウの生成に失敗した場合。
    void Create(const std::wstring& title, uint32_t width, uint32_t height);

    /// @brief 溜まっているメッセージをすべて処理します。
    /// @returns `true` なら実行を継続、`false` ならウィンドウが閉じられた（アプリを終了すべき）。
    ///
    /// ゲームやリアルタイム描画では `GetMessage`（メッセージが来るまで待つ）ではなく `PeekMessage`（あ
    /// れば取る／なければ即座に戻る）を使います。待たずに戻ることで、入力が無くても毎フレーム描画を続け
    /// られます。
    bool ProcessMessages();

    /// @brief サイズ変更時に呼ばれる処理を登録します。
    /// @param callback 新しい幅・高さを受け取る関数。
    void SetResizeCallback(ResizeCallback callback) { m_onResize = std::move(callback); }

    /// @brief ウィンドウハンドルを取得します。
    /// @returns 生成済みなら HWND、未生成または破棄済みなら `nullptr`。
    HWND Handle() const noexcept { return m_hwnd; }

    /// @brief 現在のクライアント領域の幅を取得します。
    /// @returns 幅（ピクセル）。
    uint32_t Width() const noexcept { return m_width; }

    /// @brief 現在のクライアント領域の高さを取得します。
    /// @returns 高さ（ピクセル）。
    uint32_t Height() const noexcept { return m_height; }

private:
    /// @brief OS から呼ばれる静的なウィンドウプロシージャ。
    /// @param hwnd メッセージの宛先ウィンドウ。
    /// @param message メッセージ ID（`WM_*`）。
    /// @param wParam メッセージ固有のパラメータ 1。
    /// @param lParam メッセージ固有のパラメータ 2。
    /// @returns メッセージの処理結果。
    ///
    /// **なぜ static なのか**
    ///
    /// Windows は C 言語時代からの API なので、コールバックに「関数ポインタ」しか渡せません。C++ の非静
    /// 的メンバ関数は暗黙の `this` を必要とするため関数ポインタに変換できず、static にする必要がありま
    /// す。
    ///
    /// **ではどうやって this を取り戻すのか**
    ///
    /// `CreateWindowExW` の最後の引数に `this` を渡しておくと、`WM_NCCREATE` メッセージの `lParam` 経由
    /// で受け取れます。それを `SetWindowLongPtrW(GWLP_USERDATA)` でウィンドウに保存し、以降のメッセージ
    /// では `GetWindowLongPtrW` で取り出します。これが Win32 + C++ で定番の「this の運び方」です。
    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    /// @brief インスタンス単位の実際のメッセージ処理。
    /// @param hwnd メッセージの宛先ウィンドウ。
    /// @param message メッセージ ID（`WM_*`）。
    /// @param wParam メッセージ固有のパラメータ 1。
    /// @param lParam メッセージ固有のパラメータ 2。
    /// @returns メッセージの処理結果。
    ///
    /// `hwnd` をメンバ (`m_hwnd`) から取らず引数で受け取っているのは、`WM_DESTROY` 以降 `m_hwnd` を
    /// `nullptr` にクリアするためです。破棄後にも `WM_NCDESTROY` 等が届くので、そこで `DefWindowProcW`
    /// に `nullptr` を渡してしまわないよう、OS から渡された hwnd を使います。
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    /// @brief ウィンドウのハンドル（OS が振る識別子）。
    HWND m_hwnd = nullptr;

    /// @brief 自プロセスのインスタンスハンドル。
    HINSTANCE m_instance = nullptr;

    /// @brief クライアント領域の幅（ピクセル）。
    uint32_t m_width = 0;

    /// @brief クライアント領域の高さ（ピクセル）。
    uint32_t m_height = 0;

    /// @brief 閉じる要求（WM_QUIT）を受け取ったかどうか。
    bool m_shouldClose = false;

    /// @brief サイズ変更の通知先。未設定なら何もしません。
    ResizeCallback m_onResize;
};

} // namespace dx12
