//=============================================================================
// Renderer.h
//   デバイス／キュー／スワップチェーン／パイプラインを束ね、1 フレームを描く。
//=============================================================================
#pragma once

#include "../App/Camera.h"
#include "../Assets/ImageLoader.h"
#include "../Assets/ModelLoader.h"
#include "../Common/FrameTimer.h"
#include "../Common/GraphicsCommon.h"
#include "CommandQueue.h"
#include "DepthBuffer.h"
#include "DescriptorHeap.h"
#include "GraphicsDevice.h"
#include "MaterialSet.h"
#include "Mesh.h"
#include "ShadowMap.h"
#include "SwapChain.h"
#include "MeshPipeline.h"
#include "SkyboxPipeline.h"
#include "PostProcessPipeline.h"
#include "ShaderLabPipeline.h"
#include "GpuParticleSystem.h"
#include "AutoExposure.h"
#include "GpuTimer.h"
#include "VfxPipeline.h"
#include "RenderTexture.h"

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

    /// <summary>
    /// シーンを見ているカメラを取得します。
    /// </summary>
    /// <returns>カメラへの参照。外から位置や注視点を変えられます。</returns>
    /// <remarks>`CameraController` が操作するために公開しています。</remarks>
    Camera& SceneCamera() noexcept { return m_camera; }

    /// <summary>習作モードの入り切りを切り替えます。</summary>
    void ToggleShaderLab();

    /// <summary>ディゾルブ（溶けて消える表現）の入り切りを切り替えます。</summary>
    void ToggleDissolve();

    /// <summary>半透明（VFX）の入り切りを切り替えます。</summary>
    void ToggleVfx();

    /// <summary>半透明の並べ替えの入り切りを切り替えます。</summary>
    /// <remarks>並べ替えないとどうなるかを確かめるための切り替えです。</remarks>
    void ToggleVfxSort();

    /// <summary>ソフトパーティクルの入り切りを切り替えます。</summary>
    /// <remarks>切ると、板が床を貫いた線がはっきり出ます（対照実験用）。</remarks>
    void ToggleSoftParticles();

    /// <summary>GPU パーティクルの入り切りを切り替えます。</summary>
    void ToggleGpuParticles();

    /// <summary>GPU パーティクルの数を 1024 → 4096 → 16384 と切り替えます。</summary>
    /// <remarks>数を変えても CPU の仕事が増えないことを確かめるための機能です。</remarks>
    void CycleGpuParticleCount();

    /// <summary>垂直同期の入り切りを切り替えます。</summary>
    /// <remarks>
    /// **速度を測るための機能です。** 垂直同期が有効なうちは、
    /// 描画がどれだけ速くても表示の間隔で頭打ちになり、差が見えません。
    /// </remarks>
    void ToggleVSync();

    /// <summary>自動露出の入り切りを切り替えます。</summary>
    void ToggleAutoExposure();

    /// <summary>明るさのまとめ方（Wave / 共有メモリ）を切り替えます。</summary>
    /// <remarks>Wave 命令がどれだけ効くかを測るための切り替えです。</remarks>
    void ToggleReductionMode();

    /// <summary>明るさを測る解像度を切り替えます。</summary>
    /// <remarks>集計の量を変えて、Wave 命令の効き目を測るための機能です。</remarks>
    void CycleExposureSampleRate();

    /// <summary>シーンを何回ぶん記録するかを 1 → 16 → 128 と切り替えます。</summary>
    /// <remarks>
    /// **記録に掛かる CPU 時間を測るための機能です。** 同じものを何度も
    /// 描くので絵は変わりませんが、命令の数だけが増えます。
    /// </remarks>
    void CycleMeshRepeatCount();

    /// <summary>動きを止める・再開するを切り替えます。</summary>
    /// <remarks>
    /// **対照実験のための機能です。** 時間を止めておくと、設定を切り替えても
    /// まったく同じ瞬間の絵を撮り比べられます。止めないと、比べた差の中に
    /// 「時間が進んだぶんの違い」が混ざります。
    /// </remarks>
    void ToggleTimePause();

    /// <summary>ディゾルブが有効かを返します。</summary>
    /// <returns>有効なら `true`。</returns>
    bool IsDissolveEnabled() const noexcept { return m_dissolveEnabled; }

    /// <summary>習作モードかどうかを返します。</summary>
    /// <returns>習作モードなら `true`。</returns>
    bool IsShaderLabEnabled() const noexcept { return m_shaderLabEnabled; }

    /// <summary>表示する習作を前後に動かします。</summary>
    /// <param name="delta">+1 で次、-1 で前。</param>
    void AdvanceShaderLab(int delta);

    /// <summary>表示する習作を番号で選びます。</summary>
    /// <param name="index">0 から始まる番号。</param>
    void SelectShaderLab(int index);

    /// <summary>
    /// 習作へ渡すマウスの位置を設定します。
    /// </summary>
    /// <param name="x">X 座標（ピクセル）。</param>
    /// <param name="y">Y 座標（ピクセル）。</param>
    /// <param name="down">左ボタンが押されていれば `true`。</param>
    void SetShaderLabMouse(float x, float y, bool down) noexcept
    {
        m_shaderLabMouseX = x;
        m_shaderLabMouseY = y;
        m_shaderLabMouseDown = down;
    }

    /// <summary>
    /// 直前のフレームにかかった秒数を取得します。
    /// </summary>
    /// <returns>経過秒数。</returns>
    /// <remarks>
    /// 計測は `Render()` の先頭で行うため、返るのは 1 つ前のフレームの値です。
    /// 操作量を時間に比例させる用途には、これで十分です。
    /// </remarks>
    float DeltaSeconds() const noexcept
    {
        return static_cast<float>(m_frameTimer.DeltaSeconds());
    }

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
    /// 環境マップと、そこから求めた環境光を用意します。
    /// </summary>
    /// <exception cref="HrException">リソースの生成または転送に失敗した場合。</exception>
    /// <remarks>
    /// 画像が読めなかった場合は、灰色 1 色の環境で代用します。
    /// </remarks>
    void CreateEnvironment();

    /// <summary>
    /// 床に貼るテクスチャを用意します。
    /// </summary>
    /// <returns>RGBA8 のピクセル列と、その大きさ。</returns>
    /// <remarks>
    /// 画像ファイルを読み、失敗したらコードで市松模様を作って代用します。
    /// </remarks>
    assets::ImageData CreateFloorTexture();

    /// <summary>
    /// 1 つのメッシュを、材質ごとに区切って描きます。
    /// </summary>
    /// <param name="commandList">記録先のコマンドリスト。</param>
    /// <param name="mesh">描くメッシュ。</param>
    /// <param name="materials">そのメッシュが使う材質。</param>
    void RecordMeshWithMaterials(ID3D12GraphicsCommandList* commandList,
                                 const Mesh& mesh,
                                 const MaterialSet& materials);

    /// <summary>
    /// シーンに置くメッシュを生成します。
    /// </summary>
    /// <exception cref="HrException">リソースの生成または転送に失敗した場合。</exception>
    void CreateSceneMeshes();

    /// <summary>
    /// このフレームぶんの定数バッファを更新します。
    /// </summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    void UpdateConstants(uint32_t frameIndex);

    /// <summary>
    /// シーンの全メッシュを描く命令をコマンドリストに記録します。
    /// </summary>
    /// <param name="frameIndex">使用するフレーム番号。</param>
    /// <remarks>
    /// 共通設定（PSO・ルートシグネチャ）は呼び出し側が先に済ませておくこと。
    /// 影のパスと画面のパスで、まったく同じ順序のメッシュを描きます。
    /// </remarks>
    void RecordMeshDrawCommands(uint32_t frameIndex);

    /// <summary>
    /// 半透明の板を組み立て、描く命令を記録します。
    /// </summary>
    /// <param name="frameIndex">使用するフレーム番号。</param>
    /// <remarks>
    /// 不透明な物と背景をすべて描いたあとに呼びます。
    /// アルファ合成の板は、視点から遠い順に並べ替えてから描きます。
    /// </remarks>
    void RecordVfxDrawCommands(uint32_t frameIndex);

    /// <summary>
    /// 光源から見た深度をシャドウマップへ描きます。
    /// </summary>
    /// <param name="frameIndex">使用するフレーム番号。</param>
    void RecordShadowPass(uint32_t frameIndex);

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
    /// メッシュの描き方（PSO・ルートシグネチャ・定数バッファ・テクスチャ）。
    /// </summary>
    MeshPipeline m_meshPipeline;

    /// <summary>背景に環境マップを描くパイプライン。</summary>
    SkyboxPipeline m_skyboxPipeline;

    /// <summary>シーンを描き込む中間バッファ（HDR）。</summary>
    /// <remarks>
    /// 画面へ直接描かず、いったんここへ描いてから後処理を通します。
    /// 1.0 を超える明るさを保ったまま次の処理へ渡すためです。
    /// </remarks>
    RenderTexture m_sceneTexture;

    /// <summary>後処理（露出・ブルーム・トーンマップ・ビネット）。</summary>
    PostProcessPipeline m_postProcess;

    /// <summary>習作シェーダーを全画面に描くパイプライン。</summary>
    ShaderLabPipeline m_shaderLab;

    /// <summary>半透明のビルボードを描くパイプライン。</summary>
    VfxPipeline m_vfxPipeline;

    /// <summary>加算合成で描く板（光）。</summary>
    std::vector<VfxParticle> m_additiveParticles;

    /// <summary>アルファ合成で描く板（煙）。</summary>
    std::vector<VfxParticle> m_alphaParticles;

    /// <summary>半透明を描くかどうか。</summary>
    bool m_vfxEnabled = true;

    /// <summary>アルファ合成の板を奥から並べ替えるかどうか。</summary>
    /// <remarks>対照実験のために切り替えられるようにしています。</remarks>
    bool m_vfxSortEnabled = true;

    /// <summary>ソフトパーティクル（深度で薄める）を使うかどうか。</summary>
    bool m_softParticlesEnabled = true;

    /// <summary>GPU の処理時間をパスごとに測る道具。</summary>
    GpuTimer m_gpuTimer;

    /// <summary>シーンを何回ぶん記録するか（計測用）。</summary>
    uint32_t m_meshRepeatCount = 1;

    /// <summary>直近 1 秒間の、メッシュ記録に掛かった CPU 時間の合計（マイクロ秒）。</summary>
    double m_meshRecordMicroseconds = 0.0;

    /// <summary>直近 1 秒間に記録したフレーム数。</summary>
    uint32_t m_meshRecordSamples = 0;

    /// <summary>画面の明るさから露出を決める仕組み。</summary>
    AutoExposure m_autoExposure;

    /// <summary>自動露出を使うかどうか。</summary>
    bool m_autoExposureEnabled = true;

    /// <summary>明るさのまとめ方。</summary>
    ReductionMode m_reductionMode = ReductionMode::Wave;

    /// <summary>GPU 上で動かすパーティクル。</summary>
    GpuParticleSystem m_gpuParticles;

    /// <summary>GPU パーティクルを描くかどうか。</summary>
    bool m_gpuParticlesEnabled = true;

    /// <summary>いま動かしている GPU パーティクルの数。</summary>
    uint32_t m_gpuParticleCount = 1024;

    /// <summary>垂直同期を使うかどうか。`T` キーで切り替えます。</summary>
    bool m_vsyncEnabled = true;

    /// <summary>動きに使う時計（秒）。止められる点がフレーム時間と違います。</summary>
    double m_animationTime = 0.0;

    /// <summary>動きを止めているかどうか。</summary>
    bool m_timePaused = false;

    /// <summary>習作へ渡すマウスの X 座標。</summary>
    float m_shaderLabMouseX = 0.0f;

    /// <summary>習作へ渡すマウスの Y 座標。</summary>
    float m_shaderLabMouseY = 0.0f;

    /// <summary>習作へ渡すマウスの左ボタンの状態。</summary>
    bool m_shaderLabMouseDown = false;

    /// <summary>ディゾルブを動かすかどうか。</summary>
    /// <remarks>
    /// 既定では切ってあります。常に溶けているとモデルの見た目が確かめにくく、
    /// 半透明の VFX とも重なって何が起きているか分かりにくいためです。
    /// </remarks>
    bool m_dissolveEnabled = false;

    /// <summary>習作モードかどうか。</summary>
    /// <remarks>
    /// 有効なあいだは 3D シーンをまったく描かず、習作 1 本だけを描きます。
    /// 影も後処理も通しません。
    /// </remarks>
    bool m_shaderLabEnabled = false;

    /// <summary>
    /// 回転させるモデル。ファイルから読み込みます。
    /// </summary>
    Mesh m_model;

    /// <summary>
    /// モデルを置く床。
    /// </summary>
    Mesh m_floor;

    /// <summary>
    /// モデルが使う材質（基本色とテクスチャ）。
    /// </summary>
    MaterialSet m_modelMaterials;

    /// <summary>
    /// 床が使う材質。
    /// </summary>
    MaterialSet m_floorMaterials;

    /// <summary>
    /// 光源から見た深度。影の判定に使う。
    /// </summary>
    ShadowMap m_shadowMap;

    /// <summary>
    /// 周囲の景色。粗さの段階ごとにぼかしたミップ列を持つ。
    /// </summary>
    Texture2D m_environmentMap;

    /// <summary>
    /// 拡散反射用に、あらゆる方向から届く光を積分したもの。
    /// </summary>
    Texture2D m_irradianceMap;

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
