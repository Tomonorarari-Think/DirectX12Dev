//=============================================================================
// CameraController.h
//   入力を受けてカメラを動かす。注視点のまわりを回る「軌道カメラ」。
//
//   Camera が「どこから何を見るか」を持つのに対し、
//   こちらは「操作でそれをどう変えるか」を持つ。
//=============================================================================
#pragma once

#include <DirectXMath.h>

namespace dx12
{
class Camera;
class Input;

/// <summary>
/// 注視点のまわりを回り、寄り引きできるカメラ操作。
/// </summary>
/// <remarks>
/// 視点の位置を直接持たず、**注視点からの方位角・仰角・距離**で持ちます。
/// 3 つの値から位置を計算するので、どう動かしても注視点を見失いません。
/// </remarks>
class CameraController
{
public:
    /// <summary>既定のコンストラクタ。既定の画角と距離を設定します。</summary>
    CameraController() = default;

    /// <summary>
    /// 入力に応じてカメラを更新します。
    /// </summary>
    /// <param name="input">このフレームの入力状態。</param>
    /// <param name="camera">更新するカメラ。</param>
    /// <param name="deltaSeconds">前フレームからの経過秒数。</param>
    /// <remarks>
    /// 操作は次のとおりです。
    /// 左ドラッグで回転、右ドラッグまたは中ドラッグで注視点の平行移動、
    /// ホイールで寄り引き、`W`/`S`/`A`/`D` で回転、`R` で初期状態へ戻す。
    /// </remarks>
    void Update(const Input& input, Camera& camera, float deltaSeconds);

    /// <summary>視点と注視点を初期状態へ戻します。</summary>
    void Reset();

private:
    /// <summary>
    /// 方位角・仰角・距離から視点の位置を求め、カメラへ反映します。
    /// </summary>
    /// <param name="camera">更新するカメラ。</param>
    void ApplyToCamera(Camera& camera) const;

private:
    /// <summary>注視点。ここを中心に回ります。</summary>
    DirectX::XMFLOAT3 m_target = { 0.0f, 0.55f, 0.0f };

    /// <summary>方位角（水平方向の回転、ラジアン）。</summary>
    float m_yaw = 0.0f;

    /// <summary>仰角（上下方向の回転、ラジアン）。正で見下ろします。</summary>
    float m_pitch = 0.42f;

    /// <summary>注視点までの距離。</summary>
    float m_distance = 5.4f;
};

} // namespace dx12
