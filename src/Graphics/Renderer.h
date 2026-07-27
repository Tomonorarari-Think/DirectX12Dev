//=============================================================================
// Renderer.h
//   デバイス／キュー／スワップチェーン／パイプラインを束ね、1 フレームを描く。
//=============================================================================
#pragma once

#include "../App/Camera.h"
#include "../Common/FrameTimer.h"
#include "../Common/GraphicsCommon.h"
#include "CommandQueue.h"
#include "DepthBuffer.h"
#include "DescriptorHeap.h"
#include "GraphicsDevice.h"
#include "SwapChain.h"
#include "MeshPipeline.h"

#include <array>

namespace dx12
{

/// <summary>
/// これまでの部品を束ね、「1 フレーム描く」という仕事を担当するクラス。
/// </summary>
class Renderer
{
public:
    /// <summary>
    /// 既定のコンストラクタ。まだ何も生成されません。
    /// </summary>
    Renderer() = default;

    /// <summary>
    /// デストラクタ。破棄の前に GPU の作業完了を待ちます。
    /// </summary>
    ~Renderer();

    /// <summary>
    /// コピー構築は禁止です。
    /// </summary>
    Renderer(const Renderer&) = delete;

    /// <summary>
    /// コピー代入は禁止です。
    /// </summary>
    Renderer& operator=(const Renderer&) = delete;

    /// <summary>
    /// DirectX 12 の初期化を一式行います。
    /// </summary>
    /// <param name="hwnd">描画先のウィンドウ。</param>
    /// <param name="width">初期の描画解像度（幅、ピクセル）。</param>
    /// <param name="height">初期の描画解像度（高さ、ピクセル）。</param>
    /// <exception cref="HrException">DirectX の初期化に失敗した場合。</exception>
    /// <exception cref="std::runtime_error">シェーダーファイルが見つからない場合。</exception>
    void Initialize(HWND hwnd, uint32_t width, uint32_t height);

    /// <summary>
    /// 1 フレーム描画して画面に表示します。
    /// </summary>
    /// <exception cref="HrException">コマンドの記録または投入に失敗した場合。</exception>
    void Render();

    /// <summary>
    /// ウィンドウサイズ変更に追従します。
    /// </summary>
    /// <param name="width">新しい幅（ピクセル）。</param>
    /// <param name="height">新しい高さ（ピクセル）。</param>
    void Resize(uint32_t width, uint32_t height);

    /// <summary>
    /// GPU の全作業完了を待ちます（終了時に必ず呼ぶこと）。
    /// </summary>
    void WaitForGpu();

private:
    /// <summary>
    /// コマンドアロケータ（バックバッファ枚数ぶん）とコマンドリストを生成します。
    /// </summary>
    /// <exception cref="HrException">生成に失敗した場合。</exception>
    void CreateCommandObjects();

    /// <summary>
    /// ビューポートとシザー矩形をウィンドウサイズに合わせて更新します。
    /// </summary>
    /// <param name="width">新しい幅（ピクセル）。</param>
    /// <param name="height">新しい高さ（ピクセル）。</param>
    void UpdateViewportAndScissor(uint32_t width, uint32_t height);

    /// <summary>
    /// リソースの状態遷移バリアをコマンドリストに記録します。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="resource">状態を変えるリソース。</param>
    /// <param name="stateBefore">現在の状態。実際の状態と一致している必要があります。</param>
    /// <param name="stateAfter">これから使う状態。</param>
    static void RecordResourceBarrier(ID3D12GraphicsCommandList* commandList,
                                      ID3D12Resource* resource,
                                      D3D12_RESOURCE_STATES stateBefore,
                                      D3D12_RESOURCE_STATES stateAfter);

private:
    /// <summary>
    /// デバイス・DXGI ファクトリ。
    /// </summary>
    GraphicsDevice m_graphicsDevice;

    /// <summary>
    /// コマンドキューとフェンス。
    /// </summary>
    CommandQueue m_commandQueue;

    /// <summary>
    /// スワップチェーンとバックバッファ・RTV。
    /// </summary>
    SwapChain m_swapChain;

    /// <summary>
    /// 奥行き判定に使う深度バッファと DSV。
    /// </summary>
    DepthBuffer m_depthBuffer;

    /// <summary>
    /// 立方体描画用の PSO と頂点/インデックス/定数バッファ、テクスチャ。
    /// </summary>
    MeshPipeline m_meshPipeline;

    /// <summary>
    /// 視点と透視投影の設定。
    /// </summary>
    Camera m_camera;

    /// <summary>
    /// テクスチャなどをシェーダーへ渡すためのディスクリプタヒープ。
    /// </summary>
    DescriptorHeap m_descriptorHeap;

    /// <summary>
    /// コマンドアロケータ（バックバッファの枚数ぶん用意する）。
    /// </summary>
    std::array<ComPtr<ID3D12CommandAllocator>, SwapChain::kBackBufferCount> m_commandAllocators;

    /// <summary>
    /// 命令を記録するためのコマンドリスト。
    /// </summary>
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    /// <summary>
    /// フレームごとのフェンス値（完了印）。
    /// </summary>
    std::array<uint64_t, SwapChain::kBackBufferCount> m_frameFenceValues = {};

    /// <summary>
    /// フレーム時間の計測（1 秒ごとに FPS をログ出力する）。
    /// </summary>
    FrameTimer m_frameTimer;

    /// <summary>
    /// バックバッファ上の「描画に使う矩形領域」と深度範囲。
    /// </summary>
    D3D12_VIEWPORT m_viewport = {};

    /// <summary>
    /// この矩形の外に出たピクセルを問答無用で捨てる設定。
    /// </summary>
    D3D12_RECT m_scissorRect = {};

    /// <summary>
    /// 初期化が完了しているかどうか。
    /// </summary>
    bool m_initialized = false;
};

} // namespace dx12
