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

/// @brief COM オブジェクト用のスマートポインタ（`Microsoft::WRL::ComPtr` の短縮別名）。
/// @tparam T 管理する COM インターフェース（`ID3D12Device`、`IDXGIFactory6` など）。
///
/// **COM の参照カウントとは**
///
/// DirectX のオブジェクトは `new` / `delete` では作りません。生成関数が内部でオブジェクトを作り、
/// 「参照カウント = 1」の状態で返します。使い終わったら `Release()` を呼んでカウントを 1 減らし、0
/// になった時点でオブジェクトが破棄されます。
///
/// **ComPtr がやってくれること**
///
/// - デストラクタで自動的に `Release()` を呼ぶ（解放忘れゼロ）
/// - コピーすると自動的に `AddRef()`（カウント +1）
/// - `&ptr` で「受け取り用の空ポインタ」を渡せる
///
/// **使い方の注意**
///
/// - **ptr.Get()** : 生ポインタを取り出す（所有権は移動しない）
/// - **ptr.GetAddressOf()** : ポインタのアドレスを得る（生成関数への出力用）
/// - **&ptr** : ReleaseAndGetAddressOf() と同じ。中身を解放してから渡す
/// - **ptr.As(&other)** : 別のインターフェースに問い合わせる（QueryInterface）
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;


/// @brief DirectX API が返した `HRESULT` の失敗を表す例外クラス。
///
/// **HRESULT とは**
///
/// Windows API / DirectX API がほぼ全ての関数で返す 32bit の戻り値です。最上位ビットが 0
/// なら成功、1 なら失敗を表します。判定には必ず `SUCCEEDED(hr)` / `FAILED(hr)`
/// マクロを使います。（`hr == S_OK` での比較は NG。成功値は `S_OK` 以外にも存在するためです）
///
/// **なぜ例外にするのか**
///
/// DirectX の初期化は「30 回連続で HRESULT を返す関数を呼ぶ」ような処理です。毎回 `if (FAILED(hr))
/// return false;` と書くと本質的なコードが埋もれます。例外にすれば「失敗したら初期化処理全体を中断
/// して main まで飛ぶ」を自動化でき、コードが読みやすくなります。
///
/// @see ThrowIfFailed
class HrException : public std::runtime_error
{
public:
    /// @brief 失敗した HRESULT と説明文から例外を構築します。
    /// @param hr 失敗した HRESULT 値。
    /// @param message 人が読めるエラー説明（式・ファイル・行番号を含む）。
    HrException(HRESULT hr, std::string message)
        : std::runtime_error(std::move(message))
        , m_hr(hr)
    {
    }

    /// @brief 失敗した HRESULT の値を取得します。
    /// @returns 元の HRESULT 値。
    HRESULT ErrorCode() const noexcept { return m_hr; }

private:
    /// @brief 失敗した HRESULT 値。
    HRESULT m_hr;
};


/// @brief `HRESULT` が失敗を示していれば `HrException` を送出します。
/// @param hr 検査する HRESULT。
/// @param expression 呼び出した式の文字列（`DX_CHECK` が自動で渡します）。
/// @param file 呼び出し元のファイル名（`__FILE__`）。
/// @param line 呼び出し元の行番号（`__LINE__`）。
/// @exception HrException `hr` が失敗を示す場合。
///
/// この関数を直接呼ばず、必ず `DX_CHECK` マクロ経由で使ってください。マクロを挟むことで「失敗した式
/// そのもの」「ファイル名」「行番号」を自動的にエラーメッセージへ埋め込めます。
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


/// @brief 情報ログを 1 行出力します。
/// @param message 出力する文字列（改行は不要）。
///
/// 出力先はコンソールと、デバッガの出力ウィンドウの両方です。本プロジェクトはコンソールアプリ（サブ
/// システム = Console）としてビルドしているため、ウィンドウとは別にコンソールが開きます。初期化の進
/// 行状況がそこに流れるので、学習中の挙動確認に便利です。
void Log(const std::wstring& message);

/// @brief エラーログを 1 行出力します。
/// @param message 出力する文字列（改行は不要）。
///
/// 情報ログと見分けが付くよう `[ERROR]` が先頭に付きます。
void LogError(const std::wstring& message);


/// @brief シェーダー等のリソースファイルの実際の場所を探して絶対パスを返します。
/// @param relativePath プロジェクトルートからの相対パス（例: `L"shaders/Triangle.hlsl"`）。
/// @returns 見つかったファイルの絶対パス。
/// @exception std::runtime_error どの探索場所にも見つからなかった場合。
///
/// **なぜ必要か**
///
/// 「カレントディレクトリ」は実行方法によって変わります。
///
/// - Visual Studio から F5 … プロジェクトフォルダ
/// - VSCode から F5 … launch.json の cwd 設定次第
/// - エクスプローラから直接起動 … exe のあるフォルダ
///
/// どこから起動してもシェーダーを見つけられるよう、「カレントディレクトリ」と「exe のあるフォルダ」
/// の両方を、親方向にさかのぼりながら探索します。
std::wstring ResolveAssetPath(const std::wstring& relativePath);

} // namespace dx12
