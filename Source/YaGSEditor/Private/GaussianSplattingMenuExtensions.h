#pragma once

#include "GaussianSplattingEditorFwd.h"
#include "GaussianSplattingFwd.h"

#include "CoreMinimal.h"
#include "LevelEditor.h"

class YAGSEDITOR_API FGaussianSplattingMenuExtensions
{
public:
    void InstallHooks();
    void RemoveHooks();

private:
    using FSelectedActors = FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors;

    FSelectedActors LevelEditorMenuExtenderDelegate;
    TSharedPtr<FGaussianSplattingEditingActionsBase> EditingActions;

    void CreateActionMenuSection(FMenuBuilder& MenuBuilder, TSharedRef<FGaussianSplattingActorWeakPtrs> Actors) const;

    TSharedRef<FExtender> OnExtendLevelEditorMenu(
        TSharedRef<FUICommandList> CommandList,
        const TArray<AActor*>& SelectedActors
    );
};
