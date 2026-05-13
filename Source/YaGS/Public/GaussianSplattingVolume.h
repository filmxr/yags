#pragma once

#include "GaussianSplattingAppearanceProperties.h"
#include "GaussianSplattingFwd.h"

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"

#include "GaussianSplattingVolume.generated.h"

UCLASS(Abstract, Hidden)
class YAGS_API AGaussianSplattingVolume : public AVolume
{
    GENERATED_BODY()

public:
    AGaussianSplattingVolume();

    virtual void CopyToBytes(TGaussianSplattingByteAddressBufferView<> ByteAddressBuffer) const;
};

UCLASS()
class YAGS_API AGaussianSplattingBooleanVolume final : public AGaussianSplattingVolume
{
    GENERATED_BODY()
};

UCLASS()
class YAGS_API AGaussianSplattingAppearanceVolume final : public AGaussianSplattingVolume
{
    GENERATED_BODY()

public:
    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (UIMin = 0.0f, Delta = 0.1f)
    )
    float FalloffDistance = 0.0f;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (ShowOnlyInnerProperties)
    )
    FGaussianSplattingAppearanceProperties Appearance;

    void CopyToBytes(TGaussianSplattingByteAddressBufferView<> ByteAddressBuffer) const override;
};
