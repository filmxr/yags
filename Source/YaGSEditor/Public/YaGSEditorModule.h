#pragma once

#include "GaussianSplattingEditorFwd.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class YAGSEDITOR_API FYaGSEditorModule final : public IModuleInterface
{
    TUniquePtr<FGaussianSplattingMenuExtensions> LevelEditorMenuExtensions;

    void StartupModule() override;
    void ShutdownModule() override;
};
