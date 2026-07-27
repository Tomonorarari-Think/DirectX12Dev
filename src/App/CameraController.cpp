//=============================================================================
// CameraController.cpp
//   CameraController の実装。
//=============================================================================
#include "CameraController.h"

#include "Camera.h"
#include "Input.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace dx12
{
namespace
{
/// <summary>マウス 1 ピクセルあたりの回転量（ラジアン）。</summary>
constexpr float kRadiansPerPixel = 0.006f;

/// <summary>キー操作での回転速度（ラジアン毎秒）。</summary>
constexpr float kRadiansPerSecond = 1.6f;

/// <summary>ホイール 1 段あたりの寄り引き量（距離に対する割合）。</summary>
constexpr float kZoomPerWheelStep = 0.12f;

/// <summary>マウス 1 ピクセルあたりの平行移動量（距離 1 のときの値）。</summary>
constexpr float kPanPerPixel = 0.0016f;

/// <summary>寄れる限界。0 にすると注視点と重なって行列が壊れます。</summary>
constexpr float kMinDistance = 1.2f;

/// <summary>引ける限界。</summary>
constexpr float kMaxDistance = 24.0f;

/// <summary>
/// 仰角の限界。真上・真下ちょうどは避けます。
/// </summary>
/// <remarks>
/// 視線と上方向が平行になると `XMMatrixLookAtLH` が破綻し、画面が消えます。
/// </remarks>
constexpr float kPitchLimit = 1.52f;   // 約 87 度

/// <summary>初期状態の値。`Reset` で戻す先。</summary>
constexpr float kInitialYaw      = 0.0f;
constexpr float kInitialPitch    = 0.42f;
constexpr float kInitialDistance = 5.4f;
constexpr XMFLOAT3 kInitialTarget = { 0.0f, 0.55f, 0.0f };
} // namespace


/// <summary>
/// 入力に応じてカメラを更新します。
/// </summary>
void CameraController::Update(const Input& input, Camera& camera, float deltaSeconds)
{
    // (1) 左ドラッグで回転
    //   移動量はピクセル数なので、経過時間を掛けてはいけない。
    //   「1 ピクセル動かしたら必ず同じだけ回る」ほうが操作しやすいため。
    if (input.IsMouseButtonDown(MouseButton::Left))
    {
        m_yaw   += input.MouseDeltaX() * kRadiansPerPixel;
        m_pitch += input.MouseDeltaY() * kRadiansPerPixel;
    }

    // (2) キーでも回せるようにする
    //   こちらは押しっぱなしで動き続けるので、経過時間を掛ける。
    //   掛けないと、FPS が高い環境ほど速く回ってしまう。
    const float keyRotation = kRadiansPerSecond * deltaSeconds;

    if (input.IsKeyDown('A')) { m_yaw   -= keyRotation; }
    if (input.IsKeyDown('D')) { m_yaw   += keyRotation; }
    if (input.IsKeyDown('W')) { m_pitch += keyRotation; }
    if (input.IsKeyDown('S')) { m_pitch -= keyRotation; }

    // (3) 仰角を制限する
    //   真上・真下を通り越すと、上下が裏返って操作不能になる。
    m_pitch = std::clamp(m_pitch, -kPitchLimit, kPitchLimit);

    // (4) ホイールで寄り引き
    //   距離に対する割合で変えると、遠くでは大きく、近くでは細かく動く。
    if (input.WheelDelta() != 0.0f)
    {
        m_distance *= std::pow(1.0f - kZoomPerWheelStep, input.WheelDelta());
        m_distance = std::clamp(m_distance, kMinDistance, kMaxDistance);
    }

    // (5) 右ドラッグ・中ドラッグで注視点ごと平行移動
    if (input.IsMouseButtonDown(MouseButton::Right) ||
        input.IsMouseButtonDown(MouseButton::Middle))
    {
        // 画面の右方向と上方向を、カメラの向きから求める。
        const float sinYaw = std::sin(m_yaw);
        const float cosYaw = std::cos(m_yaw);

        const XMVECTOR right = XMVectorSet(cosYaw, 0.0f, -sinYaw, 0.0f);
        const XMVECTOR up    = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        // 距離に比例させると、引いているときほど大きく動いて操作感が揃う。
        const float scale = kPanPerPixel * m_distance;

        XMVECTOR target = XMLoadFloat3(&m_target);
        target = XMVectorSubtract(target, XMVectorScale(right, input.MouseDeltaX() * scale));
        target = XMVectorAdd(target, XMVectorScale(up, input.MouseDeltaY() * scale));

        XMStoreFloat3(&m_target, target);
    }

    // (6) R キーで初期状態へ戻す
    //   押しっぱなしで連続実行されても困らないが、意図を明確にするため
    //   「押された瞬間」で判定する。
    if (input.WasKeyPressed('R'))
    {
        Reset();
    }

    ApplyToCamera(camera);
}


/// <summary>
/// 視点と注視点を初期状態へ戻します。
/// </summary>
void CameraController::Reset()
{
    m_target   = kInitialTarget;
    m_yaw      = kInitialYaw;
    m_pitch    = kInitialPitch;
    m_distance = kInitialDistance;
}


/// <summary>
/// 方位角・仰角・距離から視点の位置を求め、カメラへ反映します。
/// </summary>
void CameraController::ApplyToCamera(Camera& camera) const
{
    // 極座標から直交座標へ。注視点から見て、方位角と仰角の向きに距離ぶん離れた点。
    const float cosPitch = std::cos(m_pitch);

    const float offsetX = m_distance * cosPitch * std::sin(m_yaw);
    const float offsetY = m_distance * std::sin(m_pitch);
    const float offsetZ = m_distance * cosPitch * std::cos(m_yaw);

    // 手前（-Z 側）から見るのが初期状態なので、Z は引く向きにする。
    camera.SetPosition(m_target.x - offsetX, m_target.y + offsetY, m_target.z - offsetZ);
    camera.SetTarget(m_target.x, m_target.y, m_target.z);
}

} // namespace dx12
