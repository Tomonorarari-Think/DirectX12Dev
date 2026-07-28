//=============================================================================
// ShaderCompiler.h
//   HLSL ファイルを実行時にコンパイルする、パイプライン共通の処理。
//
//   MeshPipeline と SkyboxPipeline で同じ手順が要るため切り出した。
//   詳しい解説は docs/tutorial/06_三角形を描く.md を参照。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

#include <string>

namespace dx12::shader
{

/// <summary>
/// HLSL ファイルをコンパイルして、GPU 用のバイトコードを得ます。
/// </summary>
/// <param name="filePath">`.hlsl` ファイルの絶対パス。</param>
/// <param name="entryPoint">入口となる関数名（例: `"VSMain"`）。</param>
/// <param name="target">シェーダーモデル（例: `"vs_5_0"`）。</param>
/// <returns>コンパイル済みのバイトコード。</returns>
/// <exception cref="std::runtime_error">コンパイルに失敗した場合。</exception>
/// <remarks>
/// 文法エラーの内容は、デバッグ出力とコンソールの両方に出します。
/// 実行時コンパイルなので、起動しないと間違いに気付けません。
/// 実務では事前にコンパイルして `.cso` を配ります。
/// </remarks>
ComPtr<ID3DBlob> Compile(const std::wstring& filePath,
                         const char* entryPoint,
                         const char* target);

} // namespace dx12::shader
