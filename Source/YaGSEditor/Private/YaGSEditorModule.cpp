#include "YaGSEditorModule.h"

#include "GaussianSplattingMenuExtensions.h"

#define LOCTEXT_NAMESPACE UE_MODULE_NAME

void FYaGSEditorModule::StartupModule()
{
    LevelEditorMenuExtensions = MakeUnique<FGaussianSplattingMenuExtensions>();
    LevelEditorMenuExtensions->InstallHooks();
}

void FYaGSEditorModule::ShutdownModule()
{
    check(LevelEditorMenuExtensions.IsValid());
    LevelEditorMenuExtensions->RemoveHooks();
}

IMPLEMENT_MODULE(
    FYaGSEditorModule, YaGSEditor
)

#undef LOCTEXT_NAMESPACE
