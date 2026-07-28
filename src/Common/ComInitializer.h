//=============================================================================
// ComInitializer.h
//   COM の初期化と後始末を、スコープの寿命に合わせて行う小さな道具。
//
//   DirectX 12 自体は COM の初期化を必要としないが、
//   画像を読むのに使う WIC は COM のオブジェクトなので必要になる。
//=============================================================================
#pragma once

#include "GraphicsCommon.h"

#include <objbase.h>

namespace dx12
{

/// <summary>
/// 生成時に COM を初期化し、破棄時に後始末を行うクラス。
/// </summary>
/// <remarks>
/// スレッドごとに 1 つ必要です。本プロジェクトは 1 スレッドしか使わないので、
/// `main` の先頭で 1 つ作れば足ります。
/// </remarks>
class ComInitializer
{
public:
    /// <summary>
    /// COM を初期化します。
    /// </summary>
    /// <exception cref="HrException">初期化に失敗した場合。</exception>
    /// <remarks>
    /// `APARTMENTTHREADED` は「このスレッドからしか触らない」という宣言です。
    /// WIC はどちらでも動きますが、シェル系の API と併用する場合に無難な方を選びました。
    /// </remarks>
    ComInitializer()
    {
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        // 既に初期化済みでも構わない。その場合は自分では後始末しない。
        if (hr == RPC_E_CHANGED_MODE || hr == S_FALSE)
        {
            m_shouldUninitialize = false;
            return;
        }

        DX_CHECK(hr);
        m_shouldUninitialize = true;
    }

    /// <summary>COM の後始末を行います。</summary>
    ~ComInitializer()
    {
        if (m_shouldUninitialize)
        {
            ::CoUninitialize();
        }
    }

    /// <summary>コピー構築は禁止です。</summary>
    ComInitializer(const ComInitializer&) = delete;

    /// <summary>コピー代入は禁止です。</summary>
    ComInitializer& operator=(const ComInitializer&) = delete;

private:
    /// <summary>自分が初期化したので、自分で後始末すべきかどうか。</summary>
    bool m_shouldUninitialize = false;
};

} // namespace dx12
