#include "GaussianSplattingSubsystem.h"
#include "GaussianSplattingViewExtension.h"
#include "GaussianSplattingLog.h"

void UGaussianSplattingSubsystem::RegisterComponent(
    const TWeakObjectPtr<UGaussianSplattingComponent>& Component
)
{
    check(IsInGameThread());
    if (SceneViewExtension)
    {
        SceneViewExtension->AddComponent(Component);
    }
}

void UGaussianSplattingSubsystem::UnregisterComponent(
    const TWeakObjectPtr<UGaussianSplattingComponent>& Component
)
{
    check(IsInGameThread());
    if (SceneViewExtension)
    {
        SceneViewExtension->RemoveComponent(Component);
    }
}

void UGaussianSplattingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (auto World = GetWorld(); IsValid(World))
    {
        SceneViewExtension = FSceneViewExtensions::NewExtension<FGaussianSplattingViewExtension>(World);
    }
}

void UGaussianSplattingSubsystem::Deinitialize()
{
    SceneViewExtension.Reset();
    Super::Deinitialize();
}
