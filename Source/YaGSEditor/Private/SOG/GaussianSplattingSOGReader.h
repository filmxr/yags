#pragma once

#include "CoreMinimal.h"
#include "GaussianSplat.h"

namespace YaGS
{

TArray<TTuple<FString, FString>> GetSOGExtensionsAndDescriptions();

bool ReadSOG(const FString& Filename, FGaussianSplats& GaussianSplatData, int32& MaxSHDegree);

}  // namespace YaGS
