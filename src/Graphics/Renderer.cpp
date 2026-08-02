//=============================================================================
// Renderer.cpp
//   Renderer の実装。1 フレームの描画手順が全てここに集約されている。
//=============================================================================
#include "Renderer.h"

#include "ShaderCompiler.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "../Assets/EnvironmentPrefilter.h"

#include <format>
#include <stdexcept>

namespace dx12
{
namespace
{
/// <summary>
/// 画面のクリア色（RGBA、各 0.0〜1.0）。**リニア空間の値**です。
/// </summary>
/// <remarks>
/// レンダーターゲットが sRGB 形式なので、ここに渡した値は
/// GPU が sRGB へ変換してから書き込みます。
/// 見た目は sRGB の (0.10, 0.15, 0.30)、8bit で (26, 38, 76) 相当になります。
/// </remarks>
constexpr float kClearColor[4] = { 0.01002f, 0.01960f, 0.07324f, 1.0f };

/// <summary>
/// シェーダー可視ディスクリプタヒープに確保する数。
/// </summary>
constexpr uint32_t kDescriptorHeapCapacity = 32;

/// <summary>
/// シーンに置くオブジェクトの数（モデルと床）。
/// </summary>
constexpr uint32_t kObjectCount = 4;

/// <summary>
/// 定数バッファ上でのオブジェクトの通し番号。
/// </summary>
enum ObjectIndex : uint32_t
{
    kModelObjectIndex   = 0,
    kFloorObjectIndex   = 1,
    kTerrainObjectIndex = 2,
    kPillarObjectIndex  = 3,
};

/// <summary>地形の中心から端までの距離。</summary>
constexpr float kTerrainHalfExtent = 60.0f;

/// <summary>地形の一辺の分割数。頂点は (この値 + 1) の 2 乗になります。</summary>
/// <remarks>
/// 索引が uint16 なので 255 が上限です。細かくすると影のパスが重くなります。
/// </remarks>
constexpr uint32_t kTerrainResolution = 96;

/// <summary>地形の上に置く柱の本数（試みる回数）。</summary>
constexpr uint32_t kPillarCount = 260;

/// <summary>柱を置かない中心部の半径。既存の床を避けます。</summary>
constexpr float kPillarClearRadius = 7.5f;

/// <summary>
/// 読み込むモデル（プロジェクトルートからの相対パス）。
/// </summary>
constexpr const wchar_t* kModelRelativePath = L"assets/models/scene.glb";

/// <summary>
/// 読み込んだモデルを収める大きさ（一番長い辺の長さ）。
/// </summary>
/// <remarks>
/// モデルの単位はファイルによって違うため、読み込み時に揃えます。
/// </remarks>
constexpr float kModelTargetSize = 1.7f;

/// <summary>
/// 読み込んだモデルの底面を置く高さ。
/// </summary>
constexpr float kModelGroundLevel = 0.0f;

/// <summary>
/// モデルが 1 回転するのにかかる秒数。
/// </summary>
constexpr float kSecondsPerRotation = 8.0f;

/// <summary>
/// ディゾルブが 1 往復するのにかける秒数。
/// </summary>
constexpr float kSecondsPerDissolveCycle = 6.0f;

/// <summary>加算合成で描く光の数。</summary>
constexpr uint32_t kOrbCount = 10;

/// <summary>アルファ合成で描く煙の数。</summary>
constexpr uint32_t kSmokeCount = 14;

/// <summary>床すれすれに置く霧の数。ソフトパーティクルの効き目を見るためのもの。</summary>
constexpr uint32_t kFogCount = 8;

/// <summary>
/// ソフトパーティクルで消し始める距離（メートル）。
/// </summary>
/// <remarks>
/// 大きいほど広い範囲がぼやけます。板の大きさと同じくらいが目安で、
/// 大きすぎると板全体が薄くなり、小さすぎると効果が見えません。
/// </remarks>
constexpr float kSoftParticleFadeDistance = 0.55f;


/// <summary>
/// 小数部だけを取り出します（HLSL の `frac` と同じ）。
/// </summary>
/// <param name="value">元の値。</param>
/// <returns>0 以上 1 未満の小数部。</returns>
float frac(float value)
{
    return value - std::floor(value);
}

/// <summary>
/// モデルの読み込みに失敗したときに代わりに使う立方体の、一辺の半分の長さ。
/// </summary>
constexpr float kCubeHalfExtent = 0.6f;

/// <summary>
/// モデルの中心を置く高さ。床から浮かせて影が見やすい位置にします。
/// </summary>
constexpr float kModelCenterHeight = 0.75f;

/// <summary>
/// 床の中心から端までの距離。
/// </summary>
constexpr float kFloorHalfExtent = 2.5f;

/// <summary>
/// 床を置く高さ。
/// </summary>
constexpr float kFloorHeight = 0.0f;

/// <summary>
/// 床でテクスチャを繰り返す回数。サンプラーが WRAP なので模様が並びます。
/// </summary>
constexpr float kFloorUvTiling = 1.0f;

/// <summary>
/// シャドウマップの一辺のピクセル数。
/// </summary>
/// <remarks>
/// 大きいほど影の輪郭が細かくなりますが、メモリと描画時間が増えます。
/// この値を変えたら `shaders/Mesh.hlsl` の `kShadowMapSize` も合わせてください。
/// </remarks>
constexpr uint32_t kShadowMapSize = 2048;

/// <summary>
/// 影を落とす最大距離。カメラからこれより遠い所には影が出ません。
/// </summary>
/// <remarks>
/// 1 枚のシャドウマップだった頃は、この値を大きくすると影が粗くなりました。
/// 段に分けたので、遠くまで伸ばしても手前の細かさが落ちません
/// （[34 章](../../docs/tutorial/34_カスケードシャドウマップ.md)）。
/// </remarks>
constexpr float kShadowDistance = 40.0f;

/// <summary>
/// 床に貼る画像（プロジェクトルートからの相対パス）。
/// </summary>
constexpr const wchar_t* kFloorTextureRelativePath = L"assets/textures/uv-grid.png";

/// <summary>
/// 床に貼る法線マップの相対パス。
/// </summary>
/// <remarks>
/// `uv-grid.png` と同じ 8 x 8 のマス目に合わせて作ってあるので、
/// 目地の溝が模様の線とぴったり重なります。
/// </remarks>
constexpr const wchar_t* kFloorNormalMapRelativePath =
    L"assets/textures/floor-normal.png";

/// <summary>
/// 画像を読めなかったときに代わりに作る市松模様の、一辺のピクセル数。
/// </summary>
constexpr uint32_t kFallbackTextureSize = 256;

/// <summary>
/// 環境マップの画像（プロジェクトルートからの相対パス）。
/// </summary>
constexpr const wchar_t* kEnvironmentRelativePath =
    L"assets/textures/environment.png";

/// <summary>
/// 環境マップのいちばん鮮明な段の横幅。
/// </summary>
constexpr uint32_t kEnvironmentBaseWidth = 256;

/// <summary>
/// 環境マップの段数。粗さ 1.0 が最後の段に対応します。
/// </summary>
/// <remarks>
/// この値を変えたら `shaders/Mesh.hlsl` の `kEnvironmentMipCount` も合わせてください。
/// </remarks>
constexpr uint32_t kEnvironmentMipCount = 6;

/// <summary>
/// 代用する市松模様 1 マスのピクセル数。
/// </summary>
constexpr uint32_t kFallbackTextureCellSize = 32;
} // namespace


/// <summary>
/// デストラクタ。破棄の前に GPU の作業完了を待ちます。
/// </summary>
Renderer::~Renderer()
{
    if (m_initialized)
    {
        WaitForGpu();
    }
}


/// <summary>
/// DirectX 12 の初期化を一式行います。
/// </summary>
void Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    // (1) デバイス（DXGI ファクトリ、アダプタ、D3D12 デバイス）
    m_graphicsDevice.Initialize();

    ID3D12Device* device = m_graphicsDevice.Device();

    // (2) コマンドキューとフェンス
    m_commandQueue.Initialize(device);

    // (3) スワップチェーンと RTV
    //     ※ スワップチェーンの生成にはコマンドキューが必要なので、この順序になる
    m_swapChain.Initialize(
        m_graphicsDevice.Factory(),
        device,
        m_commandQueue.Get(),
        hwnd,
        width,
        height);

    // (4) コマンドアロケータとコマンドリスト
    CreateCommandObjects();

    // (5) シェーダー可視ディスクリプタヒープ
    //     テクスチャとシャドウマップの SRV を置く場所。
    //     数が増えても 1 本のヒープを共有するため、少し余裕を持たせておく。
    m_descriptorHeap.Initialize(device, kDescriptorHeapCapacity);

    // (6) 深度バッファと DSV
    //     レンダーターゲットと同じ解像度でなければならない。
    //   ★ ソフトパーティクルが深度をテクスチャとして読むので、
    //     SRV を置くヒープを先に用意しておく必要がある。
    m_depthBuffer.Initialize(device, m_descriptorHeap, width, height);

    // (6-b) シャドウマップ
    //     パイプラインが影用 PSO を作るのに深度形式を要るので、先に作る。
    m_shadowMap.Initialize(device, m_descriptorHeap, kShadowMapSize);

    // (7) メッシュ描画用のパイプライン
    //     PSO は描画先の形式（RTV / DSV）を知っている必要があるため両方渡す。
    //   ★ 書き込み先は画面ではなく中間バッファ（HDR）になった。
    m_meshPipeline.Initialize(device,
                              RenderTexture::kFormat,
                              DepthBuffer::kFormat,
                              SwapChain::kBackBufferCount,
                              kObjectCount,
                              ShadowMap::kDepthStencilViewFormat);

    // (7-b) 背景を描くパイプライン
    m_skyboxPipeline.Initialize(device, SwapChain::kBackBufferCount);

    // (7-c) シーンを描き込む中間バッファと、その後処理
    //   ★ クリア色は中間バッファ側で使う。ここは HDR なので sRGB 変換は無い。
    m_sceneTexture.Initialize(device, m_descriptorHeap, width, height,
                              L"シーン（HDR）", kClearColor);

    m_postProcess.Initialize(device, m_descriptorHeap, width, height,
                             SwapChain::kBackBufferCount);

    // (7-d) 半透明（VFX）
    //   ★ 中間バッファへ描くので、書き込み先の形式は HDR。
    //     加算合成した光が 1 を超えると、そのままブルームが拾う。
    m_vfxPipeline.Initialize(device, RenderTexture::kFormat, DepthBuffer::kFormat,
                             SwapChain::kBackBufferCount);

    // (7-c-2) 自動露出
    //   ★ シーンを描いた HDR の絵を測るので、その中間バッファのあとに作る。
    m_autoExposure.Initialize(device, m_descriptorHeap, width, height,
                              SwapChain::kBackBufferCount);

    // (7-d-1) GPU の計測
    //   ★ キューごとに刻みの速さが違うので、測るキューを渡す。
    m_gpuTimer.Initialize(device, m_commandQueue.Get(), SwapChain::kBackBufferCount);

    // (7-d-2) GPU パーティクル
    //   ★ 更新はコンピュートシェーダー。CPU は数を渡すだけ。
    m_gpuParticles.Initialize(device, m_descriptorHeap, RenderTexture::kFormat,
                              DepthBuffer::kFormat, SwapChain::kBackBufferCount);

    // (7-e) 習作シェーダー
    //   ★ こちらは後処理を通さず画面へ直接描くので、書き込み先の形式が違う。
    m_shaderLab.Initialize(device, SwapChain::kRenderTargetViewFormat,
                           SwapChain::kBackBufferCount);

    // (8) 環境マップ（映り込みと環境光）
    CreateEnvironment();

    // (9) 描くもの（形状データ）
    CreateSceneMeshes();

    // (10) 光源から見た行列は毎フレーム計算する。
    //     カメラの視錐台に合わせて段を切り直すため、ここでは何もしない。

    // (11) ビューポート／シザー矩形
    UpdateViewportAndScissor(width, height);

    m_initialized = true;
    // シェーダーのコンパイルに掛かった時間。起動時間の内訳を見るため。
    uint32_t shaderCount = 0;
    double   shaderMilliseconds = 0.0;
    shader::GetStatistics(shaderCount, shaderMilliseconds);

    Log(std::format(L"シェーダーを {} 本コンパイルしました（合計 {:.0f} ms）。",
                    shaderCount, shaderMilliseconds));

    Log(L"レンダラの初期化が完了しました。");
}


/// <summary>
/// コマンドアロケータ（バックバッファ枚数ぶん）とコマンドリストを生成します。
/// </summary>
void Renderer::CreateCommandObjects()
{
    ID3D12Device* device = m_graphicsDevice.Device();

    // コマンドアロケータ（命令を書き込むメモリの持ち主）を
    // バックバッファの枚数ぶん作る。
    for (uint32_t i = 0; i < SwapChain::kBackBufferCount; ++i)
    {
        DX_CHECK(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_commandAllocators[i])));
    }

    // コマンドリスト（命令を記録するための道具）
    DX_CHECK(device->CreateCommandList(
        0,                                  // NodeMask
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocators[0].Get(),       // 紐づけるアロケータ（後で切り替える）
        nullptr,                            // 初期 PSO（後で Reset 時に指定するので nullptr）
        IID_PPV_ARGS(&m_commandList)));

    DX_CHECK(m_commandList->Close());

    Log(std::format(L"コマンドアロケータ {} 個とコマンドリストを生成しました。",
                    SwapChain::kBackBufferCount));
}


/// <summary>
/// 環境マップと、そこから求めた環境光を用意します。
/// </summary>
void Renderer::CreateEnvironment()
{
    ID3D12Device* device = m_graphicsDevice.Device();

    assets::ImageData source;
    try
    {
        source = assets::LoadImageFile(ResolveAssetPath(kEnvironmentRelativePath));
    }
    catch (const std::exception& e)
    {
        LogError(L"環境マップを読めなかったため、灰色 1 色で代用します。");
        ::OutputDebugStringA(e.what());
        ::OutputDebugStringA("\n");

        source.width  = 4;
        source.height = 2;
        source.pixels.assign(4 * 2 * 4, 160);
    }

    // 粗さの段階ごとにぼかした列を作る。CPU で下ごしらえしておくのが要点。
    m_environmentMap.Initialize(
        device, m_commandQueue, m_descriptorHeap,
        assets::PrefilterEnvironment(source, kEnvironmentBaseWidth, kEnvironmentMipCount));

    // 拡散反射用の積分。結果はなだらかなので小さくてよい。
    const assets::ImageData irradiance = assets::ComputeIrradiance(source);

    m_irradianceMap.Initialize(device, m_commandQueue, m_descriptorHeap,
                               irradiance.width, irradiance.height, irradiance.pixels);
}


/// <summary>
/// 床に貼るテクスチャを用意します。
/// </summary>
assets::ImageData Renderer::CreateFloorTexture()
{
    try
    {
        return assets::LoadImageFile(ResolveAssetPath(kFloorTextureRelativePath));
    }
    catch (const std::exception& e)
    {
        // 画像が無くても動き続けられるよう、コードで市松模様を作る。
        LogError(L"画像の読み込みに失敗したため、市松模様で代用します。");
        ::OutputDebugStringA(e.what());
        ::OutputDebugStringA("\n");

        assets::ImageData fallback;
        fallback.width  = kFallbackTextureSize;
        fallback.height = kFallbackTextureSize;
        fallback.pixels = CreateCheckerboardPixels(
            kFallbackTextureSize, kFallbackTextureSize, kFallbackTextureCellSize);

        return fallback;
    }
}


/// <summary>
/// シーンに置くメッシュを生成します。
/// </summary>
void Renderer::CreateSceneMeshes()
{
    ID3D12Device* device = m_graphicsDevice.Device();

    // (1) 回転させる本体はファイルから読み込む。
    //   読めなかった場合でも動かなくならないよう、コードで作る立方体へ切り替える。
    MeshData modelData;
    try
    {
        assets::ModelLoadOptions options;
        options.targetSize  = kModelTargetSize;
        options.groundLevel = kModelGroundLevel;

        modelData = assets::LoadModel(ResolveAssetPath(kModelRelativePath), options);
    }
    catch (const std::exception& e)
    {
        LogError(L"モデルの読み込みに失敗したため、立方体で代用します。");
        ::OutputDebugStringA(e.what());
        ::OutputDebugStringA("\n");

        modelData = CreateCube(kCubeHalfExtent);
    }

    m_model.Initialize(device, m_commandQueue, modelData, L"モデル");

    // モデルが持っている材質を GPU 上の資源に変える。
    //   材質を持たないファイルでも、読み込み側が既定の 1 つを用意している。
    m_modelMaterials.Initialize(
        device, m_commandQueue, m_descriptorHeap, modelData.materials);

    // (2) 床は形が単純なのでコードで作る。
    MeshData floorData = CreatePlane(kFloorHalfExtent, kFloorHeight, kFloorUvTiling);
    m_floor.Initialize(device, m_commandQueue, floorData, L"床");

    // 床の材質は 1 つだけ。テクスチャは画像ファイルから読む。
    //   法線マップは無くても描けるので、読めなければそのまま続ける。
    try
    {
        floorData.materials[0].normalTexture =
            assets::LoadImageFile(ResolveAssetPath(kFloorNormalMapRelativePath));
    }
    catch (const std::exception&)
    {
        LogError(L"床の法線マップを読めませんでした。凹凸なしで描きます。");
    }

    m_floorMaterials.Initialize(device, m_commandQueue, m_descriptorHeap,
                                floorData.materials, CreateFloorTexture());

    // (3) 広い地形と、その上に散らばる柱
    //   ★ 34 章のカスケードシャドウマップは、範囲の広い場面でなければ
    //     効果が絵に出ない。それを確かめられる場をここで作る。
    MeshData terrainData = CreateTerrain(kTerrainHalfExtent, kTerrainResolution);
    m_terrain.Initialize(device, m_commandQueue, terrainData, L"地形");
    m_terrainMaterials.Initialize(device, m_commandQueue, m_descriptorHeap,
                                  terrainData.materials);

    MeshData pillarData =
        CreatePillarField(kTerrainHalfExtent * 0.85f, kPillarCount, kPillarClearRadius);

    m_pillars.Initialize(device, m_commandQueue, pillarData, L"柱");
    m_pillarMaterials.Initialize(device, m_commandQueue, m_descriptorHeap,
                                 pillarData.materials);
}


/// <summary>
/// 1 つのメッシュを、材質ごとに区切って描きます。
/// </summary>
void Renderer::RecordMeshWithMaterials(ID3D12GraphicsCommandList* commandList,
                                       const Mesh& mesh,
                                       const MaterialSet& materials)
{
    for (const SubMesh& subMesh : mesh.SubMeshes())
    {
        // 材質の番号が範囲外なら 0 番で代用する。壊れたファイルでも落ちないように。
        const uint32_t materialIndex =
            (subMesh.materialIndex < materials.Count()) ? subMesh.materialIndex : 0;

        m_meshPipeline.BindMaterial(commandList,
                                    materials.ConstantAddress(materialIndex));

        mesh.RecordDrawCommands(commandList, subMesh.indexOffset, subMesh.indexCount);
    }
}


/// <summary>
/// このフレームぶんの定数バッファを更新します。
/// </summary>
void Renderer::UpdateConstants(uint32_t frameIndex)
{
    using namespace DirectX;

    // 縦横比は毎フレーム渡す。ウィンドウをリサイズしても歪まないようにするため。
    m_camera.SetAspectRatio(
        static_cast<float>(m_swapChain.Width()) / static_cast<float>(m_swapChain.Height()));

    const XMMATRIX viewProjection = m_camera.ViewProjectionMatrix();

    // カメラとライトは全オブジェクト共通なので 1 回だけ書く。
    // 光源から見た行列も渡す。影を描くときと、影の中かを調べるときの両方で使う。
    // カメラの前方向。段を選ぶのに使う。
    const XMVECTOR eyeVector    = XMLoadFloat3(&m_camera.Position());
    const XMVECTOR targetVector = XMLoadFloat3(&m_camera.Target());

    XMFLOAT3 cameraForward;
    XMStoreFloat3(&cameraForward,
                  XMVector3Normalize(XMVectorSubtract(targetVector, eyeVector)));

    // ★ 毎フレーム、カメラの視錐台に合わせて段を切り直す。
    //   カメラが動けば影の範囲も動く。
    m_shadowMap.SetLight(MeshPipeline::LightDirection(), m_camera, kShadowDistance);

    m_meshPipeline.UpdateFrameConstants(
        frameIndex, viewProjection, m_camera.Position(), m_shadowMap,
        cameraForward, m_showCascades);

    // 背景は「無限に遠い」ものとして描くので、視点の位置は渡さない。
    //   環境光の強さは物体側と同じ値を使う（食い違うと空だけ浮いて見える）。
    m_skyboxPipeline.Update(frameIndex, m_camera, MeshPipeline::AmbientIntensity());

    // モデル : 2 軸で回しながら、床から浮かせた位置に置く。
    const float angle =
        static_cast<float>(m_animationTime) * (XM_2PI / kSecondsPerRotation);

    const XMMATRIX modelWorld = XMMatrixRotationY(angle)
                              * XMMatrixRotationX(angle * 0.45f)
                              * XMMatrixTranslation(0.0f, kModelCenterHeight, 0.0f);

    // ディゾルブ : 消える → 戻る、を繰り返す。
    //   0〜1 を往復させたいので、三角波にする。
    //   端でしばらく止めるため、smoothstep で緩急を付ける。
    const float cycle = frac(static_cast<float>(m_animationTime)
                             / kSecondsPerDissolveCycle);
    const float pingPong = 1.0f - std::abs(cycle * 2.0f - 1.0f);
    const float dissolve = pingPong * pingPong * (3.0f - 2.0f * pingPong);

    m_meshPipeline.UpdateObjectConstants(
        frameIndex, kModelObjectIndex, modelWorld, viewProjection,
        m_dissolveEnabled ? dissolve : 0.0f);

    // 床 : 動かさないのでワールド行列は単位行列。
    m_meshPipeline.UpdateObjectConstants(
        frameIndex, kFloorObjectIndex, XMMatrixIdentity(), viewProjection);

    // 地形と柱も動かさない。
    m_meshPipeline.UpdateObjectConstants(
        frameIndex, kTerrainObjectIndex, XMMatrixIdentity(), viewProjection);

    m_meshPipeline.UpdateObjectConstants(
        frameIndex, kPillarObjectIndex, XMMatrixIdentity(), viewProjection);
}


/// <summary>
/// 半透明の板を組み立て、描く命令を記録します。
/// </summary>
void Renderer::RecordVfxDrawCommands(uint32_t frameIndex)
{
    using namespace DirectX;

    const float time = static_cast<float>(m_animationTime);

    // --- カメラの右と上。板をカメラへ向けるのに使う ------------------------
    const XMVECTOR eye     = XMLoadFloat3(&m_camera.Position());
    const XMVECTOR target  = XMLoadFloat3(&m_camera.Target());
    const XMVECTOR worldUp = XMLoadFloat3(&m_camera.Up());

    const XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, eye));
    const XMVECTOR right   = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    const XMVECTOR up      = XMVector3Cross(forward, right);

    XMFLOAT3 cameraRight;
    XMFLOAT3 cameraUp;
    XMStoreFloat3(&cameraRight, right);
    XMStoreFloat3(&cameraUp, up);

    // --- (0) GPU パーティクルの定数 ----------------------------------------
    //   位置は GPU が持っているので、CPU が渡すのは「湧き出し口」だけ。
    const XMMATRIX viewProjectionMatrix = m_camera.ViewProjectionMatrix();
    const XMMATRIX projectionMatrix     = m_camera.ProjectionMatrix();

    const float fadeDistance = m_softParticlesEnabled ? kSoftParticleFadeDistance
                                                      : 0.0f;

    m_gpuParticles.Update(frameIndex,
                          static_cast<float>(m_timePaused ? 0.0
                                                          : m_frameTimer.DeltaSeconds()),
                          time,
                          XMFLOAT3(0.0f, 0.05f, 0.0f),
                          viewProjectionMatrix, cameraRight, cameraUp,
                          projectionMatrix, fadeDistance,
                          m_gpuParticleCount);

    m_additiveParticles.clear();
    m_alphaParticles.clear();

    // --- (1) 加算合成の板 : モデルのまわりを回る光 -------------------------
    if (m_vfxEnabled)
    {
        for (uint32_t i = 0; i < kOrbCount; ++i)
        {
            const float phase = static_cast<float>(i) / kOrbCount * XM_2PI;
            const float angle = time * 0.55f + phase;
            const float radius = 1.15f + 0.22f * std::sin(time * 0.9f + phase * 2.0f);

            VfxParticle particle = {};
            particle.positionSize = {
                std::cos(angle) * radius,
                kModelCenterHeight + 0.55f * std::sin(time * 1.3f + phase),
                std::sin(angle) * radius,
                0.17f + 0.05f * std::sin(time * 2.1f + phase)
            };

            // ★ 1 を超える明るさにする。加算合成なので、そのまま足されて
            //   ブルームがにじませる。
            const float hue = static_cast<float>(i) / kOrbCount;
            particle.color = { 1.5f + 1.1f * hue, 0.65f + 0.25f * hue,
                               0.22f + 1.2f * hue, 1.0f };
            particle.params = { 1.0f, angle * 0.7f, 0.0f, 0.0f };

            m_additiveParticles.push_back(particle);
        }

        // --- (2) アルファ合成の板 : ゆっくり漂う煙 -----------------------------
        for (uint32_t i = 0; i < kSmokeCount; ++i)
        {
            const float fi = static_cast<float>(i);
            const float drift = time * 0.16f + fi * 0.83f;

            VfxParticle particle = {};
            particle.positionSize = {
                std::cos(drift * 0.7f + fi) * (0.9f + 0.5f * std::sin(fi * 2.3f)),
                0.30f + std::fmod(drift, 2.2f),
                std::sin(drift * 0.5f + fi * 1.7f) * (0.9f + 0.5f * std::cos(fi * 1.9f)),
                0.42f + 0.22f * std::sin(fi * 3.1f)
            };

            // 上へ行くほど薄くなる
            const float height = (particle.positionSize.y - 0.30f) / 2.2f;
            particle.color = { 0.55f, 0.58f, 0.66f, 0.30f * (1.0f - height) };
            particle.params = { 1.0f, fi * 1.31f + time * 0.15f, 0.0f, 0.0f };

            m_alphaParticles.push_back(particle);
        }

        // --- (2-b) 地表の霧 : 床を突き抜ける板 ---------------------------------
        //   ★ ソフトパーティクルが効いているかは、これで見ます。
        //     板は必ずカメラを向くので、床すれすれに置くと必ず床を貫きます。
        //     深度で薄めないと、そこに直線の切り口が出ます。
        for (uint32_t i = 0; i < kFogCount; ++i)
        {
            const float fi    = static_cast<float>(i);
            const float angle = fi / kFogCount * XM_2PI + time * 0.05f;

            const float radius = 1.55f + 0.35f * std::sin(fi * 1.7f);

            VfxParticle particle = {};
            particle.positionSize = {
                std::cos(angle) * radius,
                0.16f + 0.05f * std::sin(time * 0.4f + fi),
                std::sin(angle) * radius,
                0.80f
            };

            particle.color  = { 0.72f, 0.76f, 0.86f, 0.42f };
            particle.params = { 0.85f, fi * 0.77f, 0.0f, 0.0f };

            m_alphaParticles.push_back(particle);
        }
    }

    // --- (3) アルファ合成は「奥から手前へ」並べ替える ----------------------
    //   ★ アルファ合成は掛け算の順序が結果を変えるので、順番が意味を持つ。
    //     加算合成は足し算なので、順番を変えても結果は同じ。
    if (m_vfxSortEnabled)
    {
        XMFLOAT3 eyePosition = m_camera.Position();

        std::sort(m_alphaParticles.begin(), m_alphaParticles.end(),
                  [&eyePosition](const VfxParticle& a, const VfxParticle& b)
                  {
                      const float da = (a.positionSize.x - eyePosition.x) *
                                       (a.positionSize.x - eyePosition.x)
                                     + (a.positionSize.y - eyePosition.y) *
                                       (a.positionSize.y - eyePosition.y)
                                     + (a.positionSize.z - eyePosition.z) *
                                       (a.positionSize.z - eyePosition.z);

                      const float db = (b.positionSize.x - eyePosition.x) *
                                       (b.positionSize.x - eyePosition.x)
                                     + (b.positionSize.y - eyePosition.y) *
                                       (b.positionSize.y - eyePosition.y)
                                     + (b.positionSize.z - eyePosition.z) *
                                       (b.positionSize.z - eyePosition.z);

                      return da > db;   // 遠い順
                  });
    }

    m_vfxPipeline.Update(frameIndex, 0, viewProjectionMatrix, cameraRight, cameraUp,
                         projectionMatrix, fadeDistance, m_alphaParticles);
    m_vfxPipeline.Update(frameIndex, 1, viewProjectionMatrix, cameraRight, cameraUp,
                         projectionMatrix, fadeDistance, m_additiveParticles);

    // --- 深度バッファを「読める状態」へ移す --------------------------------
    //   ★ 書き込み可のままシェーダーから読むことはできない。
    //     DEPTH_READ と PIXEL_SHADER_RESOURCE を同時に立てて、
    //     描画先には書き込みを禁じた DSV を設定する。
    RecordResourceBarrier(
        m_commandList.Get(),
        m_depthBuffer.Resource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_DEPTH_READ |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView =
        m_sceneTexture.RenderTargetView();
    const D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv =
        m_depthBuffer.ReadOnlyDepthStencilView();

    m_commandList->OMSetRenderTargets(1, &renderTargetView, FALSE, &readOnlyDsv);

    // ★ 煙（アルファ）が先、光（加算）があと。
    //   加算は順番を選ばないので、最後に描くのがいちばん素直。
    m_vfxPipeline.Record(m_commandList.Get(), frameIndex, 0, BlendMode::Alpha,
                         m_depthBuffer.ShaderResourceView(),
                         static_cast<uint32_t>(m_alphaParticles.size()));

    m_vfxPipeline.Record(m_commandList.Get(), frameIndex, 1, BlendMode::Additive,
                         m_depthBuffer.ShaderResourceView(),
                         static_cast<uint32_t>(m_additiveParticles.size()));

    // --- GPU パーティクル ---------------------------------------------------
    //   更新はこのフレームの先頭で済んでいる。ここでは描くだけ。
    if (m_gpuParticlesEnabled)
    {
        m_gpuParticles.RecordDraw(m_commandList.Get(), frameIndex,
                                  m_depthBuffer.ShaderResourceView(),
                                  m_gpuParticleCount);
    }

    // 次のフレームのために書き込み可へ戻す。
    RecordResourceBarrier(
        m_commandList.Get(),
        m_depthBuffer.Resource(),
        D3D12_RESOURCE_STATE_DEPTH_READ |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
}


/// <summary>
/// 半透明（VFX）の入り切りを切り替えます。
/// </summary>
void Renderer::ToggleVfx()
{
    m_vfxEnabled = !m_vfxEnabled;
    Log(m_vfxEnabled ? L"半透明（VFX）を有効にしました。"
                     : L"半透明（VFX）を無効にしました。");
}


/// <summary>
/// 半透明の並べ替えの入り切りを切り替えます。
/// </summary>
void Renderer::ToggleVfxSort()
{
    m_vfxSortEnabled = !m_vfxSortEnabled;
    Log(m_vfxSortEnabled ? L"半透明を奥から並べ替えます。"
                         : L"半透明の並べ替えをやめました（対照実験）。");
}


/// <summary>
/// ソフトパーティクルの入り切りを切り替えます。
/// </summary>
void Renderer::ToggleSoftParticles()
{
    m_softParticlesEnabled = !m_softParticlesEnabled;
    Log(m_softParticlesEnabled
            ? L"ソフトパーティクルを有効にしました。"
            : L"ソフトパーティクルを無効にしました（対照実験）。");
}


/// <summary>
/// GPU パーティクルの入り切りを切り替えます。
/// </summary>
void Renderer::ToggleGpuParticles()
{
    m_gpuParticlesEnabled = !m_gpuParticlesEnabled;
    Log(m_gpuParticlesEnabled ? L"GPU パーティクルを有効にしました。"
                              : L"GPU パーティクルを無効にしました。");
}


/// <summary>
/// GPU パーティクルの数を切り替えます。
/// </summary>
void Renderer::CycleGpuParticleCount()
{
    m_gpuParticleCount *= 4;

    if (m_gpuParticleCount > GpuParticleSystem::kMaxParticles)
    {
        m_gpuParticleCount = 1024;
    }

    Log(std::format(L"GPU パーティクルを {} 個にしました。", m_gpuParticleCount));
}


/// <summary>
/// 垂直同期の入り切りを切り替えます。
/// </summary>
void Renderer::ToggleVSync()
{
    m_vsyncEnabled = !m_vsyncEnabled;
    Log(m_vsyncEnabled ? L"垂直同期を有効にしました。"
                       : L"垂直同期を無効にしました（速度計測用）。");
}


/// <summary>
/// 自動露出の入り切りを切り替えます。
/// </summary>
void Renderer::ToggleAutoExposure()
{
    m_autoExposureEnabled = !m_autoExposureEnabled;
    m_postProcess.SetAutoExposureEnabled(m_autoExposureEnabled);

    Log(m_autoExposureEnabled ? L"自動露出を有効にしました。"
                              : L"自動露出を無効にしました（固定露出）。");
}


/// <summary>
/// 明るさのまとめ方を切り替えます。
/// </summary>
void Renderer::ToggleReductionMode()
{
    m_reductionMode = (m_reductionMode == ReductionMode::Wave)
                          ? ReductionMode::GroupShared
                          : ReductionMode::Wave;

    Log(m_reductionMode == ReductionMode::Wave
            ? L"明るさの集計を Wave 命令にしました。"
            : L"明るさの集計を共有メモリにしました（対照実験）。");
}


/// <summary>
/// 明るさを測る解像度を切り替えます。
/// </summary>
void Renderer::CycleExposureSampleRate()
{
    const uint32_t divisor = m_autoExposure.CycleSampleDivisor();

    Log(std::format(L"明るさを 1/{} の解像度で測ります（{} x {} = {} 点）。",
                    divisor,
                    m_autoExposure.SampleWidth(),
                    m_autoExposure.SampleHeight(),
                    m_autoExposure.SampleWidth() * m_autoExposure.SampleHeight()));
}


/// <summary>
/// シーンを何回ぶん記録するかを切り替えます。
/// </summary>
void Renderer::CycleMeshRepeatCount()
{
    m_meshRepeatCount = (m_meshRepeatCount >= 128) ? 1 : (m_meshRepeatCount * 16);

    Log(std::format(L"シーンを {} 回ぶん記録します（計測用）。", m_meshRepeatCount));
}


/// <summary>
/// 段を色で塗る表示の入り切りを切り替えます。
/// </summary>
void Renderer::ToggleCascadeView()
{
    m_showCascades = !m_showCascades;

    Log(m_showCascades ? L"影の段を色で塗ります（赤 = 手前、緑 = 中間、青 = 奥）。"
                       : L"通常の表示に戻しました。");
}


/// <summary>
/// 動きを止める・再開するを切り替えます。
/// </summary>
void Renderer::ToggleTimePause()
{
    m_timePaused = !m_timePaused;
    Log(m_timePaused ? std::format(L"動きを止めました（t = {:.2f} 秒）。", m_animationTime)
                     : L"動きを再開しました。");
}


/// <summary>
/// シーンの全メッシュを描く命令をコマンドリストに記録します。
/// </summary>
void Renderer::RecordMeshDrawCommands(uint32_t frameIndex)
{
    ID3D12GraphicsCommandList* commandList = m_commandList.Get();

    // ★ 記録に掛かる CPU 時間を測る。GPU の時間ではなく、
    //   「命令を並べるのにどれだけ掛かったか」を見る。
    //   ビンドレスで減るのはここ。
    const auto startTime = std::chrono::steady_clock::now();

    // 同じものを何回ぶん記録するか。1 なら通常。増やしても絵は変わらない。
    for (uint32_t repeat = 0; repeat < m_meshRepeatCount; ++repeat)
    {
        // オブジェクトごとに定数を差し替え、その中で材質ごとにさらに区切って描く。
        m_meshPipeline.BindObject(commandList, frameIndex, kFloorObjectIndex);
        RecordMeshWithMaterials(commandList, m_floor, m_floorMaterials);

        m_meshPipeline.BindObject(commandList, frameIndex, kModelObjectIndex);
        RecordMeshWithMaterials(commandList, m_model, m_modelMaterials);

        m_meshPipeline.BindObject(commandList, frameIndex, kTerrainObjectIndex);
        RecordMeshWithMaterials(commandList, m_terrain, m_terrainMaterials);

        m_meshPipeline.BindObject(commandList, frameIndex, kPillarObjectIndex);
        RecordMeshWithMaterials(commandList, m_pillars, m_pillarMaterials);
    }

    const auto endTime = std::chrono::steady_clock::now();

    m_meshRecordMicroseconds +=
        std::chrono::duration<double, std::micro>(endTime - startTime).count();
    ++m_meshRecordSamples;
}


/// <summary>
/// 光源から見た深度をシャドウマップへ描きます。
/// </summary>
void Renderer::RecordShadowPass(uint32_t frameIndex)
{
    ID3D12GraphicsCommandList* commandList = m_commandList.Get();

    // バリアは段ごとではなく、パス全体で 1 回。
    m_shadowMap.BeginShadowPass(commandList);

    // 影の形しか要らないので、専用の（ピクセルシェーダーの無い）設定を使う。
    m_meshPipeline.BindShadowPass(commandList, frameIndex);

    // ★ 段の数だけシーンを描き直す。ここが「3 倍描く」の実体。
    for (uint32_t cascade = 0; cascade < ShadowMap::kCascadeCount; ++cascade)
    {
        m_shadowMap.BeginRender(commandList, cascade);
        m_meshPipeline.SetCascadeIndex(commandList, cascade);

        // ★ 画面を描くときと同じメッシュを、同じワールド行列で描く。
        //   ここがずれると、物体と影の位置が合わなくなる。
        //   影の形しか要らないので、材質で区切らずメッシュ全体を一度に描く。
        m_meshPipeline.BindObject(commandList, frameIndex, kFloorObjectIndex);
        m_floor.RecordDrawCommands(commandList);

        m_meshPipeline.BindObject(commandList, frameIndex, kModelObjectIndex);
        m_model.RecordDrawCommands(commandList);

        m_meshPipeline.BindObject(commandList, frameIndex, kTerrainObjectIndex);
        m_terrain.RecordDrawCommands(commandList);

        m_meshPipeline.BindObject(commandList, frameIndex, kPillarObjectIndex);
        m_pillars.RecordDrawCommands(commandList);
    }

    // テクスチャとして読める状態に戻す。
    m_shadowMap.EndRender(commandList);
}


/// <summary>
/// ビューポートとシザー矩形をウィンドウサイズに合わせて更新します。
/// </summary>
void Renderer::UpdateViewportAndScissor(uint32_t width, uint32_t height)
{
    // ビューポート : バックバッファ全体を使う
    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;
    m_viewport.Width    = static_cast<float>(width);
    m_viewport.Height   = static_cast<float>(height);

    // 深度値の出力範囲。既定の 0.0〜1.0 をそのまま使う
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;

    // シザー矩形 : 切り抜かない（＝バックバッファ全体を許可する）
    m_scissorRect.left   = 0;
    m_scissorRect.top    = 0;
    m_scissorRect.right  = static_cast<LONG>(width);
    m_scissorRect.bottom = static_cast<LONG>(height);
}


/// <summary>
/// リソースの状態遷移バリアをコマンドリストに記録します。
/// </summary>
void Renderer::RecordResourceBarrier(ID3D12GraphicsCommandList* commandList,
                                     ID3D12Resource* resource,
                                     D3D12_RESOURCE_STATES stateBefore,
                                     D3D12_RESOURCE_STATES stateAfter)
{
    D3D12_RESOURCE_BARRIER barrier = {};

    // バリアには 3 種類あります。
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

    barrier.Transition.pResource = resource;

    // Subresource : テクスチャのミップレベルや配列要素を個別に指定できる。
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    // ★ StateBefore には「現在の実際の状態」を正確に書かなければなりません。
    barrier.Transition.StateBefore = stateBefore;
    barrier.Transition.StateAfter  = stateAfter;

    // 複数のバリアをまとめて発行できるため、引数は配列
    commandList->ResourceBarrier(1, &barrier);
}


/// <summary>
/// 1 フレーム描画して画面に表示します。
/// </summary>
void Renderer::Render()
{
    // フレーム時間を計測する（1 秒ごとに FPS がログに出ます）
    m_frameTimer.Tick();

    // ★ 動きに使う時計は、フレーム時間とは別に持つ。
    //   止められるようにしておくと、対照実験で「まったく同じ瞬間」を
    //   2 通りの設定で撮り比べられる。
    if (!m_timePaused)
    {
        m_animationTime += m_frameTimer.DeltaSeconds();
    }

    // (0) このフレームで使う「ノート（アロケータ）」が空くのを待つ
    //   バックバッファが 2 枚なので、フレーム番号は 0, 1, 0, 1 … と循環します。
    const uint32_t frameIndex = m_swapChain.CurrentBackBufferIndex();

    m_commandQueue.WaitForFenceValue(m_frameFenceValues[frameIndex]);

    // (1) コマンドアロケータのリセット
    //   (0) で完了を確認したので、安全に中身を捨てて再利用できます。
    DX_CHECK(m_commandAllocators[frameIndex]->Reset());

    // (2) コマンドリストのリセット（記録の開始）
    DX_CHECK(m_commandList->Reset(m_commandAllocators[frameIndex].Get(), nullptr));

    // (2-a) 前回このフレーム番号で測った GPU 時間を回収する
    //   ★ (0) でフェンスを待った直後なので、その submit は完了している。
    //     待つ前に読むと、まだ GPU が書いている値を読んでしまう。
    m_gpuTimer.Collect(frameIndex);

    m_gpuTimer.Begin(m_commandList.Get(), frameIndex, GpuPass::Frame);

    // (2-b) このフレームの定数（変換行列とライト）を更新する
    //   (0) でこのフレームのフェンスを待っているため、GPU はもうこの領域を読んでいない。
    UpdateConstants(frameIndex);

    // (2-c) ディスクリプタヒープの設定
    //   ★ SetGraphicsRootDescriptorTable より前に呼ぶ必要があります。
    //     ルートシグネチャを切り替えても、この設定は残ります。
    ID3D12DescriptorHeap* const descriptorHeaps[] = { m_descriptorHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // (2-c-2) GPU パーティクルを 1 フレームぶん進める
    //   ★ 描画より前に済ませる。コンピュートはレンダーターゲットを使わないので、
    //     描画先を決める前のここが置き場所として素直。
    if (m_gpuParticlesEnabled && !m_shaderLabEnabled)
    {
        m_gpuTimer.Begin(m_commandList.Get(), frameIndex, GpuPass::ParticleUpdate);

        m_gpuParticles.RecordUpdate(m_commandList.Get(), frameIndex,
                                    m_gpuParticleCount);

        m_gpuTimer.End(m_commandList.Get(), frameIndex, GpuPass::ParticleUpdate);
    }

    // (2-d) 第 1 パス : 光源から見た深度をシャドウマップへ描く
    //   ★ 画面を描く前に済ませておく必要があります。
    //     第 2 パスは、この結果をテクスチャとして読むためです。
    m_gpuTimer.Begin(m_commandList.Get(), frameIndex, GpuPass::Shadow);
    RecordShadowPass(frameIndex);
    m_gpuTimer.End(m_commandList.Get(), frameIndex, GpuPass::Shadow);

    ID3D12Resource* backBuffer = m_swapChain.CurrentBackBuffer();

    // ここから第 2 パス : シーンを中間バッファへ描く
    // (3) バリア : PRESENT → RENDER_TARGET
    //   スワップチェーンから取得したバックバッファは、
    //   Present 直後は「表示用 (PRESENT)」の状態になっています。
    RecordResourceBarrier(
        m_commandList.Get(),
        backBuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    // (4) ビューポートとシザー矩形の設定
    //   ★ 影のパスでシャドウマップの大きさに変えてあるので、必ず戻します。
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // (4-b) 習作モードなら、3D シーンを描かずに習作 1 本だけを描く
    if (m_shaderLabEnabled)
    {
        m_shaderLab.Update(frameIndex,
                           static_cast<float>(m_animationTime),
                           static_cast<float>(m_frameTimer.DeltaSeconds()),
                           m_swapChain.Width(), m_swapChain.Height(),
                           m_shaderLabMouseX, m_shaderLabMouseY,
                           m_shaderLabMouseDown);

        const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            m_swapChain.CurrentRenderTargetView();

        // 深度は使わない。画面を絵で塗り潰すだけ。
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        m_shaderLab.Record(m_commandList.Get(), frameIndex);

        RecordResourceBarrier(
            m_commandList.Get(),
            backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);

        DX_CHECK(m_commandList->Close());
        m_commandQueue.ExecuteCommandList(m_commandList.Get());
        m_swapChain.Present();
        m_frameFenceValues[frameIndex] = m_commandQueue.Signal();
        return;
    }

    // (5) 描画先を「中間バッファ」に切り替える
    //   ★ 画面へ直接描かない。1.0 を超える明るさを残したまま
    //     後処理へ渡すためです（[25 章](../../docs/tutorial/25_ポストプロセス.md)）。
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_depthBuffer.DepthStencilView();

    // バリア・描画先の設定・クリアまでを RenderTexture が行う。
    m_sceneTexture.BeginRender(m_commandList.Get(), &dsvHandle);

    // (6) 深度バッファのクリア
    //   ★ 色のクリアと同じくらい重要です。忘れると前フレームの深度が残り、
    //     2 フレーム目以降で「何も描かれない」「ちらつく」といった症状になります。
    m_commandList->ClearDepthStencilView(
        dsvHandle,
        D3D12_CLEAR_FLAG_DEPTH,     // 深度のみクリア（ステンシルは対象外）
        DepthBuffer::kClearDepth,   // 一番奥の値。作成時の最適化クリア値と一致必須
        0,                          // ステンシルのクリア値（未使用）
        0, nullptr);                // 部分クリアの矩形（0 / nullptr で全体）

    // (7) シーン（床と立方体）の描画命令を記録
    //   影のパスでルートシグネチャを切り替えたので、ここで改めて設定し直す。
    m_meshPipeline.Bind(m_commandList.Get(), frameIndex,
                        m_shadowMap.ShaderResourceView(),
                        m_environmentMap.ShaderResourceView(),
                        m_irradianceMap.ShaderResourceView());

    m_gpuTimer.Begin(m_commandList.Get(), frameIndex, GpuPass::Scene);
    RecordMeshDrawCommands(frameIndex);
    m_gpuTimer.End(m_commandList.Get(), frameIndex, GpuPass::Scene);

    // (7-b) 背景の描画
    //   ★ 物体のあとに描く。背景は最も奥なので、先に物体を描いておけば
    //     隠れるピクセルの計算が深度テストで省かれる（早期 Z）。
    m_gpuTimer.Begin(m_commandList.Get(), frameIndex, GpuPass::Skybox);

    m_skyboxPipeline.Record(m_commandList.Get(), frameIndex,
                            m_environmentMap.ShaderResourceView());

    m_gpuTimer.End(m_commandList.Get(), frameIndex, GpuPass::Skybox);

    // (7-b-2) 半透明の描画
    //   ★ 不透明な物と背景をすべて描いたあとに描く。
    //     半透明は深度を書かないので、先に描くと後ろの物に上書きされる。
    if (m_vfxEnabled || m_gpuParticlesEnabled)
    {
        m_gpuTimer.Begin(m_commandList.Get(), frameIndex, GpuPass::Transparent);
        RecordVfxDrawCommands(frameIndex);
        m_gpuTimer.End(m_commandList.Get(), frameIndex, GpuPass::Transparent);
    }

    // (7-c) 中間バッファを読める状態へ戻す
    m_sceneTexture.EndRender(m_commandList.Get());

    // (7-d) 後処理して画面へ
    //   露出 → ブルーム合成 → トーンマッピング → ビネット、をまとめて行う。
    // (7-c-2) 明るさを測って露出を決める
    //   ★ シーンを読める状態にしたあと、後処理より前。
    //     後処理はここで決めた露出をそのまま使う。CPU は経由しない。
    m_autoExposure.Update(frameIndex,
                          static_cast<float>(m_frameTimer.DeltaSeconds()));

    m_gpuTimer.Begin(m_commandList.Get(), frameIndex, GpuPass::AutoExposure);

    m_autoExposure.Record(m_commandList.Get(), frameIndex, m_sceneTexture,
                          m_reductionMode);

    m_gpuTimer.End(m_commandList.Get(), frameIndex, GpuPass::AutoExposure);

    m_gpuTimer.Begin(m_commandList.Get(), frameIndex, GpuPass::PostProcess);

    m_postProcess.Record(m_commandList.Get(), frameIndex,
                         m_sceneTexture,
                         m_autoExposure.ShaderResourceView(),
                         m_swapChain.CurrentRenderTargetView(),
                         m_viewport, m_scissorRect);

    m_gpuTimer.End(m_commandList.Get(), frameIndex, GpuPass::PostProcess);

    // (8) バリア : RENDER_TARGET → PRESENT
    //   描き終わったので、表示できる状態へ戻します。
    RecordResourceBarrier(
        m_commandList.Get(),
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);

    m_gpuTimer.End(m_commandList.Get(), frameIndex, GpuPass::Frame);

    // (8-b) 測った値を読み出し用バッファへ移す
    //   ★ Close の直前に置く。この命令を忘れると、クエリは打っているのに
    //     CPU からは何も読めない。
    m_gpuTimer.Resolve(m_commandList.Get(), frameIndex);

    // (9) 記録の終了
    //   Close を呼ぶまでコマンドリストは GPU に投入できません。
    DX_CHECK(m_commandList->Close());

    // (9-b) 計測結果をログへ。FPS と同じ間隔でしか出さない。
    if (m_frameTimer.ReportedThisFrame())
    {
        Log(m_gpuTimer.Format());

        if (m_meshRecordSamples > 0)
        {
            Log(std::format(L"CPU: メッシュ記録 {:.1f} us / フレーム（{} 回ぶん）",
                            m_meshRecordMicroseconds / m_meshRecordSamples,
                            m_meshRepeatCount));
        }

        m_meshRecordMicroseconds = 0.0;
        m_meshRecordSamples      = 0;
    }

    // (10) GPU へ投入 — ここで初めて GPU が動き始める
    m_commandQueue.ExecuteCommandList(m_commandList.Get());

    // (11) 画面に表示
    //   ★ この呼び出しで SwapChain の「現在のバックバッファ番号」が変わります。
    m_swapChain.Present(m_vsyncEnabled);

    // (12) このフレームの「完了印」を予約する
    //   Signal() はコマンドキューの末尾に積まれるだけで、
    //   この行を実行した時点ではまだ GPU は動き終えていません。
    m_frameFenceValues[frameIndex] = m_commandQueue.Signal();
}


/// <summary>
/// ウィンドウサイズ変更に追従します。
/// </summary>
void Renderer::Resize(uint32_t width, uint32_t height)
{
    if (!m_initialized)
    {
        return;
    }

    // ★ バックバッファを差し替える前に、GPU が使い終わるのを必ず待つ
    WaitForGpu();

    m_swapChain.Resize(m_graphicsDevice.Device(), width, height);

    // 深度バッファもレンダーターゲットと同じ解像度に作り直す。
    m_depthBuffer.Resize(m_graphicsDevice.Device(), width, height);
    m_autoExposure.Resize(width, height);

    UpdateViewportAndScissor(width, height);
}


/// <summary>
/// GPU の全作業完了を待ち、全フレームのフェンス値を揃えます。
/// </summary>
void Renderer::WaitForGpu()
{
    // GPU の全作業が終わるまで待つ
    const uint64_t fenceValue = m_commandQueue.Flush();

    // ★ 全フレームのフェンス値をこの値で埋める
    //   これを忘れると、古い（既に完了済みの）値が配列に残り続けます。
    m_frameFenceValues.fill(fenceValue);
}


/// <summary>
/// 習作モードの入り切りを切り替えます。
/// </summary>
void Renderer::ToggleShaderLab()
{
    m_shaderLabEnabled = !m_shaderLabEnabled;

    if (m_shaderLabEnabled)
    {
        Log(std::format(L"習作モードに入りました（{} / {} : {}）",
                        m_shaderLab.CurrentIndex() + 1, m_shaderLab.Count(),
                        m_shaderLab.CurrentName()));
    }
    else
    {
        Log(L"習作モードを抜けました。");
    }
}


/// <summary>
/// ディゾルブの入り切りを切り替えます。
/// </summary>
void Renderer::ToggleDissolve()
{
    m_dissolveEnabled = !m_dissolveEnabled;
    Log(m_dissolveEnabled ? L"ディゾルブを有効にしました。"
                          : L"ディゾルブを無効にしました。");
}


/// <summary>
/// 表示する習作を前後に動かします。
/// </summary>
void Renderer::AdvanceShaderLab(int delta)
{
    m_shaderLab.Advance(delta);
}


/// <summary>
/// 表示する習作を番号で選びます。
/// </summary>
void Renderer::SelectShaderLab(int index)
{
    m_shaderLab.Select(index);
}

} // namespace dx12
