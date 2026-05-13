#pragma once

#include "GaussianSplattingFwd.h"

#include "SceneViewExtension.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectMacros.h"

#include "GaussianSplattingSubsystem.generated.h"

UCLASS(
    BlueprintType, Transient
)
class UGaussianSplattingSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    void RegisterComponent(const TWeakObjectPtr<UGaussianSplattingComponent>& Component);
    void UnregisterComponent(const TWeakObjectPtr<UGaussianSplattingComponent>& Component);

private:
    TSharedPtr<FGaussianSplattingViewExtension> SceneViewExtension;

    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;
};
