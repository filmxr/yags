#pragma once

#include "CoreMinimal.h"

class FGaussianSplattingStaticBuffer;
class UGaussianSplattingAsset;
class AGaussianSplattingActor;
struct FGaussianSplattingSceneProxyData;
class FGaussianSplattingSceneProxy;
class UGaussianSplattingComponent;
class AGaussianSplattingBooleanVolume;
class AGaussianSplattingAppearanceVolume;
struct FGaussianSplattingAppearance;
template<typename AllocatorType = FDefaultAllocator>
class TGaussianSplattingByteAddressBufferView;
class FGaussianSplattingSceneParameters;
class FGaussianSplattingViewExtension;
class FGaussianSplattingEditingActionsBase;

using FGaussianSplattingActorWeakPtrs = TArray<TWeakObjectPtr<AGaussianSplattingActor>>;
using FGaussianSplattingActorStrongPtrs = TArray<TStrongObjectPtr<AGaussianSplattingActor>>;
