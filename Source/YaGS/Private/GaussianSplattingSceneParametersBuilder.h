#pragma once

#include "GaussianSplattingFwd.h"
#include "GaussianSplattingShaders.h"

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"

namespace YaGS
{

void BuildSceneParameters(
    TConstArrayView<FGaussianSplattingSceneProxyData> SceneProxyData,
    FRDGBuilder& GraphBuilder,
    FGaussianSplattingSceneParameters& SceneParameters,
    uint32* SortKeyMaskBitCount,
    uint32* TotalNumElements,
    TRefCountPtr<FRDGPooledBuffer>* VolumesPooled = nullptr,
    TRefCountPtr<FRDGPooledBuffer>* InstancesPooled = nullptr
);

void UpdateSceneParameters(
    FGaussianSplattingSceneParameters& SceneParameters,
    FRDGBuilder& GraphBuilder,
    TRefCountPtr<FRDGPooledBuffer>& VolumesPooled,
    TRefCountPtr<FRDGPooledBuffer>& InstancesPooled
);

}  // namespace YaGS
