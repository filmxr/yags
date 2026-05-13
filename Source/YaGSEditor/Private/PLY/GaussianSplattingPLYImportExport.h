#pragma once

#include "CoreMinimal.h"
#include "GaussianSplat.h"

#include <type_traits>

#pragma pack(push, 1)

struct FGaussianSplatPLY
{
    FVector3f Position;
    FVector3f Normal;
    FVector3f Color;
    float Opacity;
    FVector3f Scale;
    float OrientationQuatRe;
    FVector3f OrientationQuatIm;

    FQuat4f GetOrientation() const
    {
        // clang-format off
        return FQuat4f{
            OrientationQuatIm.X,
            OrientationQuatIm.Y,
            OrientationQuatIm.Z,
            OrientationQuatRe,
        }.GetNormalized();
        // clang-format on
    }
};

static_assert(std::is_standard_layout_v<FGaussianSplatPLY>);
static_assert(std::is_trivially_copyable_v<FGaussianSplatPLY>);

#pragma pack(pop)

namespace YaGS
{

inline constexpr FAnsiStringView BeginHeader = ANSITEXTVIEW("ply");
inline constexpr FAnsiStringView EndHeader = ANSITEXTVIEW("end_header\n");

FGaussianSplat ConvertGaussianSplat(const FGaussianSplatPLY& InGaussianSplat, const TArray<float>& SH);
FGaussianSplatPLY ConvertGaussianSplat(const FGaussianSplat& InGaussianSplat, TArray<float>& SH);

}  // namespace YaGS
