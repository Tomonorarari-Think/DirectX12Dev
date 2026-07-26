//=============================================================================
// GraphicsDevice.h
//   DirectX 12 の「土台」（デバッグレイヤー・DXGI・アダプタ・デバイス）。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// <summary>
/// DirectX 12 の土台（DXGI ファクトリ、GPU の選択、D3D12 デバイス）を作るクラス。
/// </summary>
/// <remarks>
/// <para>
/// <b>DXGI ファクトリ (IDXGIFactory)</b><br/>
/// GPU（アダプタ）を列挙したり、スワップチェーンを作ったりする窓口。
/// 「DirectX の受付カウンター」のような存在です。
/// </para>
/// <para>
/// <b>アダプタ (IDXGIAdapter)</b><br/>
/// 1 個の GPU を表すオブジェクト。PC には「CPU 内蔵 GPU」と
/// 「外付けの高性能 GPU」が両方載っていることがあり、
/// どちらを使うかをここで選びます。
/// </para>
/// <para>
/// <b>デバイス (ID3D12Device)</b><br/>
/// 選んだ GPU を操作するための本体。
/// 「〜を作る」系の関数（<c>CreateCommandQueue</c>、<c>CreateCommittedResource</c>、
/// <c>CreateGraphicsPipelineState</c> …）はほぼ全てこのデバイスが持っています。
/// DirectX 12 のあらゆる処理はデバイスから始まる、と覚えてください。
/// </para>
/// <para>
/// <b>デバッグレイヤー (ID3D12Debug)</b><br/>
/// DirectX 12 は性能最優先の API のため、既定では「間違った使い方」を
/// していても黙って壊れた絵を出す（あるいは即クラッシュする）だけです。
/// デバッグレイヤーを有効にすると、API の使い方の誤りを検査して
/// 詳細なエラーメッセージを出力してくれます。
/// DirectX 12 学習において最も重要な機能と言っても過言ではありません。
/// 動作は非常に遅くなるため、Debug ビルドでのみ有効にします。
/// </para>
/// </remarks>
class GraphicsDevice
{
public:
    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    GraphicsDevice() = default;

    /// <summary>デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。</summary>
    ~GraphicsDevice();

    /// <summary>コピー構築は禁止です（GPU リソースの二重解放を防ぐため）。</summary>
    GraphicsDevice(const GraphicsDevice&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    /// <summary>DirectX 12 の土台を初期化します。</summary>
    /// <exception cref="HrException">DXGI ファクトリまたはデバイスの生成に失敗した場合。</exception>
    /// <remarks>
    /// 処理順序（この順番でなければならない）:
    /// <list type="number">
    ///   <item>デバッグレイヤーを有効化 … デバイス生成より前でないと効かない</item>
    ///   <item>DXGI ファクトリを生成</item>
    ///   <item>使用する GPU（アダプタ）を選択</item>
    ///   <item>D3D12 デバイスを生成</item>
    ///   <item>情報キューを設定（エラー時にデバッガを止める）</item>
    /// </list>
    /// </remarks>
    void Initialize();

    /// <summary>生成済みの D3D12 デバイスを取得します。</summary>
    /// <returns>デバイスの生ポインタ（所有権は移動しません）。</returns>
    ID3D12Device* Device() const noexcept { return m_device.Get(); }

    /// <summary>生成済みの DXGI ファクトリを取得します。</summary>
    /// <returns>ファクトリの生ポインタ（所有権は移動しません）。</returns>
    IDXGIFactory6* Factory() const noexcept { return m_factory.Get(); }

private:
    /// <summary>デバッグレイヤーを有効化します（Debug ビルドのみ）。</summary>
    /// <remarks>
    /// <para>
    /// <b>なぜデバイス生成より前でなければならないのか</b><br/>
    /// デバッグレイヤーは「デバイス生成時に、検証機能付きの実装に差し替える」
    /// という仕組みで働きます。デバイスを作った後に有効化しても、
    /// そのデバイスは通常版のままなので何も検証されません。
    /// </para>
    /// <para>
    /// Windows の「オプション機能」に <i>Graphics Tools</i> が入っていないと
    /// 有効化に失敗します。その場合でもアプリは動作するため、
    /// 例外は投げず警告ログに留めます。
    /// </para>
    /// </remarks>
    void EnableDebugLayer();

    /// <summary>DXGI ファクトリを生成します。</summary>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void CreateFactory();

    /// <summary>描画に使う GPU（アダプタ）を選択します。</summary>
    /// <exception cref="HrException">アダプタの列挙に失敗した場合。</exception>
    /// <remarks>
    /// 高性能な GPU から順に試し、D3D12 デバイスを作れる最初の 1 台を採用します。
    /// 実 GPU が 1 つも使えない場合は WARP（CPU によるソフトウェア実装）へ切り替えます。
    /// </remarks>
    void SelectAdapter();

    /// <summary>選択したアダプタから D3D12 デバイスを生成します。</summary>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void CreateDevice();

    /// <summary>デバッグメッセージの扱いを設定します（Debug ビルドのみ）。</summary>
    /// <remarks>
    /// 深刻度が ERROR 以上のメッセージが出た瞬間にデバッガを停止させます。
    /// 設定しないと、エラーは出力ウィンドウに流れるだけで実行が続行され、
    /// 「原因の場所」ではなく「はるか後の別の場所」でクラッシュします。
    /// </remarks>
    void ConfigureInfoQueue();

private:
    /// <summary>DXGI の受付カウンター（アダプタ列挙・スワップチェーン生成に使う）。</summary>
    ComPtr<IDXGIFactory6> m_factory;

    /// <summary>選択した GPU。</summary>
    ComPtr<IDXGIAdapter1> m_adapter;

    /// <summary>D3D12 本体。あらゆる生成処理の起点。</summary>
    ComPtr<ID3D12Device> m_device;

    /// <summary>デバッグレイヤーを有効化できたか（Release ビルドでは常に false）。</summary>
    bool m_debugLayerEnabled = false;
};

} // namespace dx12
