//=============================================================================
// Input.cpp
//   Input の実装。
//=============================================================================
#include "Input.h"

#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

namespace dx12
{
namespace
{
/// <summary>
/// ホイール 1 段ぶんの回転量（Win32 の定義）。
/// </summary>
constexpr float kWheelStep = static_cast<float>(WHEEL_DELTA);
} // namespace


/// <summary>
/// ウィンドウメッセージを受け取り、状態を更新します。
/// </summary>
bool Input::ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    // キーの押し下げ。押しっぱなしだと OS が繰り返し送ってくるが、
    // 状態を立てるだけなので何度来ても問題ない。
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam < kKeyCount)
        {
            m_currentKeys.set(wParam);
        }
        return true;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam < kKeyCount)
        {
            m_currentKeys.reset(wParam);
        }
        return true;

    case WM_LBUTTONDOWN:
        m_mouseButtons[static_cast<size_t>(MouseButton::Left)] = true;
        return true;

    case WM_LBUTTONUP:
        m_mouseButtons[static_cast<size_t>(MouseButton::Left)] = false;
        return true;

    case WM_RBUTTONDOWN:
        m_mouseButtons[static_cast<size_t>(MouseButton::Right)] = true;
        return true;

    case WM_RBUTTONUP:
        m_mouseButtons[static_cast<size_t>(MouseButton::Right)] = false;
        return true;

    case WM_MBUTTONDOWN:
        m_mouseButtons[static_cast<size_t>(MouseButton::Middle)] = true;
        return true;

    case WM_MBUTTONUP:
        m_mouseButtons[static_cast<size_t>(MouseButton::Middle)] = false;
        return true;

    // マウス移動。届くのは「位置」なので、前回との差を自分で取る。
    case WM_MOUSEMOVE:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        // 最初の 1 回は差を取らない。原点からの巨大な移動として扱わないため。
        if (m_hasMousePosition)
        {
            m_mouseDeltaX += static_cast<float>(x - m_lastMouseX);
            m_mouseDeltaY += static_cast<float>(y - m_lastMouseY);
        }

        m_lastMouseX       = x;
        m_lastMouseY       = y;
        m_hasMousePosition = true;
        return true;
    }

    // ホイール。wParam の上位ワードに回転量が入っている。
    case WM_MOUSEWHEEL:
        m_wheelDelta += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / kWheelStep;
        return true;

    // ウィンドウがフォーカスを失ったら、押しっぱなしの状態を全て解除する。
    // これをしないと、Alt+Tab で切り替えたときにキーが押されたままになる。
    case WM_KILLFOCUS:
        m_currentKeys.reset();
        m_mouseButtons.fill(false);
        m_hasMousePosition = false;
        return true;

    default:
        return false;
    }
}


/// <summary>
/// 1 フレームぶんの区切りを付けます。
/// </summary>
void Input::NewFrame()
{
    // 「押された瞬間」を判定するために、1 フレーム前の状態を残しておく。
    m_previousKeys = m_currentKeys;

    // 差分は 1 フレームで使い切る値なので、毎回 0 に戻す。
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    m_wheelDelta  = 0.0f;
}


/// <summary>
/// キーが今押されているかを返します。
/// </summary>
bool Input::IsKeyDown(int virtualKey) const
{
    if (virtualKey < 0 || static_cast<size_t>(virtualKey) >= kKeyCount)
    {
        return false;
    }
    return m_currentKeys.test(static_cast<size_t>(virtualKey));
}


/// <summary>
/// キーがこのフレームで押されたかを返します。
/// </summary>
bool Input::WasKeyPressed(int virtualKey) const
{
    if (virtualKey < 0 || static_cast<size_t>(virtualKey) >= kKeyCount)
    {
        return false;
    }

    const size_t index = static_cast<size_t>(virtualKey);

    // 今は押されていて、前フレームは離れていた = このフレームで押された。
    return m_currentKeys.test(index) && !m_previousKeys.test(index);
}


/// <summary>
/// マウスのボタンが今押されているかを返します。
/// </summary>
bool Input::IsMouseButtonDown(MouseButton button) const
{
    return m_mouseButtons[static_cast<size_t>(button)];
}

} // namespace dx12
