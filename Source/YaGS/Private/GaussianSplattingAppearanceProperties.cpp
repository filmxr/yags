#include "GaussianSplattingAppearanceProperties.h"

#include "GaussianSplattingShaders.h"

FGaussianSplattingAppearance FGaussianSplattingAppearanceProperties::ToAppearance() const
{
    return {
        .MaxSHDegree = MaxSHDegree,
        .HSV = FVector3f{
            Hue,
            Saturation,
            Brightness,
        },
        .Tint = FVector3f{ Tint },
        .Gamma = Gamma,
    };
}
