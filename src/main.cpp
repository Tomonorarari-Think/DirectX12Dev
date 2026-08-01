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
#include "App/CameraController.h"
#include "App/Input.h"
#include "App/Window.h"
#include "Common/ComInitializer.h"
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
constexpr const wchar_t* kWindowTitle = L"DirectX 12 Dev - Interactive Camera";

/// <summary>
/// コンソールで日本語（UTF-16 のワイド文字）を正しく表示できるようにします。
/// </summary>
void SetupConsole()
{
    // _O_U8TEXT : ワイド文字を UTF-8 に変換して出力するモード
    ::_setmode(::_fileno(stdout), _O_U8TEXT);
    ::_setmode(::_fileno(stderr), _O_U8TEXT);
}


/// <summary>
/// 操作方法をコンソールへ表示します。
/// </summary>
void PrintControls()
{
    dx12::Log(L"操作方法");
    dx12::Log(L"  左ドラッグ        視点を回す");
    dx12::Log(L"  右／中ドラッグ    注視点ごと平行移動");
    dx12::Log(L"  ホイール          寄る・引く");
    dx12::Log(L"  W / S / A / D     視点を回す（キー操作）");
    dx12::Log(L"  R                 初期位置へ戻す");
    dx12::Log(L"  L                 習作モード（← → で切り替え）");
    dx12::Log(L"  F                 ディゾルブ");
    dx12::Log(L"  V                 半透明（VFX）");
    dx12::Log(L"  B                 半透明の並べ替え（対照実験）");
    dx12::Log(L"  N                 ソフトパーティクル（対照実験）");
    dx12::Log(L"  G                 GPU パーティクル");
    dx12::Log(L"  H                 GPU パーティクルの数（1024 → 16384）");
    dx12::Log(L"  E                 自動露出");
    dx12::Log(L"  K                 明るさの集計 Wave / 共有メモリ");
    dx12::Log(L"  J                 明るさを測る解像度（1/4 → 1/1）");
    dx12::Log(L"  M                 シーンを何回ぶん記録するか（計測用）");
    dx12::Log(L"  T                 垂直同期（速度計測用）");
    dx12::Log(L"  P                 動きを止める・再開する");
    dx12::Log(L"  ESC               終了");
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
        dx12::Log(L"===== DirectX 12 Dev : Interactive Camera =====");

        // (0) COM の初期化
        //   画像の読み込みに使う WIC が COM のオブジェクトなので、
        //   それより前に済ませておく必要があります。
        const dx12::ComInitializer comInitializer;

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

        // (4) 入力の受け取り口をウィンドウへ接続する
        //   ウィンドウプロシージャの中では「記録するだけ」で、
        //   実際の判断はメインループで行います。
        dx12::Input input;
        dx12::CameraController cameraController;

        window.SetMessageCallback([&input](UINT message, WPARAM wParam, LPARAM lParam) {
            return input.ProcessMessage(message, wParam, lParam);
        });

        PrintControls();

        // (5) メインループ
        //   ゲームやリアルタイム描画アプリの基本構造です。
        while (true)
        {
            // ★ メッセージを処理する前に、1 フレームの区切りを付ける。
            //   ここで前フレームの状態を保存し、移動量を 0 に戻します。
            //   順序を逆にすると、このフレームで届いた入力を捨ててしまいます。
            input.NewFrame();

            if (!window.ProcessMessages())
            {
                break;
            }

            // --- 習作モードの操作 -----------------------------------------
            //   L で入り切り、左右キーで前後、数字キーで直接選ぶ。
            if (input.WasKeyPressed('L'))
            {
                renderer.ToggleShaderLab();
            }

            // F : ディゾルブ（溶けて消える表現）の入り切り
            if (input.WasKeyPressed('F'))
            {
                renderer.ToggleDissolve();
            }

            // V : 半透明（VFX）の入り切り
            if (input.WasKeyPressed('V'))
            {
                renderer.ToggleVfx();
            }

            // B : 半透明の並べ替えの入り切り（対照実験用）
            if (input.WasKeyPressed('B'))
            {
                renderer.ToggleVfxSort();
            }

            // N : ソフトパーティクルの入り切り（対照実験用）
            if (input.WasKeyPressed('N'))
            {
                renderer.ToggleSoftParticles();
            }

            // G : GPU パーティクルの入り切り
            if (input.WasKeyPressed('G'))
            {
                renderer.ToggleGpuParticles();
            }

            // H : GPU パーティクルの数を切り替える（対照実験用）
            if (input.WasKeyPressed('H'))
            {
                renderer.CycleGpuParticleCount();
            }

            // T : 垂直同期の入り切り（速度計測用）
            if (input.WasKeyPressed('T'))
            {
                renderer.ToggleVSync();
            }

            // E : 自動露出の入り切り
            if (input.WasKeyPressed('E'))
            {
                renderer.ToggleAutoExposure();
            }

            // K : 明るさの集計を Wave / 共有メモリで切り替える（対照実験用）
            if (input.WasKeyPressed('K'))
            {
                renderer.ToggleReductionMode();
            }

            // J : 明るさを測る解像度を切り替える（対照実験用）
            if (input.WasKeyPressed('J'))
            {
                renderer.CycleExposureSampleRate();
            }

            // M : シーンを何回ぶん記録するか（計測用）
            if (input.WasKeyPressed('M'))
            {
                renderer.CycleMeshRepeatCount();
            }

            // P : 動きを止める・再開する（対照実験用）
            if (input.WasKeyPressed('P'))
            {
                renderer.ToggleTimePause();
            }

            if (renderer.IsShaderLabEnabled())
            {
                if (input.WasKeyPressed(VK_RIGHT)) { renderer.AdvanceShaderLab(+1); }
                if (input.WasKeyPressed(VK_LEFT))  { renderer.AdvanceShaderLab(-1); }

                // 1〜9 と 0（= 10 番目）
                for (int i = 0; i < 9; ++i)
                {
                    if (input.WasKeyPressed('1' + i)) { renderer.SelectShaderLab(i); }
                }
                if (input.WasKeyPressed('0')) { renderer.SelectShaderLab(9); }

                renderer.SetShaderLabMouse(
                    input.MouseX(), input.MouseY(),
                    input.IsMouseButtonDown(dx12::MouseButton::Left));
            }
            else
            {
                // 溜まった入力をもとにカメラを動かす。
                cameraController.Update(input, renderer.SceneCamera(),
                                        renderer.DeltaSeconds());
            }

            renderer.Render();
        }

        // (6) 終了処理
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
