//=============================================================================
// ImageLoader.h
//   PNG / JPEG などの画像ファイルを、GPU へ載せられる形に展開する。
//
//   Windows に標準で入っている WIC (Windows Imaging Component) を使う。
//   詳しい解説は docs/tutorial/17_画像を読み込む_WIC.md を参照。
//=============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dx12::assets
{

/// <summary>
/// 展開済みの画像。
/// </summary>
struct ImageData
{
    /// <summary>横のピクセル数。</summary>
    uint32_t width = 0;

    /// <summary>縦のピクセル数。</summary>
    uint32_t height = 0;

    /// <summary>
    /// ピクセル列。R, G, B, A の順に 1 バイトずつ、左上から右へ、上から下へ並びます。
    /// </summary>
    /// <remarks>
    /// 要素数は `width × height × 4` です。`Texture2D` がそのまま受け取れる形にしています。
    /// </remarks>
    std::vector<uint8_t> pixels;
};


/// <summary>
/// 画像ファイルを読み込んで RGBA8 に展開します。
/// </summary>
/// <param name="filePath">画像ファイルの絶対パス。</param>
/// <returns>展開済みの画像。</returns>
/// <exception cref="HrException">WIC の呼び出しに失敗した場合。</exception>
/// <exception cref="std::runtime_error">画像が空だった場合。</exception>
/// <remarks>
/// PNG / JPEG / BMP / GIF / TIFF などに対応します。
/// 対応する形式は WIC 側が持っているので、こちらで増やす必要はありません。
/// 呼び出す前に COM を初期化しておく必要があります（`ComInitializer`）。
/// </remarks>
ImageData LoadImageFile(const std::wstring& filePath);

/// <summary>
/// メモリ上の画像データを RGBA8 に展開します。
/// </summary>
/// <param name="bytes">画像ファイルの中身。</param>
/// <param name="byteCount">バイト数。</param>
/// <returns>展開済みの画像。</returns>
/// <exception cref="HrException">WIC の呼び出しに失敗した場合。</exception>
/// <remarks>
/// glTF が画像をファイル内に埋め込んでいる場合に使います。
/// </remarks>
ImageData DecodeImageBytes(const uint8_t* bytes, size_t byteCount);

} // namespace dx12::assets
