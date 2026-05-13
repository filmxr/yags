#pragma once

#include "Modules/ModuleManager.h"

class WEBP_API FWebPModule final : public IModuleInterface
{
    void StartupModule() override;
    void ShutdownModule() override;
};
