#include "GaussianSplattingActor.h"

#include "GaussianSplattingCommon.h"
#include "GaussianSplattingComponent.h"
#include "GaussianSplattingSubsystem.h"

AGaussianSplattingActor::AGaussianSplattingActor()
{
    GaussianSplattingComponent = CreateDefaultSubobject<UGaussianSplattingComponent>(TEXT(UE_MODULE_NAME "Component"));
    RootComponent = GaussianSplattingComponent;
    AddActorLocalTransform(YaGS::DefaultModelTransform);
}
