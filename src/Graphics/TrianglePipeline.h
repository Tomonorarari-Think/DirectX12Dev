//=============================================================================
// TrianglePipeline.h
//   三角形 1 枚を描くのに必要な「描画の設定一式」と「頂点データ」を持つクラス。
//
//   ■ このクラスが用意する 3 つのもの
//
//     (1) ルートシグネチャ (Root Signature)
//           シェーダーが使う「外部からの入力の一覧表」。関数の引数リストに相当。
//           例）「0 番に行列を渡す」「1 番にテクスチャを渡す」といった取り決め。
//           CPU 側とシェーダー側でこの取り決めを共有することで、
//           GPU はデータの受け渡し方を高速に解決できます。
//           今回の三角形は外部入力が一切無いため「空のルートシグネチャ」になります。
//           （それでも省略はできず、必ず作る必要があります）
//
//     (2) パイプラインステートオブジェクト (PSO / Pipeline State Object)
//           描画に関わるあらゆる設定を 1 つに固めたもの。
//           　・どの頂点シェーダー / ピクセルシェーダーを使うか
//           　・頂点データのメモリ配置（入力レイアウト）
//           　・裏面を描くか（ラスタライザ設定）
//           　・色をどう合成するか（ブレンド設定）
//           　・出力先の形式　など
//
//           ■ なぜ「1 つに固める」のか（DX12 の大きな設計思想）
//             DirectX 11 では設定を 1 つずつ個別に変更していました。
//             GPU は設定の組み合わせが確定するまで最適な機械語を生成できず、
//             描画命令の直前に慌てて変換する必要がありました（＝カクつきの原因）。
//             DirectX 12 では組み合わせを事前に PSO として確定させるため、
//             GPU 用のコード生成を「読み込み時」に済ませられます。
//             実行時は PSO を差し替えるだけ、という高速な構造になります。
//
//     (3) 頂点バッファ (Vertex Buffer)
//           三角形の頂点 3 個ぶんの座標と色を置いた GPU メモリ。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{
//-----------------------------------------------------------------------------
// 頂点 1 個ぶんのデータ構造
//
//   ★ この構造体のメンバ配置は、
//      ・シェーダー側の VSInput 構造体（shaders/Triangle.hlsl）
//      ・入力レイアウト D3D12_INPUT_ELEMENT_DESC（TrianglePipeline.cpp）
//     と完全に一致していなければなりません。
//     3 か所のどれかがずれると、頂点が明後日の方向に飛んだり色が壊れたりします。
//     しかもコンパイルエラーにはならないため、DirectX 初学者が最も嵌まる罠です。
//-----------------------------------------------------------------------------
struct Vertex
{
    float position[3]; // x, y, z （NDC 座標。画面中央が原点、範囲は -1〜+1）
    float color[4];    // r, g, b, a （各 0.0〜1.0）
};


class TrianglePipeline
{
public:
    TrianglePipeline() = default;
    ~TrianglePipeline() = default;

    TrianglePipeline(const TrianglePipeline&) = delete;
    TrianglePipeline& operator=(const TrianglePipeline&) = delete;

    //-------------------------------------------------------------------------
    // 初期化
    //   @param device           生成に使う D3D12 デバイス
    //   @param renderTargetFormat 描画先の形式。PSO はこれを知っている必要がある
    //-------------------------------------------------------------------------
    void Initialize(ID3D12Device* device, DXGI_FORMAT renderTargetFormat);

    //-------------------------------------------------------------------------
    // コマンドリストに「三角形を描く」命令を記録する
    //   実際に描かれるのは、このコマンドリストが GPU に投入された後です。
    //-------------------------------------------------------------------------
    void RecordDrawCommands(ID3D12GraphicsCommandList* commandList) const;

private:
    // ルートシグネチャを作る
    void CreateRootSignature(ID3D12Device* device);

    // HLSL をコンパイルし、PSO を作る
    void CreatePipelineState(ID3D12Device* device, DXGI_FORMAT renderTargetFormat);

    // 頂点バッファを作り、頂点データを書き込む
    void CreateVertexBuffer(ID3D12Device* device);

    //-------------------------------------------------------------------------
    // HLSL ファイルをコンパイルして、GPU 用のバイトコードを得る
    //   @param filePath   .hlsl ファイルの絶対パス
    //   @param entryPoint 関数名（"VSMain" など）
    //   @param target     シェーダーモデル（"vs_5_0" など）
    //-------------------------------------------------------------------------
    static ComPtr<ID3DBlob> CompileShader(const std::wstring& filePath,
                                          const char* entryPoint,
                                          const char* target);

private:
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    // 頂点データを置く GPU 上のメモリ領域
    ComPtr<ID3D12Resource> m_vertexBuffer;

    //-------------------------------------------------------------------------
    // 頂点バッファビュー
    //   「バッファのどこから」「1 頂点何バイトで」「合計何バイト読むか」を
    //   GPU に伝えるための小さな構造体。
    //   RTV や SRV と違ってディスクリプタヒープには置かず、
    //   描画時にコマンドリストへ直接渡します（ヒープ管理が不要なぶん手軽）。
    //-------------------------------------------------------------------------
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
};

} // namespace dx12
