//=============================================================================
// GraphicsCommon.h
//   プロジェクト全体で使う共通の道具をまとめたヘッダ。
//=============================================================================
#pragma once

// windows.h を include する前のおまじない
//   WIN32_LEAN_AND_MEAN : windows.h が芋づる式に読み込む巨大なヘッダ群
//                         （古い OLE/RPC 等）を削り、コンパイルを速くする。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// --- DirectX 12 本体 ---------------------------------------------------------
// d3d12.h      : Direct3D 12 の API（デバイス、コマンドリスト、リソース等）
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

// --- COM スマートポインタ ----------------------------------------------------
// DirectX のオブジェクトは COM (Component Object Model) という仕組みで
// 参照カウント管理されています。生ポインタで扱うと Release() の呼び忘れ＝
#include <wrl/client.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace dx12
{

/// <summary>
/// COM オブジェクト用のスマートポインタ（`Microsoft::WRL::ComPtr` の短縮別名）。
/// </summary>
/// <typeparam name="T">
/// 管理する COM インターフェース（`ID3D12Device`、`IDXGIFactory6` など）。
/// </typeparam>
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;


/// <summary>
/// DirectX API が返した `HRESULT` の失敗を表す例外クラス。
/// </summary>
class HrException : public std::runtime_error
{
public:
    /// <summary>
    /// 失敗した HRESULT と説明文から例外を構築します。
    /// </summary>
    /// <param name="hr">失敗した HRESULT 値。</param>
    /// <param name="message">人が読めるエラー説明（式・ファイル・行番号を含む）。</param>
    HrException(HRESULT hr, std::string message)
        : std::runtime_error(std::move(message))
        , m_hr(hr)
    {
    }

    /// <summary>
    /// 失敗した HRESULT の値を取得します。
    /// </summary>
    /// <returns>元の HRESULT 値。</returns>
    HRESULT ErrorCode() const noexcept { return m_hr; }

private:
    /// <summary>
    /// 失敗した HRESULT 値。
    /// </summary>
    HRESULT m_hr;
};


/// <summary>
/// `HRESULT` が失敗を示していれば `HrException` を送出します。
/// </summary>
/// <param name="hr">検査する HRESULT。</param>
/// <param name="expression">呼び出した式の文字列（`DX_CHECK` が自動で渡します）。</param>
/// <param name="file">呼び出し元のファイル名（`__FILE__`）。</param>
/// <param name="line">呼び出し元の行番号（`__LINE__`）。</param>
/// <exception cref="HrException">`hr` が失敗を示す場合。</exception>
void ThrowIfFailed(HRESULT hr, const char* expression, const char* file, int line);


// DX_CHECK : DirectX 呼び出しをラップして、失敗時に例外を投げるマクロ
//   使い方:
#define DX_CHECK(expr) ::dx12::ThrowIfFailed((expr), #expr, __FILE__, __LINE__)


/// <summary>
/// 情報ログを 1 行出力します。
/// </summary>
/// <param name="message">出力する文字列（改行は不要）。</param>
void Log(const std::wstring& message);

/// <summary>
/// エラーログを 1 行出力します。
/// </summary>
/// <param name="message">出力する文字列（改行は不要）。</param>
void LogError(const std::wstring& message);


/// <summary>
/// シェーダー等のリソースファイルの実際の場所を探して絶対パスを返します。
/// </summary>
/// <param name="relativePath">
/// プロジェクトルートからの相対パス（例: `L"shaders/Mesh.hlsl"`）。
/// </param>
/// <returns>見つかったファイルの絶対パス。</returns>
/// <exception cref="std::runtime_error">どの探索場所にも見つからなかった場合。</exception>
std::wstring ResolveAssetPath(const std::wstring& relativePath);

} // namespace dx12
