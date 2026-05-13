#pragma once

#include "GaussianSplattingFwd.h"

#include "CoreMinimal.h"
#include "Tasks/Pipe.h"

class YAGS_API FGaussianSplattingEditingActionsBase
{
public:
    virtual ~FGaussianSplattingEditingActionsBase();

    bool Fuse(const FGaussianSplattingActorWeakPtrs& Actors) const;

protected:
    virtual void CreateNewAssetAndActor(
        FGaussianSplattingActorStrongPtrs&& Actors,
        const FVector& SelectionOrigin,
        FGaussianSplattingStaticBuffer&& StaticBuffer
    ) const = 0;

private:
    mutable UE::Tasks::FPipe Pipe{TEXT(UE_MODULE_NAME ".EditingActions")};
};
