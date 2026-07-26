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
/// <remarks>
/// <para>
/// <b>なぜ必要か</b><br/>
/// フレームバッファリングのような「性能改善」は、見た目が全く変わりません。
/// 改善したかどうかを目で確認できないと、本当に効いているのか分からないまま
/// 進むことになります。数値で見えるようにしておくのが目的です。
/// </para>
/// <para>
/// <b>何を測っているか</b><br/>
/// 1 回のメインループにかかった実時間（壁時計時間）です。
/// つまり「メッセージ処理 ＋ 描画 ＋ Present の待ち」の合計です。
/// </para>
/// <para>
/// <b>注意：垂直同期が有効だと差が見えません</b><br/>
/// <c>Present(1)</c> は画面のリフレッシュに同期するため、
/// どんなに速く描けても 60Hz のモニタでは 16.6 ms / 60 FPS で頭打ちになります。
/// 改善の効果を測りたいときは <c>Renderer.cpp</c> の <c>kEnableVSync</c> を
/// <c>false</c> にして上限を外してください。
/// </para>
/// <para>
/// <b>DirectX に依存しない</b><br/>
/// このクラスは標準ライブラリの <c>&lt;chrono&gt;</c> だけで作られています。
/// そのため <c>Common/</c> に置いています（<c>Graphics/</c> ではありません）。
/// </para>
/// </remarks>
class FrameTimer
{
public:
    /// <summary>既定のコンストラクタ。最初の <see cref="Tick"/> で計測を開始します。</summary>
    FrameTimer() = default;

    /// <summary>毎フレーム 1 回だけ呼び出して、経過時間を更新します。</summary>
    /// <remarks>
    /// 前回の呼び出しからの経過時間を計測し、
    /// 1 秒ごとに平均 FPS とフレーム時間をログへ出力します。
    /// 初回の呼び出しでは「前フレーム」が存在しないため、計測を開始するだけです。
    /// </remarks>
    void Tick();

    /// <summary>直前のフレームにかかった秒数を取得します。</summary>
    /// <returns>経過秒数。アニメーションの進行量などに使います。</returns>
    double DeltaSeconds() const noexcept { return m_deltaSeconds; }

    /// <summary>直近 1 秒間の平均フレームレートを取得します。</summary>
    /// <returns>1 秒あたりのフレーム数。最初の 1 秒が経過するまでは 0。</returns>
    double FramesPerSecond() const noexcept { return m_framesPerSecond; }

    /// <summary>最初の <see cref="Tick"/> からの累計経過秒数を取得します。</summary>
    /// <returns>経過秒数。</returns>
    /// <remarks>
    /// アニメーションの進行に使います。フレーム数ではなく時間を基準にすることで、
    /// フレームレートが変動しても見た目の速さが一定になります。
    /// （フレーム数で進めると、120 fps の環境では 60 fps の 2 倍速くなってしまいます）
    /// </remarks>
    double TotalSeconds() const noexcept { return m_totalSeconds; }

private:
    /// <summary>
    /// 経過時間の計測に使う時計。
    /// </summary>
    /// <remarks>
    /// <c>system_clock</c> は「現在時刻」を表すため、時刻同期やサマータイムで
    /// 逆行することがあります。経過時間の測定には、
    /// 単調増加が保証されている <c>steady_clock</c> を使うのが正解です。
    /// </remarks>
    using Clock = std::chrono::steady_clock;

    /// <summary>時刻を表す型（<see cref="Clock"/> の時点型）。</summary>
    using TimePoint = Clock::time_point;

    /// <summary>前フレームの時刻。</summary>
    TimePoint m_previousTime;

    /// <summary>最後に FPS ログを出力した時刻。</summary>
    TimePoint m_lastReportTime;

    /// <summary>計測を開始済みかどうか（初回の Tick を判別するため）。</summary>
    bool m_started = false;

    /// <summary>最後のログ出力からのフレーム数。</summary>
    uint32_t m_framesSinceReport = 0;

    /// <summary>直前フレームの所要秒数。</summary>
    double m_deltaSeconds = 0.0;

    /// <summary>計測開始からの累計経過秒数。</summary>
    double m_totalSeconds = 0.0;

    /// <summary>直近 1 秒間の平均 FPS。</summary>
    double m_framesPerSecond = 0.0;
};

} // namespace dx12
