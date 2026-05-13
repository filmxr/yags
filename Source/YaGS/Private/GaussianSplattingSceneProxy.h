#pragma once

#include "GaussianSplattingFwd.h"

#include "DynamicMeshBuilder.h"
#include "Misc/AssertionMacros.h"
#include "PrimitiveSceneInfo.h"
#include "PrimitiveSceneInfoData.h"
#include "PrimitiveSceneProxy.h"

class FGaussianSplattingSceneProxy final : public FPrimitiveSceneProxy
{
public:
    FGaussianSplattingSceneProxy(UGaussianSplattingComponent* InComponent);

    bool IsVisible(const FSceneView& View) const;

private:
#if WITH_EDITOR
    UBodySetup* const BodySetup;
    FStaticMeshVertexBuffers ConvexHullVertexBuffers;
    FDynamicMeshIndexBuffer32 ConvexHullIndexBuffer;
    FLocalVertexFactory ConvexHullVertexFactory;
#endif

    void CreateRenderThreadResources(FRHICommandListBase& CmdList) override;
    void DestroyRenderThreadResources() override;
    uint32 GetMemoryFootprint() const override;
    SIZE_T GetTypeHash() const override;
#if WITH_EDITOR
    void GetDynamicMeshElements(
        const TArray<const FSceneView*>& Views,
        const FSceneViewFamily& ViewFamily,
        uint32 VisibilityMap,
        class FMeshElementCollector& Collector
    ) const override;
    FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
#endif
};
