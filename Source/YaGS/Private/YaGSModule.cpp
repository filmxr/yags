#include "YaGSModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE UE_MODULE_NAME

void FYaGSModule::StartupModule()
{
    FString PluginBaseDir = IPluginManager::Get().FindPlugin(TEXT(UE_PLUGIN_NAME))->GetBaseDir();
    FString PluginShaderSourceDir = FPaths::Combine(PluginBaseDir, TEXT("Shaders"));
    AddShaderSourceDirectoryMapping(TEXT("/Plugin/" UE_PLUGIN_NAME), PluginShaderSourceDir);
}

void FYaGSModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(
    FYaGSModule, YaGS
)

#undef LOCTEXT_NAMESPACE
