//=============================================================================
// GraphicsCommon.h
//   プロジェクト全体で使う共通の道具をまとめたヘッダ。
//=============================================================================
#pragma once

//-----------------------------------------------------------------------------
// windows.h を include する前のおまじない
//   WIN32_LEAN_AND_MEAN : windows.h が芋づる式に読み込む巨大なヘッダ群
//                         （古い OLE/RPC 等）を削り、コンパイルを速くする。
//   NOMINMAX            : windows.h は min / max という「マクロ」を定義する。
//                         これが std::min / std::max と衝突して
//                         意味不明なコンパイルエラーを引き起こすため無効化する。
//-----------------------------------------------------------------------------
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// --- DirectX 12 本体 ---------------------------------------------------------
// d3d12.h      : Direct3D 12 の API（デバイス、コマンドリスト、リソース等）
// dxgi1_6.h    : DXGI (DirectX Graphics Infrastructure)。
//                GPU（アダプタ）の列挙や、画面への表示（スワップチェーン）を担当する。
//                D3D12 と DXGI は役割分担しており、「描く」のが D3D12、
//                「画面に出す」のが DXGI、と覚えると分かりやすい。
// d3dcompiler.h: HLSL シェーダーを実行時にコンパイルするための API
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

// --- COM スマートポインタ ----------------------------------------------------
// DirectX のオブジェクトは COM (Component Object Model) という仕組みで
// 参照カウント管理されています。生ポインタで扱うと Release() の呼び忘れ＝
// メモリリークが頻発するため、必ず ComPtr 経由で扱います。
#include <wrl/client.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace dx12
{

/// <summary>
/// COM オブジェクト用のスマートポインタ（<c>Microsoft::WRL::ComPtr</c> の短縮別名）。
/// </summary>
/// <typeparam name="T">
/// 管理する COM インターフェース（<c>ID3D12Device</c>、<c>IDXGIFactory6</c> など）。
/// </typeparam>
/// <remarks>
/// <para>
/// <b>COM の参照カウントとは</b><br/>
/// DirectX のオブジェクトは <c>new</c> / <c>delete</c> では作りません。
/// 生成関数が内部でオブジェクトを作り、「参照カウント = 1」の状態で返します。
/// 使い終わったら <c>Release()</c> を呼んでカウントを 1 減らし、
/// 0 になった時点でオブジェクトが破棄されます。
/// </para>
/// <para>
/// <b>ComPtr がやってくれること</b>
/// <list type="bullet">
///   <item>デストラクタで自動的に <c>Release()</c> を呼ぶ（解放忘れゼロ）</item>
///   <item>コピーすると自動的に <c>AddRef()</c>（カウント +1）</item>
///   <item><c>&amp;ptr</c> で「受け取り用の空ポインタ」を渡せる</item>
/// </list>
/// </para>
/// <para>
/// <b>使い方の注意</b>
/// <list type="table">
///   <item><term>ptr.Get()</term><description>生ポインタを取り出す（所有権は移動しない）</description></item>
///   <item><term>ptr.GetAddressOf()</term><description>ポインタのアドレスを得る（生成関数への出力用）</description></item>
///   <item><term>&amp;ptr</term><description>ReleaseAndGetAddressOf() と同じ。中身を解放してから渡す</description></item>
///   <item><term>ptr.As(&amp;other)</term><description>別のインターフェースに問い合わせる（QueryInterface）</description></item>
/// </list>
/// </para>
/// </remarks>
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;


/// <summary>
/// DirectX API が返した <c>HRESULT</c> の失敗を表す例外クラス。
/// </summary>
/// <remarks>
/// <para>
/// <b>HRESULT とは</b><br/>
/// Windows API / DirectX API がほぼ全ての関数で返す 32bit の戻り値です。
/// 最上位ビットが 0 なら成功、1 なら失敗を表します。
/// 判定には必ず <c>SUCCEEDED(hr)</c> / <c>FAILED(hr)</c> マクロを使います。
/// （<c>hr == S_OK</c> での比較は NG。成功値は <c>S_OK</c> 以外にも存在するためです）
/// </para>
/// <para>
/// <b>なぜ例外にするのか</b><br/>
/// DirectX の初期化は「30 回連続で HRESULT を返す関数を呼ぶ」ような処理です。
/// 毎回 <c>if (FAILED(hr)) return false;</c> と書くと本質的なコードが埋もれます。
/// 例外にすれば「失敗したら初期化処理全体を中断して main まで飛ぶ」を
/// 自動化でき、コードが読みやすくなります。
/// </para>
/// </remarks>
/// <seealso cref="ThrowIfFailed"/>
class HrException : public std::runtime_error
{
public:
    /// <summary>失敗した HRESULT と説明文から例外を構築します。</summary>
    /// <param name="hr">失敗した HRESULT 値。</param>
    /// <param name="message">人が読めるエラー説明（式・ファイル・行番号を含む）。</param>
    HrException(HRESULT hr, std::string message)
        : std::runtime_error(std::move(message))
        , m_hr(hr)
    {
    }

    /// <summary>失敗した HRESULT の値を取得します。</summary>
    /// <returns>元の HRESULT 値。</returns>
    HRESULT ErrorCode() const noexcept { return m_hr; }

private:
    /// <summary>失敗した HRESULT 値。</summary>
    HRESULT m_hr;
};


/// <summary>
/// <c>HRESULT</c> が失敗を示していれば <see cref="HrException"/> を送出します。
/// </summary>
/// <param name="hr">検査する HRESULT。</param>
/// <param name="expression">呼び出した式の文字列（<c>DX_CHECK</c> が自動で渡します）。</param>
/// <param name="file">呼び出し元のファイル名（<c>__FILE__</c>）。</param>
/// <param name="line">呼び出し元の行番号（<c>__LINE__</c>）。</param>
/// <exception cref="HrException"><paramref name="hr"/> が失敗を示す場合。</exception>
/// <remarks>
/// この関数を直接呼ばず、必ず <c>DX_CHECK</c> マクロ経由で使ってください。
/// マクロを挟むことで「失敗した式そのもの」「ファイル名」「行番号」を
/// 自動的にエラーメッセージへ埋め込めます。
/// </remarks>
void ThrowIfFailed(HRESULT hr, const char* expression, const char* file, int line);


//-----------------------------------------------------------------------------
// DX_CHECK : DirectX 呼び出しをラップして、失敗時に例外を投げるマクロ
//
//   使い方:
//       DX_CHECK(D3D12CreateDevice(adapter, level, IID_PPV_ARGS(&device)));
//
//   #expr はプリプロセッサの「文字列化演算子」で、渡された式をそのまま
//   文字列リテラルに変換します。おかげでエラーメッセージに
//   「どの関数呼び出しで落ちたか」が残ります。
//
//   ※ マクロはプリプロセッサが処理するため XML ドキュメントコメントの
//     対象になりません。マクロの説明は従来どおりの // コメントで書きます。
//-----------------------------------------------------------------------------
#define DX_CHECK(expr) ::dx12::ThrowIfFailed((expr), #expr, __FILE__, __LINE__)


/// <summary>情報ログを 1 行出力します。</summary>
/// <param name="message">出力する文字列（改行は不要）。</param>
/// <remarks>
/// 出力先はコンソールと、デバッガの出力ウィンドウの両方です。
/// 本プロジェクトはコンソールアプリ（サブシステム = Console）として
/// ビルドしているため、ウィンドウとは別にコンソールが開きます。
/// 初期化の進行状況がそこに流れるので、学習中の挙動確認に便利です。
/// </remarks>
void Log(const std::wstring& message);

/// <summary>エラーログを 1 行出力します。</summary>
/// <param name="message">出力する文字列（改行は不要）。</param>
/// <remarks>情報ログと見分けが付くよう <c>[ERROR]</c> が先頭に付きます。</remarks>
void LogError(const std::wstring& message);


/// <summary>
/// シェーダー等のリソースファイルの実際の場所を探して絶対パスを返します。
/// </summary>
/// <param name="relativePath">
/// プロジェクトルートからの相対パス（例: <c>L"shaders/Triangle.hlsl"</c>）。
/// </param>
/// <returns>見つかったファイルの絶対パス。</returns>
/// <exception cref="std::runtime_error">どの探索場所にも見つからなかった場合。</exception>
/// <remarks>
/// <para>
/// <b>なぜ必要か</b><br/>
/// 「カレントディレクトリ」は実行方法によって変わります。
/// <list type="bullet">
///   <item>Visual Studio から F5 … プロジェクトフォルダ</item>
///   <item>VSCode から F5 … launch.json の cwd 設定次第</item>
///   <item>エクスプローラから直接起動 … exe のあるフォルダ</item>
/// </list>
/// どこから起動してもシェーダーを見つけられるよう、
/// 「カレントディレクトリ」と「exe のあるフォルダ」の両方を、
/// 親方向にさかのぼりながら探索します。
/// </para>
/// </remarks>
std::wstring ResolveAssetPath(const std::wstring& relativePath);

} // namespace dx12
