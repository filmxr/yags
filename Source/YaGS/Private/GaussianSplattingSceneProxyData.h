#pragma once

#include "GaussianSplattingFwd.h"
#include "GaussianSplattingShaders.h"

#include "CoreMinimal.h"

struct FGaussianSplattingSceneProxyData
{
    FString Name;
    int RenderingOrder = 0;
    FMatrix LocalToWorld;
    FBox BoundingBox;
    TSharedRef<FGaussianSplattingStaticBuffer> StaticBuffer;
    bool bIsSRGB = true;
    float Scale = 1.0f;
    float MaxLambda = 1.0f;
    FGaussianSplattingAppearance Appearance;
    TArray<uint8> Crops;
    TArray<uint8> Culls;
    TArray<uint8> LocalAppearances;

    static TArray<uint8> ToBytes(const TArray<TSoftObjectPtr<AGaussianSplattingBooleanVolume>>& Volumes);

    static TArray<uint8> ToBytes(const TArray<TSoftObjectPtr<AGaussianSplattingAppearanceVolume>>& Volumes);

    bool operator==(const FGaussianSplattingSceneProxyData&) const = default;
    bool operator!=(const FGaussianSplattingSceneProxyData&) const = default;
    bool operator<(const FGaussianSplattingSceneProxyData& Rhs) const;
};
