//=============================================================================
// FrameTimer.cpp
//   FrameTimer の実装。
//=============================================================================
#include "FrameTimer.h"

#include "GraphicsCommon.h"

#include <format>

namespace dx12
{
namespace
{
/// <summary>
/// FPS をログ出力する間隔（秒）。
/// </summary>
constexpr double kReportIntervalSeconds = 1.0;
} // namespace


/// <summary>
/// 毎フレーム 1 回だけ呼び出して、経過時間を更新します。
/// </summary>
void FrameTimer::Tick()
{
    const TimePoint now = Clock::now();

    // 初回はここで計測を開始するだけ。
    if (!m_started)
    {
        m_started         = true;
        m_previousTime    = now;
        m_lastReportTime  = now;
        return;
    }

    m_reportedThisFrame = false;

    // 前フレームからの経過時間
    //   duration<double> は「秒を double で表す時間差」の型。
    m_deltaSeconds = std::chrono::duration<double>(now - m_previousTime).count();
    m_previousTime = now;

    // 累計時間。アニメーションの進行はフレーム数ではなくこの値を基準にする。
    m_totalSeconds += m_deltaSeconds;

    ++m_framesSinceReport;

    // 一定時間ごとに平均を計算してログ出力する
    //   毎フレーム出力すると、ログ出力自体が重くて計測結果を歪めます。
    const double elapsedSinceReport =
        std::chrono::duration<double>(now - m_lastReportTime).count();

    if (elapsedSinceReport >= kReportIntervalSeconds)
    {
        m_framesPerSecond = static_cast<double>(m_framesSinceReport) / elapsedSinceReport;

        // 1 フレームあたりの平均時間をミリ秒で表示する。
        const double averageFrameMilliseconds = 1000.0 / m_framesPerSecond;

        Log(std::format(L"FPS: {:.1f}  ({:.2f} ms / frame)",
                        m_framesPerSecond, averageFrameMilliseconds));

        m_lastReportTime    = now;
        m_framesSinceReport = 0;
        m_reportedThisFrame = true;
    }
}

} // namespace dx12
