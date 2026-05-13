#pragma once

#include "AssetDefinitionDefault.h"
#include "AssetDefinition_GaussianSplatting.generated.h"

UCLASS()
class YAGSEDITOR_API UAssetDefinition_GaussianSplatting final : public UAssetDefinitionDefault
{
    GENERATED_BODY()

    FText GetAssetDisplayName() const override;
    FText GetAssetDescription(const FAssetData& AssetData) const override;
    FLinearColor GetAssetColor() const override;
    TSoftClassPtr<UObject> GetAssetClass() const override;
    TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};
