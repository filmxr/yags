#pragma once

#include "GaussianSplattingCommon.h"
#include "GaussianSplattingFwd.h"

#include "GaussianSplattingAppearanceProperties.generated.h"

USTRUCT(
    BlueprintType
)

struct YAGS_API FGaussianSplattingAppearanceProperties final
{
    GENERATED_BODY()

public:
    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (UIMin = 0, ClampMin = 0, Delta = 1, DisplayName = "Maximum SH degree")
    )
    int32 MaxSHDegree = YaGS::GMaxSHOrder - 1;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (UIMin = -1.0f, ClampMin = -1.0f, UIMax = 1.0f, ClampMax = 1.0f, Delta = 0.1f)
    )
    float Hue = 0.0f;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (UIMin = -1.0f, ClampMin = -1.0f, UIMax = 1.0f, ClampMax = 1.0f, Delta = 0.1f)
    )
    float Saturation = 0.0f;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (UIMin = -1.0f, ClampMin = -1.0f, UIMax = 1.0f, ClampMax = 1.0f, Delta = 0.1f)
    )
    float Brightness = 0.0f;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (HideAlphaChannel)
    )
    FLinearColor Tint = FLinearColor::White;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (UIMin = 0.0f, ClampMin = 0.0f, Delta = 0.1f)
    )
    float Gamma = 1.0f;

    FGaussianSplattingAppearance ToAppearance() const;
};
