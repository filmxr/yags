#pragma once

#include "CoreMinimal.h"
#include "FileUtilities/ZipArchiveReader.h"
#include "HAL/Platform.h"

#include <type_traits>

namespace WebP
{

struct FPixelRGBA
{
    uint8 r, g, b, a;
};

struct FPixelRGB
{
    uint8 r, g, b;
};

template<typename TPixel>
class TImage
{
public:
    TImage() = default;

    int32 GetWidth() const
    {
        return Width;
    }
    int32 GetHeight() const
    {
        return Height;
    }

    int32 Num() const
    {
        return Image.Num();
    }

    explicit operator bool() const
    {
        return !Image.IsEmpty();
    }

    const TPixel& operator[](
        int32 Index
    ) const&
    {
        return Image[Index];
    }

    const TPixel& operator()(
        int32 X, int32 Y
    ) const&
    {
        return operator[](X + Y * GetWidth());
    }

    static TImage Read(const FZipArchiveReader& File, const FString& Filepath);

protected:
    TImage(TConstArrayView<uint8> InData, const FString& InImageName);

    FString ImageName;
    int32 Width = 0;
    int32 Height = 0;
    TArray<TPixel> Image;
};

extern template auto TImage<FPixelRGBA>::Read(const FZipArchiveReader& File, const FString& Filepath) -> TImage;
extern template auto TImage<FPixelRGB>::Read(const FZipArchiveReader& File, const FString& Filepath) -> TImage;

extern template class TImage<FPixelRGBA>;
extern template class TImage<FPixelRGB>;

}  // namespace WebP

using FWebPImageRGBA = WebP::TImage<WebP::FPixelRGBA>;
using FWebPImageRGB = WebP::TImage<WebP::FPixelRGB>;
