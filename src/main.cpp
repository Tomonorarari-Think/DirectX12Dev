//=============================================================================
// main.cpp
//   アプリケーションの入口。ウィンドウを作り、レンダラを初期化し、
//   メインループを回す。
//
//   ■ なぜ WinMain ではなく main なのか
//     本プロジェクトはリンカのサブシステムを「コンソール」に設定しています。
//     そのため、ウィンドウとは別にコンソールウィンドウが開き、
//     std::wcout で出したログをそこで確認できます。
//     初期化の各段階が目に見えるので、学習中は非常に有用です。
//
//     製品として配布する際は、サブシステムを「Windows」に変更し、
//     エントリポイントを wWinMain にします（コンソールが開かなくなります）。
//=============================================================================
#include "App/Window.h"
#include "Common/GraphicsCommon.h"
#include "Graphics/Renderer.h"

#include <format>
#include <io.h>       // _setmode
#include <fcntl.h>    // _O_U8TEXT
#include <iostream>

namespace
{
/// <summary>
/// ウィンドウの初期幅（ピクセル）。
/// </summary>
constexpr uint32_t kInitialWidth = 1280;

/// <summary>
/// ウィンドウの初期高さ（ピクセル）。
/// </summary>
constexpr uint32_t kInitialHeight = 720;

/// <summary>
/// タイトルバーに表示する文字列。
/// </summary>
constexpr const wchar_t* kWindowTitle = L"DirectX 12 Dev - Textured Cube";

/// <summary>
/// コンソールで日本語（UTF-16 のワイド文字）を正しく表示できるようにします。
/// </summary>
void SetupConsole()
{
    // _O_U8TEXT : ワイド文字を UTF-8 に変換して出力するモード
    ::_setmode(::_fileno(stdout), _O_U8TEXT);
    ::_setmode(::_fileno(stderr), _O_U8TEXT);
}
} // namespace


/// <summary>
/// アプリケーションのエントリポイント。
/// </summary>
/// <returns>正常終了なら 0、エラーが発生した場合は 1。</returns>
int main()
{
    SetupConsole();

    try
    {
        dx12::Log(L"===== DirectX 12 Dev : Textured Cube =====");

        // (1) ウィンドウの生成
        dx12::Window window;
        window.Create(kWindowTitle, kInitialWidth, kInitialHeight);

        // (2) レンダラの初期化
        //     ここで DirectX 12 の初期化が全て行われる。
        dx12::Renderer renderer;
        renderer.Initialize(window.Handle(), kInitialWidth, kInitialHeight);

        // (3) ウィンドウのサイズ変更をレンダラへ伝える
        //   ラムダ式で Window と Renderer を接続します。
        window.SetResizeCallback([&renderer](uint32_t width, uint32_t height) {
            renderer.Resize(width, height);
        });

        dx12::Log(L"メインループを開始します。ESC キーまたは × ボタンで終了します。");

        // (4) メインループ
        //   ゲームやリアルタイム描画アプリの基本構造です。
        while (window.ProcessMessages())
        {
            renderer.Render();
        }

        // (5) 終了処理
        //   ★ GPU の作業完了を待ってからリソースを解放する。
        renderer.WaitForGpu();

        dx12::Log(L"正常に終了しました。");
        return 0;
    }
    // 例外処理
    //   DX_CHECK が投げた HrException や、std::runtime_error をここで捕まえます。
    catch (const dx12::HrException& e)
    {
        dx12::LogError(L"DirectX の呼び出しでエラーが発生しました。");

        // what() は std::string（マルチバイト）なので、そのまま A 版 API へ渡す
        ::MessageBoxA(nullptr, e.what(), "DirectX Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    catch (const std::exception& e)
    {
        dx12::LogError(L"エラーが発生しました。");

        ::MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    catch (...)
    {
        // 型の分からない例外。通常は起きないが、握り潰さないよう受け止めておく。
        dx12::LogError(L"未知のエラーが発生しました。");

        ::MessageBoxW(nullptr, L"未知のエラーが発生しました。", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
