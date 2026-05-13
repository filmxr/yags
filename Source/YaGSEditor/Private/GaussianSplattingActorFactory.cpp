#include "GaussianSplattingActorFactory.h"
#include "GaussianSplattingActor.h"
#include "GaussianSplattingAsset.h"
#include "GaussianSplattingComponent.h"

#define LOCTEXT_NAMESPACE UE_PLUGIN_NAME

UGaussianSplattingActorFactory::UGaussianSplattingActorFactory()
{
    NewActorClass = AGaussianSplattingActor::StaticClass();
}

bool UGaussianSplattingActorFactory::CanCreateActorFrom(
    const FAssetData& AssetData,
    FText& OutErrorMsg
)
{
    if (!AssetData.IsValid())
    {
        OutErrorMsg = LOCTEXT("NotGaussianSplattingAsset", "Asset data is invalid.");
        return false;
    }
    if (!AssetData.IsInstanceOf(UGaussianSplattingAsset::StaticClass()))
    {
        OutErrorMsg =
            LOCTEXT("NotGaussianSplattingAsset", "Asset data is not instaince of UGaussianSplattingAsset class.");
        return false;
    }
    return true;
}

void UGaussianSplattingActorFactory::PostSpawnActor(
    UObject* Asset,
    AActor* NewActor
)
{
    Super::PostSpawnActor(Asset, NewActor);
    auto* Actor = CastChecked<AGaussianSplattingActor>(NewActor);
    auto Component = Actor->GaussianSplattingComponent;
    check(Component);
    Component->UnregisterComponent();
    ON_SCOPE_EXIT
    {
        Component->RegisterComponent();
        // clang-format off
    };
    // clang-format on
    Component->Asset = CastChecked<UGaussianSplattingAsset>(Asset);
}

UObject* UGaussianSplattingActorFactory::GetAssetFromActorInstance(
    AActor* ActorInstance
)
{
    check(ActorInstance->IsA(NewActorClass));
    auto Actor = CastChecked<AGaussianSplattingActor>(ActorInstance);
    auto Component = Actor->GaussianSplattingComponent;
    check(Component);
    return Component->Asset.Get();
}

#undef LOCTEXT_NAMESPACE
