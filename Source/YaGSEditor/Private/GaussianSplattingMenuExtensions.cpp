#include "GaussianSplattingMenuExtensions.h"
#include "GaussianSplattingEditingActions.h"
#include "GaussianSplattingActor.h"
#include "GaussianSplattingActorFactory.h"
#include "GaussianSplattingAsset.h"
#include "GaussianSplattingAssetFactory.h"
#include "GaussianSplattingCommon.h"
#include "GaussianSplattingComponent.h"
#include "GaussianSplattingEditorLog.h"
#include "LevelEditor.h"
#include "Selection.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"

#define LOCTEXT_NAMESPACE UE_PLUGIN_NAME

namespace
{

class FGaussianSplattingEditingActions final : public FGaussianSplattingEditingActionsBase
{
public:
    void CreateNewAssetAndActor(
        FGaussianSplattingActorStrongPtrs&& Actors,
        const FVector& SelectionOrigin,
        FGaussianSplattingStaticBuffer&& StaticBuffer
    ) const override
    {
        check(IsInGameThread());
        check(!Actors.IsEmpty());
        int32 MaxSHDegree = YaGS::GMaxSHOrder - 1;
        IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
        FString PackageName;
        FString AssetName;
        AssetTools.CreateUniqueAssetName(TEXT("/Game/" UE_PLUGIN_NAME "/GS"), TEXT(""), PackageName, AssetName);
        auto AssetFactory = NewObject<UGaussianSplattingAssetFactory>();
        auto Asset = AssetTools.CreateAssetWithDialog(
            AssetName,
            PackageName,
            UGaussianSplattingAsset::StaticClass(),
            AssetFactory
        );
        if (!Asset)
        {
            UE_LOG(LogYaGSEditor, Log, TEXT("Asset creation is cancelled by user"));
            return;
        }
        TArray<UGaussianSplattingAsset::FMesh> ConvexHulls;
        for (const auto& OldActor : Actors)
        {
            check(OldActor);
            if (auto OldComponent = OldActor->GaussianSplattingComponent)
            {
                if (auto OldAsset = OldComponent->Asset)
                {
                    FTransform Transform = OldComponent->GetComponentTransform();
                    Transform.AddToTranslation(-SelectionOrigin);
                    Transform *= YaGS::DefaultModelTransformInverse;
                    for (UGaussianSplattingAsset::FMesh ConvexHull : OldAsset->GetConvexHulls())
                    {
                        for (FVector& Vertex : ConvexHull.Vertices)
                        {
                            Vertex = Transform.TransformPosition(Vertex);
                        }
                        ConvexHulls.Add(MoveTemp(ConvexHull));
                    }
                    MaxSHDegree = FMath::Min(MaxSHDegree, OldAsset->GetMaxSHDegree());
                }
            }
        }
        auto NewAsset = CastChecked<UGaussianSplattingAsset>(Asset);
        NewAsset->LoadData(MoveTemp(StaticBuffer), MoveTemp(ConvexHulls), MaxSHDegree);
        if (!GEditor)
        {
            UE_LOG(LogYaGSEditor, Log, TEXT("Creation of actor is skipped: no editor context"));
            return;
        }
        if (UWorld* World = GEditor->GetEditorWorldContext().World())
        {
            if (auto ActorFactory = NewObject<UGaussianSplattingActorFactory>())
            {
                FActorSpawnParameters SpawnParams;
                FTransform Transform{SelectionOrigin};
                auto Actor = ActorFactory->SpawnActor(NewAsset, World->GetCurrentLevel(), Transform, SpawnParams);
                if (!Actor)
                {
                    UE_LOG(LogYaGSEditor, Warning, TEXT("Actor creation failed"));
                    return;
                }
                auto NewActor = CastChecked<AGaussianSplattingActor>(Actor);
                ActorFactory->PostSpawnActor(NewAsset, NewActor);
                GEditor->SelectNone(
                    /*bNoteSelectionChange*/ false,
                    /*bDeselectBSPSurfs*/ true
                );
                GEditor->SelectActor(
                    NewActor,
                    /*bInSelected*/ true,
                    /*bNotify*/ true
                );
            }
            else
            {
                UE_LOG(
                    LogYaGSEditor,
                    Warning,
                    TEXT(
                        "Creation of actor is skipped: cannot create actor "
                        "factory"
                    )
                );
            }
        }
        else
        {
            UE_LOG(
                LogYaGSEditor,
                Log,
                TEXT(
                    "Creation of actor is skipped: cannot get editor world "
                    "context"
                )
            );
        }
    }
};

}

void FGaussianSplattingMenuExtensions::InstallHooks()
{
    EditingActions = MakeShared<FGaussianSplattingEditingActions>();
    if (auto* LevelEditorModule = FModuleManager::Get().LoadModulePtr<FLevelEditorModule>("LevelEditor"))
    {
        LevelEditorMenuExtenderDelegate = FSelectedActors::CreateLambda(
            [this](TSharedRef<FUICommandList> CommandList, const TArray<AActor*>& SelectedActors)
            {
                return OnExtendLevelEditorMenu(MoveTemp(CommandList), SelectedActors);
            }
        );
        auto& MenuExtenders = LevelEditorModule->GetAllLevelViewportContextMenuExtenders();
        MenuExtenders.Add(LevelEditorMenuExtenderDelegate);
    }
}

void FGaussianSplattingMenuExtensions::RemoveHooks()
{
    if (auto* LevelEditorModule = FModuleManager::Get().GetModulePtr<FLevelEditorModule>("LevelEditor"))
    {
        check(LevelEditorMenuExtenderDelegate.IsBound());
        const auto Predicate =
            [Handle = LevelEditorMenuExtenderDelegate.GetHandle()](const FSelectedActors& Delegate) -> bool
        {
            return Delegate.GetHandle() == Handle;
        };
        auto& MenuExtenders = LevelEditorModule->GetAllLevelViewportContextMenuExtenders();
        MenuExtenders.RemoveAll(Predicate);
    }
    check(EditingActions.IsValid());
    EditingActions.Reset();
}

void FGaussianSplattingMenuExtensions::CreateActionMenuSection(
    FMenuBuilder& MenuBuilder, TSharedRef<FGaussianSplattingActorWeakPtrs> Actors
) const
{
    MenuBuilder.BeginSection(UE_PLUGIN_NAME, LOCTEXT("LevelEditorHeading", "Gaussian splatting"));
    const bool bCanFuse = (Actors->Num() > 0);
    if (bCanFuse)
    {
        FUIAction UIAction = FExecuteAction::CreateLambda(
            [this, Actors]() mutable
            {
                EditingActions->Fuse(*Actors);
            }
        );
        MenuBuilder.AddMenuEntry(
            LOCTEXT("MenuExtensionFuse", "Fuse actors"),
            LOCTEXT("MenuExtensionFuse_Tooltip", "Replaces all of the selected actors with a single fused actor"),
            FSlateIcon{},
            UIAction,
            NAME_None,
            EUserInterfaceActionType::Button
        );
    }
    const bool bCanReplace = false;
    if (bCanReplace)
    {
        // TODO:
    }
    MenuBuilder.EndSection();
}

TSharedRef<FExtender> FGaussianSplattingMenuExtensions::OnExtendLevelEditorMenu(
    TSharedRef<FUICommandList> CommandList,
    const TArray<AActor*>& SelectedActors
)
{
    FGaussianSplattingActorWeakPtrs Actors;
    Actors.Reserve(SelectedActors.Num());
    for (AActor* SelectedActor : SelectedActors)
    {
        if (auto Actor = Cast<AGaussianSplattingActor>(SelectedActor))
        {
            Actors.Push(Actor);
        }
    }
    auto MenuExtender = MakeShared<FExtender>();
    auto MenuExtensionDelegate = FMenuExtensionDelegate::CreateLambda(
        [this,
         Actors = MakeShared<FGaussianSplattingActorWeakPtrs>(MoveTemp(Actors))](FMenuBuilder& MenuBuilder) mutable
        {
            CreateActionMenuSection(MenuBuilder, MoveTemp(Actors));
        }
    );
    MenuExtender->AddMenuExtension(
        "ActorTypeTools",
        EExtensionHook::After,
        /*CommandList*/ nullptr,
        MenuExtensionDelegate
    );
    return MenuExtender;
}

#undef LOCTEXT_NAMESPACE
