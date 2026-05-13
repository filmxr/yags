#pragma once

#include "GaussianSplattingFwd.h"

#include "GameFramework/Actor.h"

#include "GaussianSplattingActor.generated.h"

UCLASS(ComponentWrapperClass)
class YAGS_API AGaussianSplattingActor final : public AActor
{
    GENERATED_BODY()

public:
    AGaussianSplattingActor();

    UPROPERTY(
        Category = "GaussianSplatting", VisibleAnywhere, BlueprintReadOnly, meta = (ShowInnerProperties, Instanced)
    )
    TObjectPtr<UGaussianSplattingComponent> GaussianSplattingComponent;
};
