#pragma once

#include "GaussianSplattingAppearanceProperties.h"
#include "GaussianSplattingFwd.h"

#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodySetup.h"

#include "GaussianSplattingComponent.generated.h"

class AVolume;

UCLASS(ClassGroup = Rendering)
class YAGS_API UGaussianSplattingComponent final : public UPrimitiveComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(
        Category = "GaussianSplatting",
        VisibleAnywhere,
        BlueprintReadOnly,
        meta = (NoResetToDefault)
    )
    TObjectPtr<UGaussianSplattingAsset> Asset;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (Delta = 1)
    )
    int32 RenderingOrder = 0;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (Delta = 0.1f, DisplayName = "Gaussian Splat Scale")
    )
    float Scale = 1.0f;

    UPROPERTY(
        Category = "GaussianSplatting", EditAnywhere, BlueprintReadWrite, meta = (UIMin = 0.0f, ClampMin = 0.0f)
    )
    float MaxLambda = 1.0f;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (ShowOnlyInnerProperties)
    )
    FGaussianSplattingAppearanceProperties Appearance;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (NoResetToDefault, EditFixedOrder, NoElementDuplicate)
    )
    TArray<TSoftObjectPtr<AGaussianSplattingBooleanVolume>> Crops;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (NoResetToDefault, EditFixedOrder, NoElementDuplicate)
    )
    TArray<TSoftObjectPtr<AGaussianSplattingBooleanVolume>> Culls;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite,
        meta = (NoResetToDefault, EditFixedOrder, NoElementDuplicate)
    )
    TArray<TSoftObjectPtr<AGaussianSplattingAppearanceVolume>> LocalAppearances;

    using UPrimitiveComponent::UPrimitiveComponent;

    FString GetName() const;

    UBodySetup* GetBodySetup() override;

    FGaussianSplattingSceneProxyData ToSceneProxyData() const;

private:
    UPROPERTY()
    TObjectPtr<UBodySetup> BodySetup;

    void OnRegister() override;
    void OnUnregister() override;

    FPrimitiveSceneProxy* CreateSceneProxy() override;
    FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

#if WITH_EDITOR
    void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const override;

    bool ShouldCollideWhenPlacing() const override;
#endif
};
