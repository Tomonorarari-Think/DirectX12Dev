//=============================================================================
// ShaderCompiler.cpp
//   ShaderCompiler の実装。
//=============================================================================
#include "ShaderCompiler.h"

#include <cstdio>   // printf（シェーダーのコンパイルエラーをコンソールに出す）

namespace dx12::shader
{

/// <summary>
/// HLSL ファイルをコンパイルして、GPU 用のバイトコードを得ます。
/// </summary>
ComPtr<ID3DBlob> Compile(const std::wstring& filePath,
                         const char* entryPoint,
                         const char* target)
{
    UINT compileFlags = 0;

#if defined(_DEBUG)
    // DEBUG            : シェーダーデバッガで行単位のデバッグができる情報を埋め込む
    // SKIP_OPTIMIZATION: 最適化を行わない。変数が消えないためデバッグしやすい
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    // 最高レベルの最適化を行う
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    const HRESULT hr = ::D3DCompileFromFile(
        filePath.c_str(),
        nullptr,                               // マクロ定義（#define 相当）。今回は無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,     // #include を .hlsl と同じ階層から解決する
        entryPoint,                            // 入口となる関数名
        target,                                // シェーダーモデル
        compileFlags,
        0,                                     // エフェクト用フラグ（未使用）
        &shaderBlob,
        &errorBlob);

    if (FAILED(hr))
    {
        // シェーダーの文法エラーはここに出ます。
        LogError(L"シェーダーのコンパイルに失敗しました: " + filePath);

        if (errorBlob != nullptr)
        {
            const char* message = static_cast<const char*>(errorBlob->GetBufferPointer());
            ::OutputDebugStringA(message);
            ::OutputDebugStringA("\n");
            ::printf("%s\n", message); // コンソールにも出す
        }

        DX_CHECK(hr);
    }

    return shaderBlob;
}

} // namespace dx12::shader
