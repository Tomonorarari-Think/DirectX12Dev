//=============================================================================
// ShaderCompiler.h
//   HLSL ファイルを実行時にコンパイルする、パイプライン共通の処理。
//
//   コンパイラは DXC（DirectX Shader Compiler）。シェーダーモデル 6 以降は
//   こちらでしか通らない。詳しい解説は docs/tutorial/31_DXCとシェーダーモデル6.md
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

#include <dxcapi.h>

#include <string>

namespace dx12::shader
{

/// <summary>
/// コンパイル済みのバイトコード。
/// </summary>
/// <remarks>
/// `IDxcBlob` は `ID3DBlob` と同じく `GetBufferPointer` / `GetBufferSize` を
/// 持つので、PSO への渡し方は今までと変わりません。
/// </remarks>
using Bytecode = ComPtr<IDxcBlob>;

/// <summary>
/// このプロジェクトが使うシェーダーモデル。
/// </summary>
/// <remarks>
/// 6.0 は DXIL の最初の版で、DirectX 12 対応 GPU なら広く通ります。
/// これより上げると、動く GPU が絞られる代わりに新しい機能が使えます。
/// </remarks>
constexpr const wchar_t* kVertexShaderTarget  = L"vs_6_0";

/// <summary>ピクセルシェーダーのモデル。</summary>
constexpr const wchar_t* kPixelShaderTarget   = L"ps_6_0";

/// <summary>コンピュートシェーダーのモデル。</summary>
constexpr const wchar_t* kComputeShaderTarget = L"cs_6_0";

/// <summary>
/// HLSL ファイルをコンパイルして、GPU 用のバイトコードを得ます。
/// </summary>
/// <param name="filePath">`.hlsl` ファイルの絶対パス。</param>
/// <param name="entryPoint">入口となる関数名（例: `L"VSMain"`）。</param>
/// <param name="target">シェーダーモデル（例: `kVertexShaderTarget`）。</param>
/// <returns>コンパイル済みのバイトコード。</returns>
/// <exception cref="HrException">コンパイルに失敗した場合。</exception>
/// <remarks>
/// 文法エラーの内容は、デバッグ出力とコンソールの両方に出します。
/// 実行時コンパイルなので、起動しないと間違いに気付けません。
/// 実務では事前にコンパイルして `.cso` を配ります。
/// </remarks>
Bytecode Compile(const std::wstring& filePath,
                 const wchar_t* entryPoint,
                 const wchar_t* target);

/// <summary>
/// これまでにコンパイルした本数と、それに掛かった合計時間を返します。
/// </summary>
/// <param name="outCount">コンパイルした本数の受け取り先。</param>
/// <param name="outMilliseconds">合計時間（ミリ秒）の受け取り先。</param>
/// <remarks>起動時間のどれだけをシェーダーが占めるかを見るための機能です。</remarks>
void GetStatistics(uint32_t& outCount, double& outMilliseconds);

} // namespace dx12::shader
