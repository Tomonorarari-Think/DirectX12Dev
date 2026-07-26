//=============================================================================
// DepthBuffer.h
//   奥行き判定（深度テスト）に使うバッファと、その DSV。
//=============================================================================
#pragma once

#include "../Common/GraphicsCommon.h"

namespace dx12
{

/// @brief 深度バッファ（Z バッファ）と、その深度ステンシルビューを管理するクラス。
///
/// **深度バッファとは**
///
/// 画面のピクセル 1 個につき「そこに描かれている物の奥行き」を 1 個記録しておく
/// 画像です。色を記録するレンダーターゲットと、縦横のサイズが同じで、
/// 中身が「色」ではなく「奥行き」になったもの、と考えると分かりやすいです。
///
/// **何のためにあるのか**
///
/// 深度バッファが無いと、**あとから描いたものが必ず手前に見えます**。
/// 3D の世界では「奥にあるはずの壁」を後から描いただけで手前のキャラクターが
/// 消えてしまい、破綻します。
///
/// 深度バッファがあると、GPU はピクセルを塗る直前に
///
/// 1. これから塗る点の奥行きを計算する
/// 2. 深度バッファに記録済みの奥行きと比べる
/// 3. 手前なら塗って深度も更新、奥なら**捨てる**
///
/// という判定（深度テスト）を自動で行います。
/// その結果、**描く順番に関係なく前後関係が正しくなります**。
///
/// **深度テストは速度のためでもある**
///
/// 奥にあると分かったピクセルはピクセルシェーダーを実行せずに捨てられます
/// （Early-Z）。手前の物体を先に描くほど無駄な処理が減るため、
/// 実際のゲームでは「おおまかに手前から順に描く」最適化がよく行われます。
///
/// **フォーマットの選び方**
///
/// - `DXGI_FORMAT_D32_FLOAT` … 32bit 浮動小数点。精度が高い（本実装）
/// - `DXGI_FORMAT_D24_UNORM_S8_UINT` … 24bit の深度＋8bit のステンシル
/// - `DXGI_FORMAT_D16_UNORM` … 16bit。精度は低いが省メモリ
///
/// ステンシル（型抜きなどに使う付加情報）は今回使わないため、
/// 深度だけの `D32_FLOAT` を選んでいます。
class DepthBuffer
{
public:
    /// @brief 深度バッファのフォーマット。
    ///
    /// PSO の `DSVFormat` にも同じ値を指定する必要があります。
    /// 食い違うとデバッグレイヤーがエラーを出します。
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_D32_FLOAT;

    /// @brief 毎フレームのクリアに使う深度値。
    ///
    /// 深度は「手前が 0.0、奥が 1.0」です。
    /// 最初に「一番奥」で埋めておくことで、どんな物体を描いても
    /// 「記録済みより手前」と判定されて必ず描かれます。
    static constexpr float kClearDepth = 1.0f;

    /// @brief 既定のコンストラクタ。まだ何も生成されません。
    DepthBuffer() = default;

    /// @brief デストラクタ。ComPtr により全ての COM オブジェクトが自動解放されます。
    ~DepthBuffer() = default;

    /// @brief コピー構築は禁止です。
    DepthBuffer(const DepthBuffer&) = delete;

    /// @brief コピー代入は禁止です。
    DepthBuffer& operator=(const DepthBuffer&) = delete;

    /// @brief DSV ディスクリプタヒープと深度バッファ本体を生成します。
    /// @param device 生成に使う D3D12 デバイス。
    /// @param width 深度バッファの幅（ピクセル）。レンダーターゲットと同じにすること。
    /// @param height 深度バッファの高さ（ピクセル）。レンダーターゲットと同じにすること。
    /// @exception HrException 生成に失敗した場合。
    void Initialize(ID3D12Device* device, uint32_t width, uint32_t height);

    /// @brief ウィンドウサイズ変更に追従して深度バッファを作り直します。
    /// @param device 生成に使う D3D12 デバイス。
    /// @param width 新しい幅（ピクセル）。0 なら何もしません。
    /// @param height 新しい高さ（ピクセル）。0 なら何もしません。
    /// @exception HrException 生成に失敗した場合。
    ///
    /// @warning 呼び出し前に必ず GPU の作業完了を待ってください。
    /// GPU がまだ書き込んでいるバッファを破棄すると即クラッシュします。
    void Resize(ID3D12Device* device, uint32_t width, uint32_t height);

    /// @brief 深度ステンシルビュー（DSV）の位置を取得します。
    /// @returns ヒープ内の CPU ディスクリプタハンドル。
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const
    {
        return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    }

private:
    /// @brief 深度バッファのリソースを作り、DSV をヒープに書き込みます。
    /// @param device 生成に使う D3D12 デバイス。
    /// @param width 幅（ピクセル）。
    /// @param height 高さ（ピクセル）。
    /// @exception HrException 生成に失敗した場合。
    void CreateResourceAndView(ID3D12Device* device, uint32_t width, uint32_t height);

private:
    /// @brief DSV を 1 個だけ置くためのディスクリプタヒープ。
    ///
    /// RTV 用ヒープ（`SwapChain` が持つ）とは種類が違うため、別に用意します。
    /// ディスクリプタヒープは種類ごとに分ける決まりです。
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    /// @brief 深度値を記録するテクスチャ本体。
    ///
    /// バックバッファと違いスワップチェーンが用意してくれないため、
    /// 自分で `CreateCommittedResource` して作ります。
    /// 1 枚しか作らないのは、深度バッファがフレームをまたいで
    /// 内容を持ち越さない（毎フレーム先頭でクリアする）ためです。
    ComPtr<ID3D12Resource> m_depthBuffer;

    /// @brief 現在の幅（ピクセル）。
    uint32_t m_width = 0;

    /// @brief 現在の高さ（ピクセル）。
    uint32_t m_height = 0;
};

} // namespace dx12
