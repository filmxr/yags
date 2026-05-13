#include "GaussianSplattingTransform.h"

void FGaussianSplattingTransform::Set(
    const FMatrix44f& InTransform, const FVector3f& InScale
)
{
    InTransform.To3x4MatrixTranspose(&Transform[0][0]);
    check(FMath::Abs(InTransform.M[0][3]) < UE_KINDA_SMALL_NUMBER);
    check(FMath::Abs(InTransform.M[1][3]) < UE_KINDA_SMALL_NUMBER);
    check(FMath::Abs(InTransform.M[2][3]) < UE_KINDA_SMALL_NUMBER);
    check(FMath::Abs(InTransform.M[3][3] - 1.0f) < UE_KINDA_SMALL_NUMBER);
    Scale = InScale;
}

FQuat4f FGaussianSplattingTransform::GetRotation() const
{
    auto OutTransform = FMatrix44f::Identity;
    OutTransform.M[0][0] = Transform[0][0];
    OutTransform.M[1][0] = Transform[0][1];
    OutTransform.M[2][0] = Transform[0][2];
    OutTransform.M[3][0] = Transform[0][3];
    OutTransform.M[0][1] = Transform[1][0];
    OutTransform.M[1][1] = Transform[1][1];
    OutTransform.M[2][1] = Transform[1][2];
    OutTransform.M[3][1] = Transform[1][3];
    OutTransform.M[0][2] = Transform[2][0];
    OutTransform.M[1][2] = Transform[2][1];
    OutTransform.M[2][2] = Transform[2][2];
    OutTransform.M[3][2] = Transform[2][3];
    return OutTransform.ToQuat();
}

FVector3f FGaussianSplattingTransform::GetTranslation() const
{
    return {
        Transform[0][3],
        Transform[1][3],
        Transform[2][3],
    };
}
