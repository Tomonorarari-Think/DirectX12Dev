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

/// <summary>
/// シェーダーへ毎フレーム渡す定数の内容。
/// </summary>
/// <remarks>
/// <para>
/// この構造体のメモリ配置は、HLSL 側の <c>cbuffer SceneConstants</c>
/// （<c>shaders/Triangle.hlsl</c>）と一致していなければなりません。
/// 頂点レイアウトと同じく、ずれてもコンパイルエラーにはならず、
/// 実行時に絵が壊れるだけなので注意が必要です。
/// </para>
/// <para>
/// <c>XMFLOAT4X4</c> は 4x4 の float 行列を「そのままメモリに置く」ための型です。
/// 計算用の <c>XMMATRIX</c>（SIMD レジスタ向けに 16 バイト境界を要求する型）とは
/// 別物で、保存・受け渡しにはこちらを使います。
/// </para>
/// </remarks>
struct SceneConstants
{
    /// <summary>ワールド × ビュー × プロジェクションをまとめた変換行列。</summary>
    /// <remarks>
    /// HLSL 側が列優先で読むため、書き込む前に転置しておく必要があります
    /// （<see cref="TrianglePipeline::Update"/> を参照）。
    /// </remarks>
    DirectX::XMFLOAT4X4 worldViewProjection;
};


/// <summary>
/// 頂点 1 個ぶんのデータ構造。
/// </summary>
/// <remarks>
/// <para>
/// このメンバ配置は、
/// <list type="bullet">
///   <item>シェーダー側の <c>VSInput</c> 構造体（<c>shaders/Triangle.hlsl</c>）</item>
///   <item>入力レイアウト <c>D3D12_INPUT_ELEMENT_DESC</c>（<c>TrianglePipeline.cpp</c>）</item>
/// </list>
/// と完全に一致していなければなりません。
/// </para>
/// <para>
/// 3 か所のどれかがずれると、頂点が明後日の方向に飛んだり色が壊れたりします。
/// しかもコンパイルエラーにはならないため、DirectX 初学者が最も嵌まる罠です。
/// </para>
/// </remarks>
struct Vertex
{
    /// <summary>頂点の座標 (x, y, z)。NDC 座標で、画面中央が原点、範囲は -1〜+1。</summary>
    float position[3];

    /// <summary>頂点の色 (r, g, b, a)。各成分は 0.0〜1.0。</summary>
    float color[4];
};


/// <summary>
/// 三角形 1 枚を描くのに必要な「描画の設定一式」と「頂点データ」を持つクラス。
/// </summary>
/// <remarks>
/// <para>
/// <b>このクラスが用意する 3 つのもの</b>
/// </para>
/// <para>
/// <b>(1) ルートシグネチャ (Root Signature)</b><br/>
/// シェーダーが使う「外部からの入力の一覧表」。関数の引数リストに相当します。
/// 例）「0 番に行列を渡す」「1 番にテクスチャを渡す」といった取り決め。
/// CPU 側とシェーダー側でこの取り決めを共有することで、
/// GPU はデータの受け渡し方を高速に解決できます。
/// 今回の三角形は外部入力が一切無いため「空のルートシグネチャ」になりますが、
/// それでも省略はできず、必ず作る必要があります。
/// </para>
/// <para>
/// <b>(2) パイプラインステートオブジェクト (PSO)</b><br/>
/// 描画に関わるあらゆる設定を 1 つに固めたもの。
/// どのシェーダーを使うか、頂点データのメモリ配置、裏面を描くか、
/// 色をどう合成するか、出力先の形式などが含まれます。
/// </para>
/// <para>
/// <b>なぜ「1 つに固める」のか（DX12 の大きな設計思想）</b><br/>
/// DirectX 11 では設定を 1 つずつ個別に変更していました。
/// GPU は設定の組み合わせが確定するまで最適な機械語を生成できず、
/// 描画命令の直前に慌てて変換する必要がありました（＝カクつきの原因）。
/// DirectX 12 では組み合わせを事前に PSO として確定させるため、
/// GPU 用のコード生成を「読み込み時」に済ませられます。
/// 実行時は PSO を差し替えるだけ、という高速な構造になります。
/// </para>
/// <para>
/// <b>(3) 頂点バッファ (Vertex Buffer)</b><br/>
/// 三角形の頂点 3 個ぶんの座標と色を置いた GPU メモリ。
/// </para>
/// </remarks>
class TrianglePipeline
{
public:
    /// <summary>既定のコンストラクタ。まだ何も生成されません。</summary>
    TrianglePipeline() = default;

    /// <summary>デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。</summary>
    ~TrianglePipeline() = default;

    /// <summary>コピー構築は禁止です。</summary>
    TrianglePipeline(const TrianglePipeline&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    TrianglePipeline& operator=(const TrianglePipeline&) = delete;

    /// <summary>ルートシグネチャ・PSO・頂点バッファ・定数バッファを生成します。</summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="renderTargetFormat">
    /// 描画先の形式。PSO はこれを知っている必要があります。
    /// バックバッファの形式と食い違うと PSO の生成が失敗します。
    /// </param>
    /// <param name="frameCount">
    /// 定数バッファに用意するフレーム数（通常はバックバッファの枚数）。
    /// </param>
    /// <exception cref="HrException">いずれかの生成に失敗した場合。</exception>
    /// <exception cref="std::runtime_error">シェーダーファイルが見つからない場合。</exception>
    void Initialize(ID3D12Device* device, DXGI_FORMAT renderTargetFormat, uint32_t frameCount);

    /// <summary>このフレームの変換行列を計算し、定数バッファへ書き込みます。</summary>
    /// <param name="frameIndex">書き込み先のフレーム番号。</param>
    /// <param name="aspectRatio">画面の縦横比（幅 ÷ 高さ）。</param>
    /// <param name="totalSeconds">起動からの経過秒数。回転角の算出に使います。</param>
    /// <remarks>
    /// 描画命令を記録する前に呼んでください。
    /// 呼び出し時点で、そのフレーム番号の GPU 処理は完了している必要があります
    /// （<c>Renderer::Render</c> の先頭でフェンスを待っています）。
    /// </remarks>
    void Update(uint32_t frameIndex, float aspectRatio, float totalSeconds);

    /// <summary>コマンドリストに「三角形を描く」命令を記録します。</summary>
    /// <param name="commandList">記録先の（Reset 済みで開いている）コマンドリスト。</param>
    /// <param name="frameIndex">使用する定数バッファのフレーム番号。</param>
    /// <remarks>
    /// 実際に描かれるのは、このコマンドリストが GPU に投入された後です。
    /// PSO・ルートシグネチャ・定数バッファの設定もこのメソッドが行うため、
    /// 呼び出し側は「いつ描くか」だけを制御すれば済みます。
    /// </remarks>
    void RecordDrawCommands(ID3D12GraphicsCommandList* commandList, uint32_t frameIndex) const;

private:
    /// <summary>ルートシグネチャを生成します。</summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <exception cref="HrException">シリアライズまたは生成に失敗した場合。</exception>
    void CreateRootSignature(ID3D12Device* device);

    /// <summary>HLSL をコンパイルし、パイプラインステートオブジェクトを生成します。</summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <param name="renderTargetFormat">描画先の形式。</param>
    /// <exception cref="HrException">コンパイルまたは PSO 生成に失敗した場合。</exception>
    void CreatePipelineState(ID3D12Device* device, DXGI_FORMAT renderTargetFormat);

    /// <summary>頂点バッファを作り、頂点データを書き込みます。</summary>
    /// <param name="device">生成に使う D3D12 デバイス。</param>
    /// <exception cref="HrException">リソースの生成またはマップに失敗した場合。</exception>
    void CreateVertexBuffer(ID3D12Device* device);

    /// <summary>HLSL ファイルをコンパイルして、GPU 用のバイトコードを得ます。</summary>
    /// <param name="filePath">.hlsl ファイルの絶対パス。</param>
    /// <param name="entryPoint">入口となる関数名（<c>"VSMain"</c> など）。</param>
    /// <param name="target">シェーダーモデル（<c>"vs_5_0"</c> など）。</param>
    /// <returns>コンパイル済みバイトコードを保持する Blob。</returns>
    /// <exception cref="HrException">コンパイルに失敗した場合。</exception>
    /// <remarks>
    /// <para>
    /// <b>シェーダーのコンパイル方式は 2 通り</b>
    /// <list type="bullet">
    ///   <item>事前コンパイル … ビルド時に .cso を生成。起動が速く、製品版ではこちらが基本。</item>
    ///   <item>実行時コンパイル（本実装）… .hlsl を書き換えて再実行するだけで
    ///         結果を確認できるため、学習中はこちらが圧倒的に扱いやすい。</item>
    /// </list>
    /// </para>
    /// <para>
    /// <b>D3DCompile と DXC の違い</b><br/>
    /// ここで使う <c>D3DCompileFromFile</c> は「FXC」と呼ばれる旧コンパイラで、
    /// シェーダーモデル 5.1 までの対応です。三角形を描くには十分ですが、
    /// 最新機能（シェーダーモデル 6.x、レイトレーシング等）を使う場合は
    /// DXC (DirectX Shader Compiler) が必要になります。
    /// </para>
    /// </remarks>
    static ComPtr<ID3DBlob> CompileShader(const std::wstring& filePath,
                                          const char* entryPoint,
                                          const char* target);

private:
    /// <summary>シェーダーが受け取る外部入力の一覧表（今回は空）。</summary>
    ComPtr<ID3D12RootSignature> m_rootSignature;

    /// <summary>描画設定を 1 つに固めたパイプラインステートオブジェクト。</summary>
    ComPtr<ID3D12PipelineState> m_pipelineState;

    /// <summary>頂点データを置く GPU 上のメモリ領域。</summary>
    ComPtr<ID3D12Resource> m_vertexBuffer;

    /// <summary>変換行列をシェーダーへ渡すための定数バッファ（フレーム数ぶん）。</summary>
    ConstantBuffer m_constantBuffer;

    /// <summary>頂点バッファの読み取り方を GPU に伝える構造体。</summary>
    /// <remarks>
    /// 「バッファのどこから」「1 頂点何バイトで」「合計何バイト読むか」を伝えます。
    /// RTV や SRV と違ってディスクリプタヒープには置かず、
    /// 描画時にコマンドリストへ直接渡します（ヒープ管理が不要なぶん手軽）。
    /// </remarks>
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
};

} // namespace dx12
