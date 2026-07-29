//=============================================================================
// Input.h
//   キーボードとマウスの状態を溜めておくクラス。
//
//   ウィンドウプロシージャは「いつ呼ばれるか分からない」ので、
//   そこで処理をせず状態を記録するだけにし、実際の判断は毎フレーム行う。
//   詳しい解説は docs/tutorial/14_操作する_入力とカメラ.md を参照。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

#include <array>
#include <bitset>
#include <cstdint>

namespace dx12
{

/// <summary>
/// マウスのボタン。
/// </summary>
enum class MouseButton : uint32_t
{
    /// <summary>左ボタン。</summary>
    Left = 0,

    /// <summary>右ボタン。</summary>
    Right = 1,

    /// <summary>中ボタン（ホイールの押し込み）。</summary>
    Middle = 2,

    /// <summary>ボタンの総数。配列の大きさに使います。</summary>
    Count = 3,
};


/// <summary>
/// キーボードとマウスの入力状態を保持するクラス。
/// </summary>
/// <remarks>
/// DirectX に依存しないため `App/` に置いています。Win32 のメッセージだけを
/// 知っており、それをどう解釈するかは利用側（`CameraController` など）の仕事です。
/// </remarks>
class Input
{
public:
    /// <summary>扱う仮想キーコードの数（`VK_*` は 0〜255）。</summary>
    static constexpr size_t kKeyCount = 256;

    /// <summary>既定のコンストラクタ。すべて「押されていない」状態から始まります。</summary>
    Input() = default;

    /// <summary>
    /// ウィンドウメッセージを受け取り、状態を更新します。
    /// </summary>
    /// <param name="message">メッセージ ID（`WM_*`）。</param>
    /// <param name="wParam">メッセージ固有のパラメータ 1。</param>
    /// <param name="lParam">メッセージ固有のパラメータ 2。</param>
    /// <returns>入力として処理したなら `true`。</returns>
    /// <remarks>
    /// ここでは記録するだけで、ゲームの処理は一切行いません。
    /// メッセージは「1 フレームに何回来るか分からない」ためです。
    /// </remarks>
    bool ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam);

    /// <summary>
    /// 1 フレームぶんの区切りを付けます。メッセージを処理する前に呼びます。
    /// </summary>
    /// <remarks>
    /// 現在の状態を「前フレームの状態」へ移し、差分（移動量やホイール）を 0 に戻します。
    /// これを呼ばないと「押された瞬間」の判定ができません。
    /// </remarks>
    void NewFrame();

    /// <summary>
    /// キーが今押されているかを返します。
    /// </summary>
    /// <param name="virtualKey">仮想キーコード（`'W'` や `VK_SHIFT` など）。</param>
    /// <returns>押されていれば `true`。</returns>
    bool IsKeyDown(int virtualKey) const;

    /// <summary>
    /// キーがこのフレームで押されたかを返します。
    /// </summary>
    /// <param name="virtualKey">仮想キーコード。</param>
    /// <returns>前フレームは離れていて、今フレームで押されたなら `true`。</returns>
    /// <remarks>
    /// 「押しっぱなしで動き続ける」操作には `IsKeyDown`、
    /// 「1 回押すと 1 回だけ起きる」操作にはこちらを使います。
    /// </remarks>
    bool WasKeyPressed(int virtualKey) const;

    /// <summary>
    /// マウスのボタンが今押されているかを返します。
    /// </summary>
    /// <param name="button">調べるボタン。</param>
    /// <returns>押されていれば `true`。</returns>
    bool IsMouseButtonDown(MouseButton button) const;

    /// <summary>このフレームでのマウスの横移動量（ピクセル）。</summary>
    /// <returns>右向きが正。</returns>
    /// <summary>マウスの X 座標（クライアント領域のピクセル）を返します。</summary>
    /// <returns>最後に受け取った位置。</returns>
    float MouseX() const noexcept { return static_cast<float>(m_lastMouseX); }

    /// <summary>マウスの Y 座標（クライアント領域のピクセル）を返します。</summary>
    /// <returns>最後に受け取った位置。</returns>
    float MouseY() const noexcept { return static_cast<float>(m_lastMouseY); }

    float MouseDeltaX() const noexcept { return m_mouseDeltaX; }

    /// <summary>このフレームでのマウスの縦移動量（ピクセル）。</summary>
    /// <returns>下向きが正（画面座標に合わせています）。</returns>
    float MouseDeltaY() const noexcept { return m_mouseDeltaY; }

    /// <summary>このフレームでのホイールの回転量。</summary>
    /// <returns>手前から奥へ回すと正。1 段が 1.0。</returns>
    float WheelDelta() const noexcept { return m_wheelDelta; }

private:
    /// <summary>キーが今押されているか。</summary>
    std::bitset<kKeyCount> m_currentKeys;

    /// <summary>前フレームで押されていたか。</summary>
    std::bitset<kKeyCount> m_previousKeys;

    /// <summary>マウスのボタンが今押されているか。</summary>
    std::array<bool, static_cast<size_t>(MouseButton::Count)> m_mouseButtons = {};

    /// <summary>直前に受け取ったマウス位置（クライアント座標）。</summary>
    int m_lastMouseX = 0;

    /// <summary>直前に受け取ったマウス位置（クライアント座標）。</summary>
    int m_lastMouseY = 0;

    /// <summary>まだ一度もマウス位置を受け取っていないか。</summary>
    /// <remarks>
    /// 最初の 1 回で「前回位置 0,0 からの移動」と誤認して視点が飛ぶのを防ぎます。
    /// </remarks>
    bool m_hasMousePosition = false;

    /// <summary>このフレームでの横移動量の合計。</summary>
    float m_mouseDeltaX = 0.0f;

    /// <summary>このフレームでの縦移動量の合計。</summary>
    float m_mouseDeltaY = 0.0f;

    /// <summary>このフレームでのホイール回転量の合計。</summary>
    float m_wheelDelta = 0.0f;
};

} // namespace dx12
