//=============================================================================
// Terrain.h
//   広い地形と、その上に散らばる柱を組み立てる。
//
//   34 章のカスケードシャドウマップは、範囲の広い場面でなければ
//   効果が絵に出ない。それを確かめられる場を用意する。
//   詳しい解説は docs/tutorial/35_広い場面を作る.md を参照。
//=============================================================================
#pragma once

#include "Geometry.h"

#include <cstdint>

namespace dx12
{

/// <summary>
/// 地形の高さを返します。
/// </summary>
/// <param name="x">ワールド座標の X。</param>
/// <param name="z">ワールド座標の Z。</param>
/// <returns>その位置の地面の高さ（Y）。</returns>
/// <remarks>
/// **地形と柱の両方から呼びます。** 別々の式で高さを求めると、
/// 柱が地面から浮いたり埋まったりします。
///
/// 原点のまわりは平らにしてあります。既存の床（5 x 5）と段差が出ないようにするためです。
/// </remarks>
float TerrainHeight(float x, float z);

/// <summary>
/// 起伏のある広い地形を作ります。
/// </summary>
/// <param name="halfExtent">中心から端までの距離（ワールド単位）。</param>
/// <param name="resolution">一辺の分割数。頂点は (resolution + 1) の 2 乗になります。</param>
/// <returns>頂点と索引を詰めた形状データ。</returns>
/// <remarks>
/// 高さは `TerrainHeight` から求め、法線は隣の高さとの差から計算します。
/// </remarks>
MeshData CreateTerrain(float halfExtent, uint32_t resolution);

/// <summary>
/// 地形の上に柱を散らして、1 つの形状データにまとめます。
/// </summary>
/// <param name="halfExtent">柱を置く範囲（中心から端まで）。</param>
/// <param name="count">柱の本数。</param>
/// <param name="clearRadius">この半径の内側には置きません（既存の床を避けるため）。</param>
/// <returns>すべての柱を 1 つにまとめた形状データ。</returns>
/// <remarks>
/// **1 本ずつ描くのではなく、まとめて 1 つのメッシュにします。**
/// 影のパスは段の数だけ描き直すので、描画命令の数がそのまま効いてきます。
/// </remarks>
MeshData CreatePillarField(float halfExtent, uint32_t count, float clearRadius);

} // namespace dx12
