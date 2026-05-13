#include "AssetDefinition_GaussianSplatting.h"
#include "GaussianSplattingAsset.h"

FText UAssetDefinition_GaussianSplatting::GetAssetDisplayName() const
{
    return FText::FromString("ygs Asset");
}

FText UAssetDefinition_GaussianSplatting::GetAssetDescription(const FAssetData& AssetData) const
{
    if (auto Asset = Cast<UGaussianSplattingAsset>(AssetData.GetAsset()))
    {
        return FText::FromString(Asset->GetDescription());
    }
    return FText::GetEmpty();
}

FLinearColor UAssetDefinition_GaussianSplatting::GetAssetColor() const
{
    return FColor::Purple;
}

TSoftClassPtr<UObject> UAssetDefinition_GaussianSplatting::GetAssetClass() const
{
    return UGaussianSplattingAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_GaussianSplatting::GetAssetCategories() const
{
    static const FAssetCategoryPath AssetCategories[] = {
        FText::FromName("Gaussian Splatting"),
    };
    return MakeConstArrayView(AssetCategories);
}
