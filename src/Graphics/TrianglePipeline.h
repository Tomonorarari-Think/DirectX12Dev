//=============================================================================
// TrianglePipeline.h
//   三角形 1 枚を描くための描画設定一式（ルートシグネチャ・PSO・頂点バッファ）。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"
#include "ConstantBuffer.h"
#include "Texture2D.h"

// DirectXMath : Windows SDK に同梱される数学ライブラリ。
#include <DirectXMath.h>

namespace dx12
{
class CommandQueue;
class DescriptorHeap;

/// <summary>
/// シェーダーへ毎フレーム渡す定数の内容。
/// </summary>
struct SceneConstants
{
    /// <summary>
    /// ワールド × ビュー × プロジェクションをまとめた変換行列。
    /// </summary>
    DirectX::XMFLOAT4X4 worldViewProjection;
};


/// <summary>
/// 頂点 1 個ぶんのデータ構造。
/// </summary>
struct Vertex
{
    /// <summary>
    /// 頂点の座標 (x, y, z)。NDC 座標で、画面中央が原点、範囲は -1〜+1。
    /// </summary>
    float position[3];

    /// <summary>
    /// 頂点の色 (r, g, b, a)。各成分は 0.0〜1.0。
    /// </summary>
    float color[4];

    /// <summary>
    /// テクスチャ座標 (u, v)。左上が (0,0)、右下が (1,1)。
    /// </summary>
    /// <remarks>
    /// V は下向きが正です（画面の Y が上向きなのと逆）。 取り違えるとテクスチャが上下逆さまに貼
    /// られます。
    /// </remarks>
    float uv[2];
};


/// <summary>
/// 三角形 1 枚を描くのに必要な「描画の設定一式」と「頂点データ」を持つクラス。
/// </summary>
class TrianglePipeline
{
public:
    /// <summary>
    /// 既定のコンストラクタ。まだ何も生成されません。
    /// </summary>
    TrianglePipeline() = default;

    /// <summary>
    /// デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。
    /// </summary>
    ~TrianglePipeline() = default;

    /// <summary>
    /// コピー構築は禁止です。
    /// </summary>
    TrianglePipeline(const TrianglePipeline&) = delete;

    /// <summary>
    /// コピー代入は禁止です。
    /// </summary>
    TrianglePipeline& operator=(const TrianglePipeline&) = delete;

    /// <summary>
    /// ルートシグネチャ・PSO・頂点バッファ・定数バッファを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="renderTargetFormat">
    /// 描画先の形式。PSO はこれを知っている必要があります。
    /// </param>
    /// <param name="depthStencilFormat">
    /// 深度バッファの形式。`DepthBuffer::kFormat` と一致させること。
    /// </param>
    /// <param name="frameCount">
    /// 定数バッファに用意するフレーム数（通常はバックバッファの枚数）。
    /// </param>
    /// <param name="commandQueue">
    /// テクスチャ転送に使うキュー。転送の完了まで待機します。
    /// </param>
    /// <param name="descriptorHeap">テクスチャの SRV を登録するシェーダー可視ヒープ。</param>
    /// <exception cref="HrException">いずれかの生成に失敗した場合。</exception>
    /// <exception cref="std::runtime_error">シェーダーファイルが見つからない場合。</exception>
    void Initialize(ID3D12Device* device,
                    DXGI_FORMAT renderTargetFormat,
                    DXGI_FORMAT depthStencilFormat,
                    uint32_t frameCount,
                    CommandQueue& commandQueue,
                    DescriptorHeap& descriptorHeap);

    /// <summary>
    /// このフレームの変換行列を計算し、定数バッファへ書き込みます。
    /// </summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    /// <param name="aspectRatio">画面の縦横比（幅 ÷ 高さ）。</param>
    /// <param name="totalSeconds">起動からの経過秒数。回転角の算出に使います。</param>
    void Update(uint32_t frameIndex, float aspectRatio, float totalSeconds);

    /// <summary>
    /// コマンドリストに「三角形を描く」命令を記録します。
    /// </summary>
    /// <param name="commandList">記録先の（Reset 済みで開いている）コマンドリスト。</param>
    /// <param name="frameIndex">使用する定数バッファのフレーム番号。</param>
    void RecordDrawCommands(ID3D12GraphicsCommandList* commandList, uint32_t frameIndex) const;

private:
    /// <summary>
    /// ルートシグネチャを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <exception cref="HrException">シリアライズまたは生成に失敗した場合。</exception>
    void CreateRootSignature(ID3D12Device* device);

    /// <summary>
    /// HLSL をコンパイルし、パイプラインステートオブジェクトを生成します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="renderTargetFormat">描画先の形式。</param>
    /// <param name="depthStencilFormat">深度バッファの形式。</param>
    /// <exception cref="HrException">コンパイルまたは PSO 生成に失敗した場合。</exception>
    void CreatePipelineState(ID3D12Device* device,
                             DXGI_FORMAT renderTargetFormat,
                             DXGI_FORMAT depthStencilFormat);

    /// <summary>
    /// 頂点バッファを DEFAULT ヒープに作り、頂点データを転送します。
    /// </summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="commandQueue">転送コマンドを実行するキュー。完了まで待機します。</param>
    /// <exception cref="HrException">リソースの生成または転送に失敗した場合。</exception>
    void CreateVertexBuffer(ID3D12Device* device, CommandQueue& commandQueue);

    /// <summary>
    /// HLSL ファイルをコンパイルして、GPU 用のバイトコードを得ます。
    /// </summary>
    /// <param name="filePath">.hlsl ファイルの絶対パス。</param>
    /// <param name="entryPoint">入口となる関数名（`"VSMain"` など）。</param>
    /// <param name="target">シェーダーモデル（`"vs_5_0"` など）。</param>
    /// <returns>コンパイル済みバイトコードを保持する Blob。</returns>
    /// <exception cref="HrException">コンパイルに失敗した場合。</exception>
    static ComPtr<ID3DBlob> CompileShader(const std::wstring& filePath,
                                          const char* entryPoint,
                                          const char* target);

private:
    /// <summary>
    /// シェーダーが受け取る外部入力の一覧表（今回は空）。
    /// </summary>
    ComPtr<ID3D12RootSignature> m_rootSignature;

    /// <summary>
    /// 描画設定を 1 つに固めたパイプラインステートオブジェクト。
    /// </summary>
    ComPtr<ID3D12PipelineState> m_pipelineState;

    /// <summary>
    /// 頂点データを置く GPU 上のメモリ領域（DEFAULT ヒープ）。
    /// </summary>
    ComPtr<ID3D12Resource> m_vertexBuffer;

    /// <summary>
    /// 変換行列をシェーダーへ渡すための定数バッファ（フレーム数ぶん）。
    /// </summary>
    ConstantBuffer m_constantBuffer;

    /// <summary>
    /// 三角形に貼るテクスチャ（市松模様）。
    /// </summary>
    Texture2D m_texture;

    /// <summary>
    /// 頂点バッファの読み取り方を GPU に伝える構造体。
    /// </summary>
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
};

} // namespace dx12
