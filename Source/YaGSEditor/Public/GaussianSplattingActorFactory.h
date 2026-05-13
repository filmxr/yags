#pragma once

#include "ActorFactories/ActorFactory.h"
#include "GaussianSplattingActorFactory.generated.h"

UCLASS()
class YAGSEDITOR_API UGaussianSplattingActorFactory final : public UActorFactory
{
    GENERATED_BODY()

public:
    UGaussianSplattingActorFactory();

    using UActorFactory::SpawnActor;

    bool CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg) override;

    void PostSpawnActor(UObject* Asset, AActor* NewActor) override;

    UObject* GetAssetFromActorInstance(AActor* ActorInstance) override;
};
