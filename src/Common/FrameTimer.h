//=============================================================================
// FrameTimer.h
//   フレーム時間の計測と FPS 表示。
//=============================================================================
#pragma once

#include <chrono>
#include <cstdint>

namespace dx12
{

/// @brief フレーム時間を計測し、1 秒ごとに FPS をログ出力するクラス。
///
/// **なぜ必要か**
///
/// フレームバッファリングのような「性能改善」は、見た目が全く変わりません。改善したかどうかを目で確
/// 認できないと、本当に効いているのか分からないまま進むことになります。数値で見えるようにしておくの
/// が目的です。
///
/// **何を測っているか**
///
/// 1 回のメインループにかかった実時間（壁時計時間）です。つまり「メッセージ処理 ＋ 描画 ＋ Present
/// の待ち」の合計です。
///
/// **注意：垂直同期が有効だと差が見えません**
///
/// `Present(1)` は画面のリフレッシュに同期するため、どんなに速く描けても 60Hz のモニタでは 16.6 ms
/// / 60 FPS で頭打ちになります。改善の効果を測りたいときは `Renderer.cpp` の `kEnableVSync` を
/// `false` にして上限を外してください。
///
/// **DirectX に依存しない**
///
/// このクラスは標準ライブラリの `<chrono>` だけで作られています。そのため `Common/` に置いています
/// （`Graphics/` ではありません）。
class FrameTimer
{
public:
    /// @brief 既定のコンストラクタ。最初の `Tick` で計測を開始します。
    FrameTimer() = default;

    /// @brief 毎フレーム 1 回だけ呼び出して、経過時間を更新します。
    ///
    /// 前回の呼び出しからの経過時間を計測し、1 秒ごとに平均 FPS とフレーム時間をログへ出力します。初回
    /// の呼び出しでは「前フレーム」が存在しないため、計測を開始するだけです。
    void Tick();

    /// @brief 直前のフレームにかかった秒数を取得します。
    /// @returns 経過秒数。アニメーションの進行量などに使います。
    double DeltaSeconds() const noexcept { return m_deltaSeconds; }

    /// @brief 直近 1 秒間の平均フレームレートを取得します。
    /// @returns 1 秒あたりのフレーム数。最初の 1 秒が経過するまでは 0。
    double FramesPerSecond() const noexcept { return m_framesPerSecond; }

    /// @brief 最初の `Tick` からの累計経過秒数を取得します。
    /// @returns 経過秒数。
    ///
    /// アニメーションの進行に使います。フレーム数ではなく時間を基準にすることで、フレームレートが変動し
    /// ても見た目の速さが一定になります。（フレーム数で進めると、120 fps の環境では 60 fps の 2 倍速く
    /// なってしまいます）
    double TotalSeconds() const noexcept { return m_totalSeconds; }

private:
    /// @brief 経過時間の計測に使う時計。
    ///
    /// `system_clock` は「現在時刻」を表すため、時刻同期やサマータイムで逆行することがあります。経過時
    /// 間の測定には、単調増加が保証されている `steady_clock` を使うのが正解です。
    using Clock = std::chrono::steady_clock;

    /// @brief 時刻を表す型（`Clock` の時点型）。
    using TimePoint = Clock::time_point;

    /// @brief 前フレームの時刻。
    TimePoint m_previousTime;

    /// @brief 最後に FPS ログを出力した時刻。
    TimePoint m_lastReportTime;

    /// @brief 計測を開始済みかどうか（初回の Tick を判別するため）。
    bool m_started = false;

    /// @brief 最後のログ出力からのフレーム数。
    uint32_t m_framesSinceReport = 0;

    /// @brief 直前フレームの所要秒数。
    double m_deltaSeconds = 0.0;

    /// @brief 計測開始からの累計経過秒数。
    double m_totalSeconds = 0.0;

    /// @brief 直近 1 秒間の平均 FPS。
    double m_framesPerSecond = 0.0;
};

} // namespace dx12
