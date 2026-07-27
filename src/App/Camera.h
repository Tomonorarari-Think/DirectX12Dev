//=============================================================================
// Camera.h
//   視点の位置と向き、および透視投影の設定を持つカメラ。
//
//   DirectX に依存せず DirectXMath だけで作られているため App/ に置いている。
//   詳しい解説は docs/tutorial/10_3Dにする_カメラと透視投影.md を参照。
//=============================================================================
#pragma once

#include <DirectXMath.h>

namespace dx12
{

/// <summary>
/// ビュー行列と射影行列を組み立てるカメラ。
/// </summary>
/// <remarks>
/// 本プロジェクトは左手座標系（LH）で統一しています。行列の掛ける順序と
/// 座標系を混在させると、モデルが裏返ったり映らなくなったりします。
/// </remarks>
class Camera
{
public:
    /// <summary>既定のコンストラクタ。原点を見る初期位置を設定します。</summary>
    Camera() = default;

    /// <summary>視点の位置を設定します。</summary>
    /// <param name="x">X 座標。</param>
    /// <param name="y">Y 座標。</param>
    /// <param name="z">Z 座標。</param>
    void SetPosition(float x, float y, float z) { m_position = { x, y, z }; }

    /// <summary>注視点（カメラが向く先）を設定します。</summary>
    /// <param name="x">X 座標。</param>
    /// <param name="y">Y 座標。</param>
    /// <param name="z">Z 座標。</param>
    void SetTarget(float x, float y, float z) { m_target = { x, y, z }; }

    /// <summary>画面の縦横比を設定します。</summary>
    /// <param name="aspectRatio">幅 ÷ 高さ。</param>
    /// <remarks>ウィンドウサイズが変わるたびに更新してください。</remarks>
    void SetAspectRatio(float aspectRatio) { m_aspectRatio = aspectRatio; }

    /// <summary>垂直方向の画角を設定します。</summary>
    /// <param name="radians">画角（ラジアン）。既定は 45 度。</param>
    void SetFieldOfView(float radians) { m_fieldOfViewY = radians; }

    /// <summary>ビュー行列（ワールド空間をカメラ基準へ移す行列）を返します。</summary>
    /// <returns>左手座標系のビュー行列。</returns>
    DirectX::XMMATRIX ViewMatrix() const;

    /// <summary>射影行列（遠近感を付け、クリップ空間へ移す行列）を返します。</summary>
    /// <returns>左手座標系の透視射影行列。</returns>
    DirectX::XMMATRIX ProjectionMatrix() const;

    /// <summary>ビュー行列と射影行列を掛け合わせたものを返します。</summary>
    /// <returns>ビュー × 射影。ワールド行列と掛けて頂点シェーダーへ渡します。</returns>
    DirectX::XMMATRIX ViewProjectionMatrix() const;

private:
    /// <summary>視点の位置。</summary>
    DirectX::XMFLOAT3 m_position = { 0.0f, 1.2f, -3.2f };

    /// <summary>注視点。</summary>
    DirectX::XMFLOAT3 m_target = { 0.0f, 0.0f, 0.0f };

    /// <summary>上方向。カメラの傾きを決めます。</summary>
    DirectX::XMFLOAT3 m_up = { 0.0f, 1.0f, 0.0f };

    /// <summary>垂直方向の画角（ラジアン）。</summary>
    float m_fieldOfViewY = DirectX::XM_PIDIV4;

    /// <summary>画面の縦横比（幅 ÷ 高さ）。</summary>
    float m_aspectRatio = 16.0f / 9.0f;

    /// <summary>手前のクリップ面までの距離。</summary>
    /// <remarks>
    /// 小さくしすぎると遠くの深度の精度が落ち、面がちらつきます（Z ファイティング）。
    /// </remarks>
    float m_nearZ = 0.1f;

    /// <summary>奥のクリップ面までの距離。</summary>
    float m_farZ = 100.0f;
};

} // namespace dx12
