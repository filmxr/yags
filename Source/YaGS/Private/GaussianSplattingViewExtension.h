#pragma once

#include "GaussianSplattingFwd.h"
#include "GaussianSplattingSceneProxyData.h"

#include "GaussianSplat.h"
#include "RHIFeatureLevel.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RendererInterface.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"

class FGaussianSplattingViewExtension final : public FWorldSceneViewExtension
{
public:
    using FWorldSceneViewExtension::FWorldSceneViewExtension;

    void AddComponent(const TWeakObjectPtr<UGaussianSplattingComponent>& Component);
    void RemoveComponent(const TWeakObjectPtr<UGaussianSplattingComponent>& Component);

private:
    using FSceneProxyDataGroups = TArray<TArray<FGaussianSplattingSceneProxyData>>;

    // Used on game thread
    TSet<TWeakObjectPtr<UGaussianSplattingComponent>> Components;

    // Used on render thread and game thread non-concurrently
    TArray<TArray<FGaussianSplattingSceneProxyData>> ViewSceneProxyData;

    static FScreenPassTexture GetColorTexture(FRDGBuilder& GraphBuilder, const FIntPoint& Extent);

    static FRDGTextureRef GetDepthTexture(
        FRDGBuilder& GraphBuilder,
        ERHIFeatureLevel::Type FeatureLevel,
        FRDGTextureRef SceneDepthTexture,
        const FIntRect& ViewRect
    );

    template<bool bEnableDepthWrite>
    static FRHIDepthStencilState* GetSceneBlendPassDepthStencilState();

    static FRHIDepthStencilState* GetSceneBlendPassDepthStencilState(bool bEnableDepthWrite);

    void GenerateKeysAndIndices(
        FRDGBuilder& GraphBuilder,
        ERHIFeatureLevel::Type FeatureLevel,
        const FGaussianSplattingSceneParameters& SceneParameters,
        uint32 SortKeyMaskBitCount,
        uint32 TotalNumElements,
        const FMatrix& ViewMatrix,
        FRDGBufferUAVRef SortKeysUAV,
        FRDGBufferUAVRef IndicesUAV
    );

    void SortKeysAndIndices(
        FRDGBuilder& GraphBuilder,
        ERHIFeatureLevel::Type FeatureLevel,
        uint32 SortKeyMaskBitCount,
        uint32 TotalNumElements,
        FRDGBufferSRVRef SortKeysSRV,
        FRDGBufferSRVRef IndicesSRV,
        FRDGBufferUAVRef SortKeysUAV,
        FRDGBufferUAVRef IndicesUAV
    );

    void AddDrawPass(
        FRDGBuilder& GraphBuilder,
        ERHIFeatureLevel::Type FeatureLevel,
        bool IsFirstSceneProxyGroup,
        FRDGTextureRef SceneDepthTexture,
        const FScreenPassTexture& ColorTexture,
        FRDGTextureRef DepthTexture,
        const FGaussianSplattingSceneParameters& SceneParameters,
        uint32 SortKeyMaskBitCount,
        uint32 TotalNumElements,
        const FMatrix& ViewMatrix,
        const FMatrix& ProjectionMatrix
    );

    void AddSceneBlendPass(
        FRDGBuilder& GraphBuilder,
        ERHIFeatureLevel::Type FeatureLevel,
        const FScreenPassTexture& ColorTexture,
        FRDGTextureRef DepthTexture,
        const FScreenPassRenderTarget& SceneColorRenderTarget,
        FRDGTextureRef SceneDepthTexture
    );

    bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

    void SetupViewFamily(FSceneViewFamily& ViewFamily) override;
    void SetupView(FSceneViewFamily& ViewFamily, FSceneView& View) override;

    void PrePostProcessPass_RenderThread(
        FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs
    ) override;
};
