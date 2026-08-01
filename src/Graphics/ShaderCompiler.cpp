//=============================================================================
// ShaderCompiler.cpp
//   ShaderCompiler の実装。
//=============================================================================
#include "ShaderCompiler.h"

#include <chrono>
#include <vector>

namespace dx12::shader
{
namespace
{

/// <summary>コンパイルした本数。</summary>
uint32_t g_compiledCount = 0;

/// <summary>コンパイルに掛かった合計時間（ミリ秒）。</summary>
double g_compiledMilliseconds = 0.0;

/// <summary>DXC の道具一式。1 度だけ作って使い回す。</summary>
struct Compiler
{
    /// <summary>ファイル読み込みや引数の組み立てを助ける道具。</summary>
    ComPtr<IDxcUtils> utils;

    /// <summary>コンパイラ本体。</summary>
    ComPtr<IDxcCompiler3> compiler;

    /// <summary>`#include` を解決する係。</summary>
    ComPtr<IDxcIncludeHandler> includeHandler;
};


/// <summary>
/// DXC の道具一式を取得します。初回だけ生成します。
/// </summary>
/// <returns>道具一式への参照。</returns>
/// <exception cref="HrException">生成に失敗した場合。</exception>
/// <remarks>
/// 生成は安くありません。33 本の習作を 1 本ずつ作り直すと、その回数ぶん
/// 損をします。関数内 static にして 1 度だけ作ります。
/// </remarks>
Compiler& GetCompiler()
{
    static Compiler instance = []()
    {
        Compiler created;

        // ★ DxcCreateInstance は COM の CoCreateInstance に似ているが別物。
        //   dxcompiler.dll が公開している独自の生成関数です。
        DX_CHECK(::DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&created.utils)));
        DX_CHECK(::DxcCreateInstance(CLSID_DxcCompiler,
                                     IID_PPV_ARGS(&created.compiler)));

        // 既定の `#include` 解決係。読み込むファイルからの相対で探します。
        DX_CHECK(created.utils->CreateDefaultIncludeHandler(&created.includeHandler));

        return created;
    }();

    return instance;
}


/// <summary>
/// DXC が返す UTF-8 の文字列を、ログに出せるワイド文字列へ変換します。
/// </summary>
/// <param name="utf8">変換元。</param>
/// <returns>ワイド文字列。</returns>
/// <remarks>
/// **`printf` で出してはいけません。** このアプリはコンソールを
/// `_O_U8TEXT`（ワイド専用）にしているため、ナロー出力を混ぜると
/// 不正パラメータとして扱われ、**何も表示せずにプロセスが落ちます**。
/// コンパイルエラーがいちばん知りたい場面で、それが起きます。
/// </remarks>
std::wstring ToWide(const char* utf8)
{
    if (utf8 == nullptr || *utf8 == '\0')
    {
        return {};
    }

    const int length = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);

    if (length <= 1)
    {
        return {};
    }

    std::wstring wide(static_cast<size_t>(length) - 1, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), length);

    return wide;
}

} // namespace


/// <summary>
/// HLSL ファイルをコンパイルして、GPU 用のバイトコードを得ます。
/// </summary>
Bytecode Compile(const std::wstring& filePath,
                 const wchar_t* entryPoint,
                 const wchar_t* target)
{
    return Compile(filePath, entryPoint, target, nullptr, 0);
}


/// <summary>
/// 追加の引数を渡して HLSL をコンパイルします。
/// </summary>
Bytecode Compile(const std::wstring& filePath,
                 const wchar_t* entryPoint,
                 const wchar_t* target,
                 const wchar_t* const* extraArguments,
                 size_t extraArgumentCount)
{
    const auto startTime = std::chrono::steady_clock::now();

    Compiler& dxc = GetCompiler();

    // --- (1) ファイルを読む ---------------------------------------------------
    //   ★ FXC と違い、DXC は「ファイルを開く」機能を持たない。
    //     読み込みは呼び出し側の仕事です。
    ComPtr<IDxcBlobEncoding> source;

    const HRESULT loadResult =
        dxc.utils->LoadFile(filePath.c_str(), nullptr, &source);

    if (FAILED(loadResult))
    {
        LogError(L"シェーダーを読み込めませんでした: " + filePath);
        DX_CHECK(loadResult);
    }

    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr      = source->GetBufferPointer();
    sourceBuffer.Size     = source->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_ACP;   // 先頭の BOM から自動判別させる

    // --- (2) 引数を組み立てる -------------------------------------------------
    //   ★ DXC の設定は、フラグの数値ではなくコマンドラインの文字列で渡す。
    //     dxc.exe に打つのと同じ書き方がそのまま使えます。
    std::vector<const wchar_t*> arguments;

    arguments.push_back(filePath.c_str());   // エラー表示に出るファイル名

    arguments.push_back(L"-E");
    arguments.push_back(entryPoint);

    arguments.push_back(L"-T");
    arguments.push_back(target);

    // ★ 行列の並びは指定しない（＝ HLSL の既定の列優先のまま）。
    //   FXC も既定は列優先で、C++ 側は XMMatrixTranspose してから渡している。
    //   ここで -Zpr（行優先）を足すと、同じ行列が転置されて解釈され、
    //   カメラも物体も違う場所へ行く。絵は出るので気付きにくい。

    // 警告を見落とさないよう、すべて表示する。
    arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);

#if defined(_DEBUG)
    arguments.push_back(DXC_ARG_DEBUG);              // -Zi 行単位のデバッグ情報
    arguments.push_back(DXC_ARG_SKIP_OPTIMIZATIONS); // -Od 最適化しない
#else
    arguments.push_back(DXC_ARG_OPTIMIZATION_LEVEL3);

    // 出来上がりに要らない情報を落とす。
    arguments.push_back(L"-Qstrip_debug");
    arguments.push_back(L"-Qstrip_reflect");
#endif

    for (size_t i = 0; i < extraArgumentCount; ++i)
    {
        arguments.push_back(extraArguments[i]);
    }

    // --- (3) コンパイル -------------------------------------------------------
    ComPtr<IDxcResult> result;

    DX_CHECK(dxc.compiler->Compile(&sourceBuffer,
                                   arguments.data(),
                                   static_cast<UINT32>(arguments.size()),
                                   dxc.includeHandler.Get(),
                                   IID_PPV_ARGS(&result)));

    // --- (4) 結果を取り出す ---------------------------------------------------
    //   ★ Compile 自体は成功を返す。エラーかどうかは結果に聞く。
    //     戻り値だけ見ていると、失敗を見逃します。
    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);

    if (errors != nullptr && errors->GetStringLength() > 0)
    {
        const std::wstring message = ToWide(errors->GetStringPointer());

        ::OutputDebugStringW(message.c_str());
        ::OutputDebugStringW(L"\n");

        LogError(message);
    }

    HRESULT status = S_OK;
    DX_CHECK(result->GetStatus(&status));

    if (FAILED(status))
    {
        LogError(L"シェーダーのコンパイルに失敗しました: " + filePath);
        DX_CHECK(status);
    }

    Bytecode bytecode;
    DX_CHECK(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&bytecode), nullptr));

    const auto endTime = std::chrono::steady_clock::now();

    ++g_compiledCount;
    g_compiledMilliseconds +=
        std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return bytecode;
}


/// <summary>
/// これまでにコンパイルした本数と、それに掛かった合計時間を返します。
/// </summary>
void GetStatistics(uint32_t& outCount, double& outMilliseconds)
{
    outCount        = g_compiledCount;
    outMilliseconds = g_compiledMilliseconds;
}

} // namespace dx12::shader
