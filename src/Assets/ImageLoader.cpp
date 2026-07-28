//=============================================================================
// ImageLoader.cpp
//   ImageLoader の実装。
//=============================================================================
#include "ImageLoader.h"

#include "../Common/GraphicsCommon.h"

#include <wincodec.h>

#include <format>
#include <stdexcept>

namespace dx12::assets
{
namespace
{
/// <summary>
/// WIC の窓口となるファクトリを 1 つだけ作って使い回します。
/// </summary>
/// <returns>ファクトリ。</returns>
/// <exception cref="HrException">生成に失敗した場合。</exception>
/// <remarks>
/// 画像を読むたびに作り直す必要はありません。
/// COM が初期化されていないとここで失敗します。
/// </remarks>
IWICImagingFactory* GetFactory()
{
    static ComPtr<IWICImagingFactory> factory;

    if (factory == nullptr)
    {
        DX_CHECK(::CoCreateInstance(CLSID_WICImagingFactory,
                                    nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory)));
    }

    return factory.Get();
}


/// <summary>
/// デコーダから 1 枚目の画像を取り出し、RGBA8 のピクセル列にします。
/// </summary>
/// <param name="decoder">画像を開いた状態のデコーダ。</param>
/// <returns>展開済みの画像。</returns>
/// <exception cref="HrException">WIC の呼び出しに失敗した場合。</exception>
ImageData ConvertToRgba8(IWICBitmapDecoder* decoder)
{
    // (1) フレームを取り出す。
    //   GIF のように複数枚を持つ形式もあるので「何枚目か」の指定が要る。
    ComPtr<IWICBitmapFrameDecode> frame;
    DX_CHECK(decoder->GetFrame(0, &frame));

    // (2) 形式を RGBA8 に揃える。
    //   ★ 元の画像がパレット・グレースケール・16bit など何であっても、
    //     ここを通せば必ず 32bppRGBA になる。呼び出し側は元の形式を気にしなくてよい。
    ComPtr<IWICFormatConverter> converter;
    DX_CHECK(GetFactory()->CreateFormatConverter(&converter));

    DX_CHECK(converter->Initialize(frame.Get(),
                                   GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone,
                                   nullptr,       // パレット（使わない）
                                   0.0,           // アルファ閾値
                                   WICBitmapPaletteTypeCustom));

    ImageData image;
    DX_CHECK(converter->GetSize(&image.width, &image.height));

    if (image.width == 0 || image.height == 0)
    {
        throw std::runtime_error("画像の大きさが 0 です。");
    }

    // (3) ピクセルを取り出す。
    //   stride は 1 行あたりのバイト数。RGBA8 なら幅 × 4 でぴったり。
    const uint32_t stride = image.width * 4;
    const uint32_t totalBytes = stride * image.height;

    image.pixels.resize(totalBytes);

    DX_CHECK(converter->CopyPixels(nullptr,   // 全体を取り出す
                                   stride,
                                   totalBytes,
                                   image.pixels.data()));

    return image;
}
} // namespace


/// <summary>
/// 画像ファイルを読み込んで RGBA8 に展開します。
/// </summary>
ImageData LoadImageFile(const std::wstring& filePath)
{
    ComPtr<IWICBitmapDecoder> decoder;

    // 形式は中身から自動判別される。拡張子を見ているわけではない。
    DX_CHECK(GetFactory()->CreateDecoderFromFilename(
        filePath.c_str(),
        nullptr,                          // ベンダー指定なし
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder));

    ImageData image = ConvertToRgba8(decoder.Get());

    Log(std::format(L"画像を読み込みました（{} x {}, RGBA8 {} バイト）",
                    image.width, image.height, image.pixels.size()));

    return image;
}


/// <summary>
/// メモリ上の画像データを RGBA8 に展開します。
/// </summary>
ImageData DecodeImageBytes(const uint8_t* bytes, size_t byteCount)
{
    // WIC はストリーム越しにしか読まないので、メモリをストリームに見せかける。
    ComPtr<IWICStream> stream;
    DX_CHECK(GetFactory()->CreateStream(&stream));

    DX_CHECK(stream->InitializeFromMemory(const_cast<uint8_t*>(bytes),
                                          static_cast<DWORD>(byteCount)));

    ComPtr<IWICBitmapDecoder> decoder;
    DX_CHECK(GetFactory()->CreateDecoderFromStream(
        stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder));

    return ConvertToRgba8(decoder.Get());
}

} // namespace dx12::assets
