#include "WebP.h"

#include "WebPLog.h"

THIRD_PARTY_INCLUDES_START
#include "webp/decode.h"
THIRD_PARTY_INCLUDES_END

namespace WebP
{

namespace
{

template<typename TPixel>
constexpr auto GetWebPDecode()
{
    if constexpr (std::is_same_v<TPixel, FPixelRGBA>)
    {
        return &WebPDecodeRGBAInto;
    }
    else if constexpr (std::is_same_v<TPixel, FPixelRGB>)
    {
        return &WebPDecodeRGBInto;
    }
    else
    {
        static_assert(sizeof(TPixel) == 0);
    }
}

}  // namespace

template<typename TPixel>
auto TImage<TPixel>::Read(
    const FZipArchiveReader& File, const FString& Filepath
) -> TImage
{
    TArray<uint8> Data;
    if (!File.TryReadFile(Filepath, Data))
    {
        UE_LOG(LogWebP, Warning, TEXT("Failed to read file '%s' from SOG file"), *Filepath);
        return {};
    }
    return {Data, FPaths::GetBaseFilename(Filepath)};
}

template<typename TPixel>
TImage<TPixel>::TImage(
    TConstArrayView<uint8> InData, const FString& InImageName
)
    : ImageName{InImageName}
{
    const auto Data = reinterpret_cast<const uint8_t*>(InData.GetData());
    const auto DataSize = StaticCast<size_t>(InData.NumBytes());
    WebPBitstreamFeatures Features = {};
    if (WebPGetFeatures(Data, DataSize, &Features) != VP8StatusCode::VP8_STATUS_OK)
    {
        UE_LOG(LogWebP, Error, TEXT("Failed to get features for image '%s'"), *ImageName);
        return;
    }
    Width = StaticCast<int32>(Features.width);
    Height = StaticCast<int32>(Features.height);
    if (Features.format == 1)
    {
        UE_LOG(LogWebP, Warning, TEXT("Image '%s' has a lossy format"), *ImageName);
    }
    else if (Features.format == 2)
    {
        UE_LOG(LogWebP, Log, TEXT("Image '%s' has a lossless format"), *ImageName);
    }
    else
    {
        UE_LOG(LogWebP, Error, TEXT("Image '%s' has an unknown format"), *ImageName);
        return;
    }
    if (Features.has_animation)
    {
        UE_LOG(LogWebP, Error, TEXT("Image '%s' has animation"), *ImageName);
    }
    UE_LOG(LogWebP, Log, TEXT("Image '%s' size: %ix%i"), *ImageName, Width, Height);
    if constexpr (std::is_same_v<TPixel, FPixelRGBA>)
    {
        if (Features.has_alpha == 0)
        {
            UE_LOG(LogWebP, Error, TEXT("RGBA image '%s' has no alpha channel"), *ImageName);
            return;
        }
    }
    else if constexpr (std::is_same_v<TPixel, FPixelRGB>)
    {
        if (Features.has_alpha != 0)
        {
            UE_LOG(LogWebP, Warning, TEXT("RGB image '%s' has alpha channel"), *ImageName);
        }
    }
    else
    {
        static_assert(sizeof(TPixel) == 0);
    }
    Image.SetNumUninitialized(Width * Height);
    const auto DataOut = GetWebPDecode<TPixel>()(
        Data,
        DataSize,
        reinterpret_cast<uint8_t*>(Image.GetData()),
        StaticCast<size_t>(Image.NumBytes()),
        StaticCast<size_t>(Width) * sizeof(TPixel)
    );
    if (DataOut == nullptr)
    {
        Features = {};
        Image.Empty();
        UE_LOG(LogWebP, Error, TEXT("Failed to decode image '%s'"), *ImageName);
        return;
    }
}

template WEBP_API auto TImage<FPixelRGBA>::Read(const FZipArchiveReader& File, const FString& Filepath) -> TImage;
template WEBP_API auto TImage<FPixelRGB>::Read(const FZipArchiveReader& File, const FString& Filepath) -> TImage;

template class TImage<FPixelRGBA> WEBP_API;
template class TImage<FPixelRGB> WEBP_API;

}  // namespace WebP
