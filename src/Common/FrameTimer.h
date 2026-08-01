//=============================================================================
// FrameTimer.h
//   フレーム時間の計測と FPS 表示。
//=============================================================================
#pragma once

#include <chrono>
#include <cstdint>

namespace dx12
{

/// <summary>
/// フレーム時間を計測し、1 秒ごとに FPS をログ出力するクラス。
/// </summary>
class FrameTimer
{
public:
    /// <summary>
    /// 既定のコンストラクタ。最初の `Tick` で計測を開始します。
    /// </summary>
    FrameTimer() = default;

    /// <summary>
    /// 毎フレーム 1 回だけ呼び出して、経過時間を更新します。
    /// </summary>
    void Tick();

    /// <summary>
    /// 直前のフレームにかかった秒数を取得します。
    /// </summary>
    /// <returns>経過秒数。アニメーションの進行量などに使います。</returns>
    double DeltaSeconds() const noexcept { return m_deltaSeconds; }

    /// <summary>
    /// 直近 1 秒間の平均フレームレートを取得します。
    /// </summary>
    /// <returns>1 秒あたりのフレーム数。最初の 1 秒が経過するまでは 0。</returns>
    double FramesPerSecond() const noexcept { return m_framesPerSecond; }

    /// <summary>
    /// このフレームで FPS のログを出したかどうかを返します。
    /// </summary>
    /// <returns>出したなら `true`。</returns>
    /// <remarks>
    /// GPU の計測結果を FPS と同じ間隔で出すために使います。
    /// 毎フレーム出すと、ログ出力自体が計測結果を歪めます。
    /// </remarks>
    bool ReportedThisFrame() const noexcept { return m_reportedThisFrame; }

    /// <summary>
    /// 最初の `Tick` からの累計経過秒数を取得します。
    /// </summary>
    /// <returns>経過秒数。</returns>
    double TotalSeconds() const noexcept { return m_totalSeconds; }

private:
    /// <summary>
    /// 経過時間の計測に使う時計。
    /// </summary>
    using Clock = std::chrono::steady_clock;

    /// <summary>
    /// 時刻を表す型（`Clock` の時点型）。
    /// </summary>
    using TimePoint = Clock::time_point;

    /// <summary>
    /// 前フレームの時刻。
    /// </summary>
    TimePoint m_previousTime;

    /// <summary>
    /// 最後に FPS ログを出力した時刻。
    /// </summary>
    TimePoint m_lastReportTime;

    /// <summary>
    /// 計測を開始済みかどうか（初回の Tick を判別するため）。
    /// </summary>
    bool m_started = false;

    /// <summary>
    /// 最後のログ出力からのフレーム数。
    /// </summary>
    uint32_t m_framesSinceReport = 0;

    /// <summary>
    /// 直前フレームの所要秒数。
    /// </summary>
    double m_deltaSeconds = 0.0;

    /// <summary>
    /// 計測開始からの累計経過秒数。
    /// </summary>
    double m_totalSeconds = 0.0;

    /// <summary>
    /// 直近 1 秒間の平均 FPS。
    /// </summary>
    double m_framesPerSecond = 0.0;

    /// <summary>
    /// このフレームで FPS のログを出したか。
    /// </summary>
    bool m_reportedThisFrame = false;
};

} // namespace dx12
