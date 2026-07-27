//=============================================================================
// ModelLoader.h
//   モデルファイルを読み込んで MeshData にするための共通の入口。
//
//   ファイルの中身を解釈するだけで DirectX は使わない。
//   GPU へ載せるのは Mesh の仕事。
//=============================================================================
#pragma once

#include "../Graphics/Geometry.h"

#include <string>

namespace dx12::assets
{

/// <summary>
/// 読み込み時の調整項目。
/// </summary>
struct ModelLoadOptions
{
    /// <summary>
    /// 頂点に付ける色 (r, g, b, a)。
    /// </summary>
    /// <remarks>
    /// OBJ は頂点カラーを持たないため、こちらで与えます。
    /// 陰影が読み取りやすいよう、既定は彩度の低い明るい色にしています。
    /// </remarks>
    float color[4] = { 0.82f, 0.84f, 0.90f, 1.0f };

    /// <summary>
    /// 原点を中心にし、指定した大きさに収まるよう自動で拡大縮小するか。
    /// </summary>
    /// <remarks>
    /// モデルの単位はファイルによってまちまち（メートル、センチ、インチ…）です。
    /// そのまま置くと、画面に入りきらないか小さすぎて見えません。
    /// </remarks>
    bool fitToTargetSize = true;

    /// <summary>
    /// `fitToTargetSize` が `true` のときの目標サイズ（一番長い辺の長さ）。
    /// </summary>
    float targetSize = 1.6f;

    /// <summary>
    /// 拡大縮小したあと、モデルの底面を置く高さ。
    /// </summary>
    /// <remarks>床の上にちょうど乗せたいので、既定は床の高さに合わせています。</remarks>
    float groundLevel = 0.0f;
};


/// <summary>
/// 拡張子を見て、対応するローダでモデルを読み込みます。
/// </summary>
/// <param name="filePath">モデルファイルの絶対パス。</param>
/// <param name="options">読み込み時の調整項目。</param>
/// <returns>頂点とインデックスの組。</returns>
/// <exception cref="std::runtime_error">
/// ファイルを開けない、対応していない拡張子、または内容が壊れている場合。
/// </exception>
MeshData LoadModel(const std::wstring& filePath, const ModelLoadOptions& options = {});

/// <summary>
/// モデルを原点中心へ移動し、指定した大きさに収まるよう拡大縮小します。
/// </summary>
/// <param name="mesh">対象のメッシュ。頂点座標が書き換わります。</param>
/// <param name="targetSize">一番長い辺をこの長さに揃えます。</param>
/// <param name="groundLevel">底面を置く高さ。</param>
/// <remarks>
/// 法線は向きだけなので、一様な拡大縮小では変わりません。そのため触りません。
/// </remarks>
void FitToTargetSize(MeshData& mesh, float targetSize, float groundLevel);

} // namespace dx12::assets
