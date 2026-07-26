//=============================================================================
// GraphicsCommon.cpp
//   GraphicsCommon.h で宣言した共通ユーティリティの実装。
//=============================================================================
#include "GraphicsCommon.h"

#include <filesystem>
#include <format>
#include <iostream>

namespace dx12
{
namespace
{

/// @brief HRESULT を人間が読める説明文に変換します。
/// @param hr 変換する HRESULT。
/// @returns 説明文。取得できなかった場合は `"(説明文なし)"`。
///
/// `FormatMessageW` は Windows が持つ「エラーコード → 環境の言語の説明文」の変換 API です。DirectX
/// 固有のコード（例: `DXGI_ERROR_DEVICE_REMOVED`）は変換できないことがあるため、その場合は説明なし
/// を返します。
std::wstring HResultToMessage(HRESULT hr)
{
    LPWSTR buffer = nullptr;

    // FORMAT_MESSAGE_ALLOCATE_BUFFER : 説明文の格納先を Windows 側で確保させる
    // FORMAT_MESSAGE_FROM_SYSTEM     : システム定義のエラーテーブルから探す
    // FORMAT_MESSAGE_IGNORE_INSERTS  : "%1" 等の差し込み指定を展開しない
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    if (length == 0 || buffer == nullptr)
    {
        // 説明文が得られなかった（DirectX 固有コードなど）
        return L"(説明文なし)";
    }

    std::wstring message(buffer, length);

    // FormatMessageW が確保したメモリは LocalFree で必ず解放する
    ::LocalFree(buffer);

    // 末尾の改行・空白を取り除く（FormatMessageW は "\r\n" を付けてくる）
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
    {
        message.pop_back();
    }

    return message;
}

/// @brief 実行中の exe が置かれているフォルダを取得します。
/// @returns exe のあるディレクトリのパス。取得に失敗した場合はカレントディレクトリ。
std::filesystem::path GetExecutableDirectory()
{
    // MAX_PATH (260) を超える長いパスにも耐えられるよう、余裕を持ったバッファを使う
    std::wstring buffer(1024, L'\0');

    // 第 1 引数 nullptr は「自分自身（実行中の exe）」を意味する
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
    {
        return std::filesystem::current_path();
    }

    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

/// @brief コンソールとデバッガ出力の両方へ 1 行書き出します。
/// @param line 出力する行（改行は内部で付与します）。
void WriteLine(const std::wstring& line)
{
    // (1) コンソールへ。std::wcout はワイド文字（UTF-16）用の出力ストリーム。
    std::wcout << line << std::endl;

    // (2) デバッガへ。Visual Studio の「出力」ウィンドウに表示される。
    //     VSCode でも C++ 拡張のデバッグコンソールに表示される。
    ::OutputDebugStringW((line + L"\n").c_str());
}

} // 無名 namespace（このファイルの外からは見えない ＝ 内部実装専用）


/// @brief HRESULT が失敗を示していれば HrException を送出します。
void ThrowIfFailed(HRESULT hr, const char* expression, const char* file, int line)
{
    // SUCCEEDED / FAILED は HRESULT の最上位ビットを見るマクロ。
    // 「hr == S_OK」で判定してはいけない（S_FALSE など他の成功値があるため）。
    if (SUCCEEDED(hr))
    {
        return;
    }

    // ファイルパスはフルパスだと長すぎるのでファイル名だけに縮める
    const std::filesystem::path filePath(file);
    const std::string fileName = filePath.filename().string();

    const std::wstring detail = HResultToMessage(hr);

    // ログにも残しておく（例外が握り潰されても痕跡が残るように）
    LogError(std::format(L"HRESULT 失敗 (0x{:08X}) : {}", static_cast<unsigned int>(hr), detail));

    // 例外メッセージは std::runtime_error に合わせて std::string で組み立てる
    const std::string message = std::format(
        "DirectX API 呼び出しに失敗しました。\n"
        "  式        : {}\n"
        "  場所      : {}({})\n"
        "  HRESULT   : 0x{:08X}",
        expression, fileName, line, static_cast<unsigned int>(hr));

    throw HrException(hr, message);
}

/// @brief 情報ログを 1 行出力します。
void Log(const std::wstring& message)
{
    WriteLine(L"[INFO ] " + message);
}

/// @brief エラーログを 1 行出力します。
void LogError(const std::wstring& message)
{
    WriteLine(L"[ERROR] " + message);
}

/// @brief リソースファイルの実際の場所を探して絶対パスを返します。
std::wstring ResolveAssetPath(const std::wstring& relativePath)
{
    namespace fs = std::filesystem;

    // 探索の起点：カレントディレクトリと、exe のあるフォルダ
    const fs::path searchRoots[] = {
        fs::current_path(),
        GetExecutableDirectory(),
    };

    for (const fs::path& root : searchRoots)
    {
        // 起点から親フォルダへ最大 5 階層さかのぼりながら探す。
        //   例) exe が build/x64/Debug/ にあるとき、
        //       build/x64/Debug/shaders → build/x64/shaders → … と探索が進む。
        fs::path current = root;
        for (int depth = 0; depth < 5; ++depth)
        {
            const fs::path candidate = current / relativePath;

            std::error_code ec; // 例外を投げない版のオーバーロードを使うためのエラー受け
            if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
            {
                return fs::absolute(candidate, ec).wstring();
            }

            if (!current.has_parent_path() || current.parent_path() == current)
            {
                break; // ドライブのルートまで到達した
            }
            current = current.parent_path();
        }
    }

    // どこにも見つからなかった。何を探したかを添えて失敗させる。
    const std::wstring wideMessage = L"アセットが見つかりません: " + relativePath;
    LogError(wideMessage);
    LogError(L"  カレントディレクトリ : " + fs::current_path().wstring());
    LogError(L"  実行ファイルの場所   : " + GetExecutableDirectory().wstring());

    throw std::runtime_error(
        "アセットファイルが見つかりません。shaders フォルダの場所と、"
        "デバッグ時の作業ディレクトリ設定を確認してください。");
}

} // namespace dx12
