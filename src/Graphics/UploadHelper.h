//=============================================================================
// UploadHelper.h
//   CPU のデータを GPU 専用メモリ（DEFAULT ヒープ）へ転送する共通処理。
//
//   DEFAULT ヒープには CPU から直接書けないため、UPLOAD ヒープの
//   中継バッファを経由して GPU にコピーさせる（ステージング転送）。
//   詳しい解説は docs/tutorial/09_テクスチャを貼る.md を参照。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

#include <functional>

namespace dx12
{
class CommandQueue;

namespace upload
{

/// <summary>
/// 使い捨てのコマンドリストへ転送コマンドを記録し、実行して完了まで待ちます。
/// </summary>
/// <param name="device">生成に使う D3D12 デバイス。</param>
/// <param name="commandQueue">コマンドを実行するキュー。</param>
/// <param name="record">コマンドリストへ記録する処理。</param>
/// <exception cref="HrException">コマンドの生成または実行に失敗した場合。</exception>
/// <remarks>
/// 完了を待つのは、呼び出し元が持つ中継バッファを安全に破棄できるようにするためです。
/// 起動時の 1 回だけを想定しており、毎フレーム呼ぶものではありません。
/// </remarks>
void ExecuteImmediate(ID3D12Device* device,
                      CommandQueue& commandQueue,
                      const std::function<void(ID3D12GraphicsCommandList*)>& record);

/// <summary>
/// UPLOAD ヒープに中継用のバッファを作ります。
/// </summary>
/// <param name="device">生成に使う D3D12 デバイス。</param>
/// <param name="sizeInBytes">確保するバイト数。</param>
/// <returns>CPU から書き込める（GENERIC_READ 状態の）バッファ。</returns>
/// <exception cref="HrException">生成に失敗した場合。</exception>
ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, uint64_t sizeInBytes);

/// <summary>
/// DEFAULT ヒープにバッファを作り、データを転送して返します。
/// </summary>
/// <param name="device">生成に使う D3D12 デバイス。</param>
/// <param name="commandQueue">転送コマンドを実行するキュー。完了まで待機します。</param>
/// <param name="data">転送するデータの先頭アドレス。</param>
/// <param name="sizeInBytes">転送するバイト数。</param>
/// <param name="finalState">転送後に遷移させる状態。用途に応じて指定します。</param>
/// <returns>データが書き込まれた DEFAULT ヒープ上のバッファ。</returns>
/// <exception cref="HrException">生成または転送に失敗した場合。</exception>
ComPtr<ID3D12Resource> CreateBufferWithData(ID3D12Device* device,
                                            CommandQueue& commandQueue,
                                            const void* data,
                                            uint64_t sizeInBytes,
                                            D3D12_RESOURCE_STATES finalState);

} // namespace upload
} // namespace dx12
