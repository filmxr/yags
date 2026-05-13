#pragma once

#include "CoreMinimal.h"
#include "Matrix3x4.h"

namespace YaGS
{

inline constexpr float GGaussianSplatScale = 4.0f * UE_SQRT_2;

}  // namespace YaGS

#pragma pack(push, 1)

struct YAGS_API FGaussianSplattingTransform
{
    decltype(FMatrix3x4::M) Transform;  // Rotation * Translation
    FVector3f Scale;

    void Set(const FMatrix44f& InTransform, const FVector3f& InScale);

    FQuat4f GetRotation() const;
    FVector3f GetTranslation() const;
};

static_assert(std::is_standard_layout_v<FGaussianSplattingTransform>);
static_assert(std::is_trivially_copyable_v<FGaussianSplattingTransform>);

#pragma pack(pop)
