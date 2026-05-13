#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "GaussianSplattingAssetFactory.generated.h"

UCLASS()
class YAGSEDITOR_API UGaussianSplattingAssetFactory final : public UFactory
{
    GENERATED_UCLASS_BODY()

    UObject* FactoryCreateNew(
        UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn
    ) override;

    UObject* FactoryCreateFile(
        UClass* InClass,
        UObject* InParent,
        FName InName,
        EObjectFlags Flags,
        const FString& Filename,
        const TCHAR* Parms,
        FFeedbackContext* Warn,
        bool& bOutOperationCanceled
    ) override;
};
