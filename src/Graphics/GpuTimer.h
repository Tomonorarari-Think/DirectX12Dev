//=============================================================================
// GpuTimer.h
//   GPU がどのパスに何ミリ秒使ったかを測る仕組み。
//
//   CPU 側のフレーム時間では「GPU の中でどこが重いか」が分からない。
//   タイムスタンプクエリを使うと、パス単位で GPU の実時間が取れる。
//   詳しい解説は docs/tutorial/30_GPUの時間を測る.md を参照。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

#include <array>
#include <string>

namespace dx12
{

/// <summary>
/// 時間を測る区間。並びは `GpuTimer::kPassNames` と一致させること。
/// </summary>
enum class GpuPass : uint32_t
{
    /// <summary>パーティクルの更新（コンピュート）。</summary>
    ParticleUpdate = 0,

    /// <summary>影のパス。</summary>
    Shadow,

    /// <summary>不透明な物の描画。</summary>
    Scene,

    /// <summary>背景。</summary>
    Skybox,

    /// <summary>半透明（板と GPU パーティクル）。</summary>
    Transparent,

    /// <summary>後処理。</summary>
    PostProcess,

    /// <summary>フレーム全体。個々のパスの合計との差が「その他」になる。</summary>
    Frame,

    /// <summary>区間の数。列挙の末尾に置くこと。</summary>
    Count,
};


/// <summary>
/// GPU の処理時間をパスごとに測るクラス。
/// </summary>
/// <remarks>
/// **測れるのは GPU が実際に動いた時間だけです。** CPU の記録時間や、
/// 垂直同期の待ち時間は含まれません。フレーム時間との差がそのまま
/// 「GPU 以外に使われた時間」になります。
/// </remarks>
class GpuTimer
{
public:
    /// <summary>測れる区間の数。</summary>
    static constexpr uint32_t kPassCount = static_cast<uint32_t>(GpuPass::Count);

    /// <summary>区間 1 つにつき、開始と終了で 2 個のクエリを使う。</summary>
    static constexpr uint32_t kQueriesPerPass = 2;

    /// <summary>1 フレームで使うクエリの数。</summary>
    static constexpr uint32_t kQueriesPerFrame = kPassCount * kQueriesPerPass;

    /// <summary>区間の表示名。`GpuPass` と並びを合わせること。</summary>
    static const std::array<const wchar_t*, kPassCount> kPassNames;

    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    GpuTimer() = default;

    /// <summary>デストラクタ。ComPtr により COM オブジェクトが自動解放されます。</summary>
    ~GpuTimer() = default;

    /// <summary>コピー構築は禁止です。</summary>
    GpuTimer(const GpuTimer&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    GpuTimer& operator=(const GpuTimer&) = delete;

    /// <summary>
    /// クエリヒープと読み出し用バッファを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="commandQueue">
    /// 時間を測るキュー。ここから 1 秒あたりの刻み数を取得します。
    /// </param>
    /// <param name="frameCount">同時に処理するフレーム数。</param>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void Initialize(ID3D12Device* device,
                    ID3D12CommandQueue* commandQueue,
                    uint32_t frameCount);

    /// <summary>
    /// 区間の開始を記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">フレーム番号。</param>
    /// <param name="pass">測る区間。</param>
    void Begin(ID3D12GraphicsCommandList* commandList,
               uint32_t frameIndex,
               GpuPass pass);

    /// <summary>
    /// 区間の終了を記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">フレーム番号。</param>
    /// <param name="pass">測る区間。`Begin` と同じものを渡します。</param>
    void End(ID3D12GraphicsCommandList* commandList,
             uint32_t frameIndex,
             GpuPass pass);

    /// <summary>
    /// 測った値を読み出し用バッファへ書き出す命令を記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="frameIndex">フレーム番号。</param>
    /// <remarks>
    /// **コマンドリストを閉じる直前に呼びます。** クエリの値は
    /// このコマンドで移して初めて CPU から読めるようになります。
    /// </remarks>
    void Resolve(ID3D12GraphicsCommandList* commandList, uint32_t frameIndex);

    /// <summary>
    /// 前回このフレーム番号で測った結果を読み取ります。
    /// </summary>
    /// <param name="frameIndex">フレーム番号。</param>
    /// <remarks>
    /// **そのフレーム番号のフェンスを待ったあとに呼びます。**
    /// まだ GPU が書いている領域を読むと、値が壊れます。
    /// </remarks>
    void Collect(uint32_t frameIndex);

    /// <summary>
    /// 直近に読み取った区間の時間をミリ秒で返します。
    /// </summary>
    /// <param name="pass">知りたい区間。</param>
    /// <returns>ミリ秒。まだ測れていなければ 0。</returns>
    double Milliseconds(GpuPass pass) const;

    /// <summary>
    /// 全区間の結果を 1 行の文字列にまとめます。
    /// </summary>
    /// <returns>ログにそのまま出せる文字列。</returns>
    std::wstring Format() const;

    /// <summary>
    /// 使えるかどうかを返します。
    /// </summary>
    /// <returns>初期化に成功していれば `true`。</returns>
    bool IsAvailable() const noexcept { return m_queryHeap != nullptr; }

private:
    /// <summary>
    /// 指定した区間のクエリ番号（開始側）を返します。
    /// </summary>
    /// <param name="frameIndex">フレーム番号。</param>
    /// <param name="pass">区間。</param>
    /// <returns>クエリヒープ内の通し番号。</returns>
    uint32_t QueryIndex(uint32_t frameIndex, GpuPass pass) const;

private:
    /// <summary>タイムスタンプを書き込むクエリヒープ。</summary>
    ComPtr<ID3D12QueryHeap> m_queryHeap;

    /// <summary>クエリの値を CPU から読むためのバッファ。</summary>
    ComPtr<ID3D12Resource> m_readbackBuffer;

    /// <summary>1 秒あたりの刻み数。刻みを秒へ直すのに使う。</summary>
    uint64_t m_ticksPerSecond = 0;

    /// <summary>同時に処理するフレーム数。</summary>
    uint32_t m_frameCount = 0;

    /// <summary>直近に読み取った区間ごとの時間（ミリ秒）。</summary>
    std::array<double, kPassCount> m_milliseconds = {};
};

} // namespace dx12
