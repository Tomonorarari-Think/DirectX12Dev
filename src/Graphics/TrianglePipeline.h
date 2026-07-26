//=============================================================================
// TrianglePipeline.h
//   三角形 1 枚を描くための描画設定一式（ルートシグネチャ・PSO・頂点バッファ）。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"
#include "ConstantBuffer.h"

// DirectXMath : Windows SDK に同梱される数学ライブラリ。
//   ベクトル・行列演算を SIMD 命令（SSE / AVX）で高速に行える。
//   追加のライブラリのリンクは不要（ヘッダオンリー）。
#include <DirectXMath.h>

namespace dx12
{

/// @brief シェーダーへ毎フレーム渡す定数の内容。
///
/// この構造体のメモリ配置は、HLSL 側の `cbuffer SceneConstants` （`shaders/Triangle.hlsl`）と一致し
/// ていなければなりません。頂点レイアウトと同じく、ずれてもコンパイルエラーにはならず、実行時に絵が
/// 壊れるだけなので注意が必要です。
///
/// `XMFLOAT4X4` は 4x4 の float 行列を「そのままメモリに置く」ための型です。計算用の
/// `XMMATRIX`（SIMD レジスタ向けに 16 バイト境界を要求する型）とは別物で、保存・受け渡しにはこちら
/// を使います。
struct SceneConstants
{
    /// @brief ワールド × ビュー × プロジェクションをまとめた変換行列。
    ///
    /// HLSL 側が列優先で読むため、書き込む前に転置しておく必要があります（`TrianglePipeline::Update` を
    /// 参照）。
    DirectX::XMFLOAT4X4 worldViewProjection;
};


/// @brief 頂点 1 個ぶんのデータ構造。
///
/// このメンバ配置は、
///
/// - シェーダー側の `VSInput` 構造体（`shaders/Triangle.hlsl`）
/// - 入力レイアウト `D3D12_INPUT_ELEMENT_DESC`（`TrianglePipeline.cpp`）
///
/// と完全に一致していなければなりません。
///
/// 3 か所のどれかがずれると、頂点が明後日の方向に飛んだり色が壊れたりします。しかもコンパイルエラー
/// にはならないため、DirectX 初学者が最も嵌まる罠です。
struct Vertex
{
    /// @brief 頂点の座標 (x, y, z)。NDC 座標で、画面中央が原点、範囲は -1〜+1。
    float position[3];

    /// @brief 頂点の色 (r, g, b, a)。各成分は 0.0〜1.0。
    float color[4];
};


/// @brief 三角形 1 枚を描くのに必要な「描画の設定一式」と「頂点データ」を持つクラス。
///
/// **このクラスが用意する 3 つのもの**
///
/// **(1) ルートシグネチャ (Root Signature)**
///
/// シェーダーが使う「外部からの入力の一覧表」。関数の引数リストに相当します。例）「0 番に行列を渡す」
/// 「1 番にテクスチャを渡す」といった取り決め。CPU
/// 側とシェーダー側でこの取り決めを共有することで、GPU はデータの受け渡し方を高速に解決できます。今
/// 回の三角形は外部入力が一切無いため「空のルートシグネチャ」になりますが、それでも省略はできず、必
/// ず作る必要があります。
///
/// **(2) パイプラインステートオブジェクト (PSO)**
///
/// 描画に関わるあらゆる設定を 1 つに固めたもの。どのシェーダーを使うか、頂点データのメモリ配置、裏
/// 面を描くか、色をどう合成するか、出力先の形式などが含まれます。
///
/// **なぜ「1 つに固める」のか（DX12 の大きな設計思想）**
///
/// DirectX 11 では設定を 1 つずつ個別に変更していました。GPU は設定の組み合わせが確定するまで最適な
/// 機械語を生成できず、描画命令の直前に慌てて変換する必要がありました（＝カクつきの原因）。DirectX
/// 12 では組み合わせを事前に PSO として確定させるため、GPU 用のコード生成を「読み込み時」に済ませら
/// れます。実行時は PSO を差し替えるだけ、という高速な構造になります。
///
/// **(3) 頂点バッファ (Vertex Buffer)**
///
/// 三角形の頂点 3 個ぶんの座標と色を置いた GPU メモリ。
class TrianglePipeline
{
public:
    /// @brief 既定のコンストラクタ。まだ何も生成されません。
    TrianglePipeline() = default;

    /// @brief デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。
    ~TrianglePipeline() = default;

    /// @brief コピー構築は禁止です。
    TrianglePipeline(const TrianglePipeline&) = delete;

    /// @brief コピー代入は禁止です。
    TrianglePipeline& operator=(const TrianglePipeline&) = delete;

    /// @brief ルートシグネチャ・PSO・頂点バッファ・定数バッファを生成します。
    /// @param device 生成に使う D3D12 デバイス。
    /// @param renderTargetFormat 描画先の形式。PSO はこれを知っている必要があります。バックバッファの形
    ///     式と食い違うと PSO の生成が失敗します。
    /// @param frameCount 定数バッファに用意するフレーム数（通常はバックバッファの枚数）。
    /// @exception HrException いずれかの生成に失敗した場合。
    /// @exception std::runtime_error シェーダーファイルが見つからない場合。
    void Initialize(ID3D12Device* device, DXGI_FORMAT renderTargetFormat, uint32_t frameCount);

    /// @brief このフレームの変換行列を計算し、定数バッファへ書き込みます。
    /// @param frameIndex 書き込み先のフレーム番号。
    /// @param aspectRatio 画面の縦横比（幅 ÷ 高さ）。
    /// @param totalSeconds 起動からの経過秒数。回転角の算出に使います。
    ///
    /// 描画命令を記録する前に呼んでください。呼び出し時点で、そのフレーム番号の GPU 処理は完了している
    /// 必要があります（`Renderer::Render` の先頭でフェンスを待っています）。
    void Update(uint32_t frameIndex, float aspectRatio, float totalSeconds);

    /// @brief コマンドリストに「三角形を描く」命令を記録します。
    /// @param commandList 記録先の（Reset 済みで開いている）コマンドリスト。
    /// @param frameIndex 使用する定数バッファのフレーム番号。
    ///
    /// 実際に描かれるのは、このコマンドリストが GPU に投入された後です。PSO・ルートシグネチャ・定数バッ
    /// ファの設定もこのメソッドが行うため、呼び出し側は「いつ描くか」だけを制御すれば済みます。
    void RecordDrawCommands(ID3D12GraphicsCommandList* commandList, uint32_t frameIndex) const;

private:
    /// @brief ルートシグネチャを生成します。
    /// @param device 生成に使う D3D12 デバイス。
    /// @exception HrException シリアライズまたは生成に失敗した場合。
    void CreateRootSignature(ID3D12Device* device);

    /// @brief HLSL をコンパイルし、パイプラインステートオブジェクトを生成します。
    /// @param device 生成に使う D3D12 デバイス。
    /// @param renderTargetFormat 描画先の形式。
    /// @exception HrException コンパイルまたは PSO 生成に失敗した場合。
    void CreatePipelineState(ID3D12Device* device, DXGI_FORMAT renderTargetFormat);

    /// @brief 頂点バッファを作り、頂点データを書き込みます。
    /// @param device 生成に使う D3D12 デバイス。
    /// @exception HrException リソースの生成またはマップに失敗した場合。
    void CreateVertexBuffer(ID3D12Device* device);

    /// @brief HLSL ファイルをコンパイルして、GPU 用のバイトコードを得ます。
    /// @param filePath .hlsl ファイルの絶対パス。
    /// @param entryPoint 入口となる関数名（`"VSMain"` など）。
    /// @param target シェーダーモデル（`"vs_5_0"` など）。
    /// @returns コンパイル済みバイトコードを保持する Blob。
    /// @exception HrException コンパイルに失敗した場合。
    ///
    /// **シェーダーのコンパイル方式は 2 通り**
    ///
    /// - 事前コンパイル … ビルド時に .cso を生成。起動が速く、製品版ではこちらが基本。
    /// - 実行時コンパイル（本実装）… .hlsl を書き換えて再実行するだけで結果を確認できるため、学習中はこ
    ///   ちらが圧倒的に扱いやすい。
    ///
    /// **D3DCompile と DXC の違い**
    ///
    /// ここで使う `D3DCompileFromFile` は「FXC」と呼ばれる旧コンパイラで、シェーダーモデル 5.1 までの対
    /// 応です。三角形を描くには十分ですが、最新機能（シェーダーモデル 6.x、レイトレーシング等）を使う場
    /// 合は DXC (DirectX Shader Compiler) が必要になります。
    static ComPtr<ID3DBlob> CompileShader(const std::wstring& filePath,
                                          const char* entryPoint,
                                          const char* target);

private:
    /// @brief シェーダーが受け取る外部入力の一覧表（今回は空）。
    ComPtr<ID3D12RootSignature> m_rootSignature;

    /// @brief 描画設定を 1 つに固めたパイプラインステートオブジェクト。
    ComPtr<ID3D12PipelineState> m_pipelineState;

    /// @brief 頂点データを置く GPU 上のメモリ領域。
    ComPtr<ID3D12Resource> m_vertexBuffer;

    /// @brief 変換行列をシェーダーへ渡すための定数バッファ（フレーム数ぶん）。
    ConstantBuffer m_constantBuffer;

    /// @brief 頂点バッファの読み取り方を GPU に伝える構造体。
    ///
    /// 「バッファのどこから」「1 頂点何バイトで」「合計何バイト読むか」を伝えます。RTV や SRV と違って
    /// ディスクリプタヒープには置かず、描画時にコマンドリストへ直接渡します（ヒープ管理が不要なぶん手軽）。
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
};

} // namespace dx12
