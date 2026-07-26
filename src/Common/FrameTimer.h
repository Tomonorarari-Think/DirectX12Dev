//=============================================================================
// FrameTimer.h
//   フレーム時間を計測し、1 秒ごとに FPS をログ出力するクラス。
//
//   ■ なぜ必要か
//     フレームバッファリングのような「性能改善」は、
//     見た目が全く変わりません。改善したかどうかを目で確認できないと、
//     本当に効いているのか分からないまま進むことになります。
//     数値で見えるようにしておくのが目的です。
//
//   ■ 何を測っているか
//     1 回のメインループにかかった実時間（壁時計時間）です。
//     つまり「メッセージ処理 ＋ 描画 ＋ Present の待ち」の合計です。
//
//   ■ 注意：垂直同期が有効だと差が見えません
//     Present(1) は画面のリフレッシュに同期するため、
//     どんなに速く描けても 60Hz のモニタでは 16.6 ms / 60 FPS で頭打ちになります。
//     改善の効果を測りたいときは Renderer.cpp の kEnableVSync を false にして
//     上限を外してください。
//
//   ■ DirectX に依存しない
//     このクラスは標準ライブラリの <chrono> だけで作られています。
//     そのため Common/ に置いています（Graphics/ ではありません）。
//=============================================================================
#pragma once

#include <chrono>
#include <cstdint>

namespace dx12
{
class FrameTimer
{
public:
    FrameTimer() = default;

    //-------------------------------------------------------------------------
    // 毎フレーム 1 回だけ呼ぶ
    //   前回の呼び出しからの経過時間を計測し、
    //   1 秒ごとに平均 FPS とフレーム時間をログへ出力します。
    //-------------------------------------------------------------------------
    void Tick();

    // 直前のフレームにかかった秒数（アニメーション等に使う想定）
    double DeltaSeconds() const noexcept { return m_deltaSeconds; }

    // 直近 1 秒間の平均 FPS
    double FramesPerSecond() const noexcept { return m_framesPerSecond; }

private:
    //-------------------------------------------------------------------------
    // steady_clock を使う理由
    //   system_clock は「現在時刻」を表すため、時刻同期やサマータイムで
    //   逆行することがあります。経過時間の測定には、
    //   単調増加が保証されている steady_clock を使うのが正解です。
    //-------------------------------------------------------------------------
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint m_previousTime;      // 前フレームの時刻
    TimePoint m_lastReportTime;    // 最後にログを出した時刻

    // 初回の Tick では「前回」が存在しないため、計測を開始するだけにする
    bool m_started = false;

    uint32_t m_framesSinceReport = 0;    // 最後のログ出力からのフレーム数
    double   m_deltaSeconds      = 0.0;  // 直前フレームの所要秒数
    double   m_framesPerSecond   = 0.0;  // 直近 1 秒の平均 FPS
};

} // namespace dx12
