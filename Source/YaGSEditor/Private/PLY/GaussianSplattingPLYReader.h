#pragma once

#include "CoreMinimal.h"
#include "GaussianSplat.h"

namespace YaGS
{

TArray<TTuple<FString, FString>> GetPLYExtensionsAndDescriptions();

bool ReadPLY(const FString& Filename, FGaussianSplats& GaussianSplatData, int32& MaxSHDegree);

}  // namespace YaGS
