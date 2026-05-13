#pragma once

#include "CoreMinimal.h"

namespace YaGS
{

inline constexpr int32 GMaxSHOrder = 4;
static_assert(GMaxSHOrder > 0);

inline constexpr uint32 GVertexCountPerQuad = 4;
inline constexpr uint32 GTriangleCountPerQuad = 2;

inline const FTransform DefaultModelTransform{
    /*Rotation*/ FQuat::MakeFromEuler({90.0, 0.0, 0.0}),
    /*Translation*/ FVector::ZeroVector,
    /*Scale3D*/ FVector{UE_M_TO_CM},
};

inline const FTransform DefaultModelTransformInverse = DefaultModelTransform.Inverse();

inline float Sigmoid(
    float X
)
{
    return 1.0f / (1.0f + FMath::Exp(-X));
}

inline float SigmoidInverse(
    float Y
)
{
    if (Y >= 1.0f)
    {
        return TNumericLimits<float>::Max();
    }
    return -FMath::Loge(1.0f / Y - 1.0f);
}

}  // namespace YaGS
