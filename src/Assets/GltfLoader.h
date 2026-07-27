//=============================================================================
// GltfLoader.h
//   glTF 2.0（.gltf / .glb）の読み込み。
//
//   詳しい解説は docs/tutorial/16_モデルを読み込む_glTF.md を参照。
//=============================================================================
#pragma once

#include "ModelLoader.h"

namespace dx12::assets
{

/// <summary>
/// glTF 2.0 のモデルを読み込みます。
/// </summary>
/// <param name="filePath">.gltf または .glb ファイルの絶対パス。</param>
/// <param name="options">読み込み時の調整項目。</param>
/// <returns>頂点とインデックスの組。</returns>
/// <exception cref="std::runtime_error">
/// ファイルを開けない、内容が壊れている、または未対応の構成だった場合。
/// </exception>
/// <remarks>
/// 対応するのは形（メッシュ）だけです。次のものは読み飛ばします。
/// マテリアル、テクスチャ画像、アニメーション、スキン、モーフターゲット、
/// カメラ、疎なアクセサ (sparse)。
/// シーン中の全メッシュを、ノードの変換を適用したうえで 1 つに結合します。
/// </remarks>
MeshData LoadGltf(const std::wstring& filePath, const ModelLoadOptions& options = {});

} // namespace dx12::assets
