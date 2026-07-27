//=============================================================================
// Camera.cpp
//   Camera の実装。
//=============================================================================
#include "Camera.h"

using namespace DirectX;

namespace dx12
{

/// <summary>
/// ビュー行列（ワールド空間をカメラ基準へ移す行列）を返します。
/// </summary>
XMMATRIX Camera::ViewMatrix() const
{
    // 「カメラを動かす」のではなく「世界をカメラの逆向きに動かす」行列。
    // GPU にカメラという概念は無く、原点から +Z を見ている状態しか扱えないため。
    const XMVECTOR eye    = XMLoadFloat3(&m_position);
    const XMVECTOR target = XMLoadFloat3(&m_target);
    const XMVECTOR up     = XMLoadFloat3(&m_up);

    return XMMatrixLookAtLH(eye, target, up);
}


/// <summary>
/// 射影行列（遠近感を付け、クリップ空間へ移す行列）を返します。
/// </summary>
XMMATRIX Camera::ProjectionMatrix() const
{
    // 遠くのものほど w が大きくなるように仕込む行列。
    // GPU がラスタライズ前に xyz を w で割ることで、遠近感が生まれる。
    return XMMatrixPerspectiveFovLH(m_fieldOfViewY, m_aspectRatio, m_nearZ, m_farZ);
}


/// <summary>
/// ビュー行列と射影行列を掛け合わせたものを返します。
/// </summary>
XMMATRIX Camera::ViewProjectionMatrix() const
{
    // 行ベクトル規約なので「先に適用したい変換を左」に書く。
    return ViewMatrix() * ProjectionMatrix();
}

} // namespace dx12
