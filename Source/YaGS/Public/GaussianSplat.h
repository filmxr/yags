#pragma once

#include "GaussianSplattingCommon.h"
#include "GaussianSplattingTransform.h"

#include "CoreMinimal.h"

#include <type_traits>

namespace YaGS
{

inline constexpr float GSHCoeffData[] = {
    0.28209479177387814f,
    -0.4886025119029199f,
    0.4886025119029199f,
    -0.4886025119029199f,
    1.0925484305920792f,
    -1.0925484305920792f,
    0.31539156525252005f,
    -1.0925484305920792f,
    0.5462742152960396f,
    -0.5900435899266435f,
    2.890611442640554f,
    -0.4570457994644658f,
    0.3731763325901154f,
    -0.4570457994644658f,
    1.445305721320277f,
    -0.5900435899266435f
};
static_assert(GetNum(GSHCoeffData) >= GMaxSHOrder * GMaxSHOrder);

}  // namespace YaGS

#pragma pack(push, 1)

template<int32 MaxSHOrder>
struct TGaussianSplat;

template<>
struct TGaussianSplat<1>
{
    FGaussianSplattingTransform Transform;
    FVector3f Normal;
    FVector3f AlbedoColor;
    float AlbedoAlpha;

    void SetSH(
        const TArray<float>&
    )
    {
    }

    void GetSH(
        TArray<float>&
    ) const
    {
    }
};

template<int32 MaxSHOrder>
struct TGaussianSplat
{
    FGaussianSplattingTransform Transform;
    FVector3f Normal;
    FVector3f AlbedoColor;
    float AlbedoAlpha;
    FVector3f SH[MaxSHOrder * MaxSHOrder - 1];

    void SetSH(const TArray<float>& InSH)
    {
        check(InSH.Num() % 3 == 0);
        const int32 SHCount = InSH.Num() / 3;
        int32 Index = 0;
        for (FVector3f& RGB : SH)
        {
            if (Index < SHCount)
            {
                FVector3f Components = {
                    InSH[0 * SHCount + Index],
                    InSH[1 * SHCount + Index],
                    InSH[2 * SHCount + Index],
                };
                RGB = YaGS::GSHCoeffData[Index + 1] * Components;
            }
            else
            {
                RGB = FVector3f::ZeroVector;
            }
            ++Index;
        }
    }

    void GetSH(
        TArray<float>& OutSH
    ) const
    {
        check(OutSH.Num() % 3 == 0);
        const int32 SHCount = OutSH.Num() / 3;
        int32 Index = 0;
        for (const FVector3f& RGB : SH)
        {
            if (Index == SHCount)
            {
                break;
            }
            FVector3f Components = RGB / YaGS::GSHCoeffData[Index + 1];
            OutSH[0 * SHCount + Index] = Components.X;
            OutSH[1 * SHCount + Index] = Components.Y;
            OutSH[2 * SHCount + Index] = Components.Z;
            ++Index;
        }
    }
};

#pragma pack(pop)

using FGaussianSplat = TGaussianSplat<YaGS::GMaxSHOrder>;
static_assert(std::is_standard_layout_v<FGaussianSplat>);
static_assert(std::is_trivially_copyable_v<FGaussianSplat>);
static_assert(
    FMath::CountBits(sizeof(FGaussianSplat)) != 1, "Performance will drastically degrade if size is power of two"
);

YAGS_API FArchive& operator<<(FArchive& Ar, FGaussianSplat& GaussianSplat);

using FGaussianSplats = TArray64<FGaussianSplat>;

namespace YaGS
{

inline constexpr typename FGaussianSplats::SizeType GChunkLen =
    TNumericLimits<int32>::Max() / FGaussianSplats::GetTypeSize();

}  // namespace YaGS
