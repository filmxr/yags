#include "GaussianSplattingAssetFactory.h"

#include "GaussianSplattingAsset.h"
#include "GaussianSplattingEditorLog.h"
#include "PLY/GaussianSplattingPLYReader.h"
#include "SOG/GaussianSplattingSOGReader.h"

#include "ObjectTools.h"

UGaussianSplattingAssetFactory::UGaussianSplattingAssetFactory(
    const FObjectInitializer& ObjectInitializer
)
    : Super{ObjectInitializer}
{
    SupportedClass = UGaussianSplattingAsset::StaticClass();
    for (const auto& ExtensionsAndDescriptions :
         {YaGS::GetPLYExtensionsAndDescriptions(), YaGS::GetSOGExtensionsAndDescriptions()})
    {
        for (const auto& [Ext, Descr] : ExtensionsAndDescriptions)
        {
            Formats.Add(FString::Printf(TEXT("%s;%s"), *Ext, *Descr));
        }
    }
    bEditorImport = true;
    ImportPriority = 1;
}

UObject* UGaussianSplattingAssetFactory::FactoryCreateNew(
    UClass* InClass,
    UObject* InParent,
    FName InName,
    EObjectFlags Flags,
    UObject* Context,
    FFeedbackContext* Warn
)
{
    check(InClass->IsChildOf(UGaussianSplattingAsset::StaticClass()));
    EnumAddFlags(Flags, RF_Transactional);
    return NewObject<UGaussianSplattingAsset>(InParent, InClass, InName, Flags);
}

UObject* UGaussianSplattingAssetFactory::FactoryCreateFile(
    UClass* InClass,
    UObject* InParent,
    FName InName,
    EObjectFlags Flags,
    const FString& Filename,
    const TCHAR* Parms,
    FFeedbackContext* Warn,
    bool& bOutOperationCanceled
)
{
    check(InClass->IsChildOf(UGaussianSplattingAsset::StaticClass()));
    EnumAddFlags(Flags, RF_Transactional);
    TUniquePtr<UGaussianSplattingAsset> Asset{ NewObject<UGaussianSplattingAsset>(InParent, InName, Flags) };
    {
        FGaussianSplats GaussianSplats;
        int32 MaxSHDegree = YaGS::GMaxSHOrder - 1;
        auto Extension = FPaths::GetExtension(Filename).ToLower();
        const auto MatchExtension = [&Extension](const TTuple<FString, FString>& ExtAndDesc)
        {
            return Extension == ExtAndDesc.Key;
        };
        if (YaGS::GetPLYExtensionsAndDescriptions().ContainsByPredicate(MatchExtension))
        {
            if (!YaGS::ReadPLY(Filename, GaussianSplats, MaxSHDegree))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to read PLY file '%s'"), *Filename);
                return Asset.Release();
            }
        }
        else if (YaGS::GetSOGExtensionsAndDescriptions().ContainsByPredicate(MatchExtension))
        {
            if (!YaGS::ReadSOG(Filename, GaussianSplats, MaxSHDegree))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to read SOG file '%s'"), *Filename);
                return Asset.Release();
            }
        }
        else
        {
            checkNoEntry();
        }
        if (GaussianSplats.IsEmpty())
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("PLY file '%s' has zero splats"), *Filename);
            return Asset.Release();
        }
        if (!Asset->LoadData(MoveTemp(GaussianSplats), MaxSHDegree))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to load PLY file '%s' data"), *Filename);
        }
    }
    return Asset.Release();
}
