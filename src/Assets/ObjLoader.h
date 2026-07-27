//=============================================================================
// ObjLoader.h
//   Wavefront OBJ 形式の読み込み。
//
//   テキスト形式で人間が読めるため、モデル形式の入門として最適。
//   詳しい解説は docs/tutorial/15_モデルを読み込む_OBJ.md を参照。
//=============================================================================
#pragma once

#include "ModelLoader.h"

namespace dx12::assets
{

/// <summary>
/// Wavefront OBJ ファイルを読み込みます。
/// </summary>
/// <param name="filePath">.obj ファイルの絶対パス。</param>
/// <param name="options">読み込み時の調整項目。</param>
/// <returns>頂点とインデックスの組。</returns>
/// <exception cref="std::runtime_error">
/// ファイルを開けない、頂点が 1 つも無い、または頂点が 65536 個を超えた場合。
/// </exception>
/// <remarks>
/// 対応するのは `v` / `vt` / `vn` / `f` の 4 つだけです。マテリアル (`mtllib` /
/// `usemtl`) やグループ (`g` / `o`) は読み飛ばします。
/// 右手座標系から左手座標系への変換もここで行います。
/// </remarks>
MeshData LoadObj(const std::wstring& filePath, const ModelLoadOptions& options = {});

} // namespace dx12::assets
