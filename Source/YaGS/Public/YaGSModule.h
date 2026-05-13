#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class YAGS_API FYaGSModule final : public IModuleInterface
{
    void StartupModule() override;
    void ShutdownModule() override;
};
