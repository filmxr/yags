#include "GaussianSplattingPLYImportExport.h"

#include "GaussianSplattingCommon.h"

namespace YaGS
{

FGaussianSplat ConvertGaussianSplat(
    const FGaussianSplatPLY& InGaussianSplat, const TArray<float>& SH
)
{
    FGaussianSplat GaussianSplat = {};
    {
        FVector3f Scale = {
            FMath::Exp(InGaussianSplat.Scale.X),
            FMath::Exp(InGaussianSplat.Scale.Y),
            FMath::Exp(InGaussianSplat.Scale.Z),
        };
        FTransform3f Transform{
            InGaussianSplat.GetOrientation(),
            InGaussianSplat.Position,
            /* Scale */ FVector3f::OneVector,
        };
        GaussianSplat.Transform.Set(Transform.ToMatrixNoScale(), YaGS::GGaussianSplatScale * Scale);
    }
    GaussianSplat.Normal = InGaussianSplat.Normal.GetSafeNormal();
    GaussianSplat.AlbedoColor = YaGS::GSHCoeffData[0] * InGaussianSplat.Color + 0.5f;
    GaussianSplat.AlbedoAlpha = YaGS::Sigmoid(InGaussianSplat.Opacity);
    GaussianSplat.SetSH(SH);
    return GaussianSplat;
}

FGaussianSplatPLY ConvertGaussianSplat(
    const FGaussianSplat& InGaussianSplat, TArray<float>& SH
)
{
    FGaussianSplatPLY GaussianSplat = {};
    GaussianSplat.Position = InGaussianSplat.Transform.GetTranslation();
    {
        FVector3f Scale = InGaussianSplat.Transform.Scale / YaGS::GGaussianSplatScale;
        GaussianSplat.Scale = {
            (Scale.X > 0.0f) ? FMath::Loge(Scale.X) : TNumericLimits<float>::Lowest(),
            (Scale.Y > 0.0f) ? FMath::Loge(Scale.Y) : TNumericLimits<float>::Lowest(),
            (Scale.Z > 0.0f) ? FMath::Loge(Scale.Z) : TNumericLimits<float>::Lowest(),
        };
    }
    {
        FQuat4f Orientation = InGaussianSplat.Transform.GetRotation();
        GaussianSplat.OrientationQuatRe = Orientation.W;
        GaussianSplat.OrientationQuatIm = {
            Orientation.X,
            Orientation.Y,
            Orientation.Z,
        };
    }
    GaussianSplat.Normal = InGaussianSplat.Normal;
    GaussianSplat.Color = (InGaussianSplat.AlbedoColor - 0.5f) / YaGS::GSHCoeffData[0];
    GaussianSplat.Opacity = SigmoidInverse(InGaussianSplat.AlbedoAlpha);
    InGaussianSplat.GetSH(SH);
    return GaussianSplat;
}

}  // namespace YaGS
