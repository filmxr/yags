#include "GaussianSplattingViewExtension.h"

#include "GaussianSplattingActor.h"
#include "GaussianSplattingAsset.h"
#include "GaussianSplattingCommon.h"
#include "GaussianSplattingComponent.h"
#include "GaussianSplattingDebugReadback.h"
#include "GaussianSplattingLog.h"
#include "GaussianSplattingQuadIndices.h"
#include "GaussianSplattingSceneParametersBuilder.h"
#include "GaussianSplattingSceneProxy.h"
#include "GaussianSplattingSettings.h"
#include "GaussianSplattingShaders.h"
#include "GaussianSplattingStaticBuffer.h"
#include "GaussianSplattingVolume.h"

#include "Algo/BinarySearch.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "FXRenderingUtils.h"
#include "GPUSort.h"
#include "Misc/Optional.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphEvent.h"
#include "RenderGraphUtils.h"
#include "Rendering/CustomRenderPass.h"
#include "RenderingThread.h"
#include "SystemTextures.h"

#include <type_traits>

namespace
{

constexpr EPixelFormat ColorRenderTargetPixelFormat = EPixelFormat::PF_FloatRGBA;
const FLinearColor ClearColor = FLinearColor::Transparent;

constexpr EPixelFormat DepthRenderTargetPixelFormat = EPixelFormat::PF_R32_FLOAT;
const FLinearColor ClearDepth{
    StaticCast<float>(ERHIZBuffer::FarPlane),
    StaticCast<float>(ERHIZBuffer::FarPlane),
    StaticCast<float>(ERHIZBuffer::FarPlane),
    FLinearColor::Transparent.A,
};

constexpr uint8 StencilRef = 0;  // Vis SceneDepthZ A*255

DECLARE_GPU_DRAWCALL_STAT_NAMED(YaGS_DrawPass, TEXT(UE_MODULE_NAME ".DrawPass"));
DECLARE_GPU_DRAWCALL_STAT_NAMED(YaGS_SceneBlendPass, TEXT(UE_MODULE_NAME ".SceneBlendPass"));

bool IsFeatureSupported(ERHIFeatureSupport FeatureSupport)
{
    if (FeatureSupport == ERHIFeatureSupport::RuntimeDependent)
    {
        return true;
    }
    if (FeatureSupport == ERHIFeatureSupport::RuntimeGuaranteed)
    {
        return true;
    }
    return false;
}

bool AreWaveOpsSupported(
    EShaderPlatform ShaderPlatform
)
{
    if (!IsFeatureSupported(FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(ShaderPlatform)))
    {
        return false;
    }
    if (!GRHISupportsWaveOperations)
    {
        return false;
    }
    return true;
}

FScreenTransform SvPositionToViewportUV(
    const FIntRect& SrcViewport
)  // FScreenTransform::SvPositionToViewportUV is
   // not exported currently
{
    return (FScreenTransform::Identity - SrcViewport.Min) / SrcViewport.Size();
}

}

void FGaussianSplattingViewExtension::AddComponent(
    const TWeakObjectPtr<UGaussianSplattingComponent>& Component
)
{
    check(IsInGameThread());
    Components.Add(Component);
}

void FGaussianSplattingViewExtension::RemoveComponent(
    const TWeakObjectPtr<UGaussianSplattingComponent>& Component
)
{
    check(IsInGameThread());
    Components.Remove(Component);
}

FScreenPassTexture FGaussianSplattingViewExtension::GetColorTexture(
    FRDGBuilder& GraphBuilder, const FIntPoint& Extent
)
{
    auto TextureDesc = FRDGTextureDesc::Create2D(
        /*Size*/ Extent,
        ColorRenderTargetPixelFormat,
        FClearValueBinding{ClearColor},
        /*Flags*/ TexCreate_ShaderResource | TexCreate_RenderTargetable
    );
    FRDGTextureRef GaussianSplatsTexture = GraphBuilder.CreateTexture(TextureDesc, TEXT(UE_MODULE_NAME ".Color"));
    return {
        GaussianSplatsTexture,
        GetRectFromExtent(Extent),
    };
}

FRDGTextureRef FGaussianSplattingViewExtension::GetDepthTexture(
    FRDGBuilder& GraphBuilder,
    ERHIFeatureLevel::Type FeatureLevel,
    FRDGTextureRef SceneDepthTexture,
    const FIntRect& ViewRect
)
{
    if (!UGaussianSplattingSettings::GetDepthWrite())
    {
        return nullptr;
    }
    FIntPoint Extent = ViewRect.Size();
    constexpr auto TextureCreateFlags = TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable;
    auto DepthTextureDesc =
        FRDGTextureDesc::Create2D(Extent, DepthRenderTargetPixelFormat, FClearValueBinding::None, TextureCreateFlags);
    auto DepthTexture = GraphBuilder.CreateTexture(DepthTextureDesc, TEXT(UE_MODULE_NAME ".Depth"));
    using FComputeShader = FGaussianSplattingDepthCopyCS;
    FComputeShader::FPermutationDomain PermutationVector;
    FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
    TShaderMapRef<FComputeShader> CopyDepthCS{ShaderMap, PermutationVector};
    auto* PassParameters = GraphBuilder.AllocParameters<FComputeShader::FParameters>();
    PassParameters->InputRectMinAndSize = ViewRect;
    PassParameters->InputDepth = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneDepthTexture));
    PassParameters->OutputDepth = GraphBuilder.CreateUAV(DepthTexture);
    FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(Extent, FComputeShader::GetGroupSize());
    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME(UE_MODULE_NAME ".CopyDepth"),
        ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
        CopyDepthCS,
        PassParameters,
        GroupCount
    );
    return DepthTexture;
}

template<bool bEnableDepthWrite>
FRHIDepthStencilState* FGaussianSplattingViewExtension::GetSceneBlendPassDepthStencilState()
{
    return TStaticDepthStencilState<
        bEnableDepthWrite,
        /*DepthTest*/ CF_Always,
        /*bEnableFrontFaceStencil*/ false,
        /*FrontFaceStencilTest*/ CF_Always,
        /*FrontFaceStencilFailStencilOp*/ SO_Keep,
        /*FrontFaceDepthFailStencilOp*/ SO_Keep,
        /*FrontFacePassStencilOp*/ SO_Keep,
        /*bEnableBackFaceStencil*/ false,
        /*BackFaceStencilTest*/ CF_Never,
        /*BackFaceStencilFailStencilOp*/ SO_Keep,
        /*BackFaceDepthFailStencilOp*/ SO_Keep,
        /*BackFacePassStencilOp*/ SO_Keep,
        /*StencilReadMask*/ 0,
        /*StencilWriteMask*/ 0>::GetRHI();
}

FRHIDepthStencilState* FGaussianSplattingViewExtension::GetSceneBlendPassDepthStencilState(
    bool bEnableDepthWrite
)
{
    if (bEnableDepthWrite)
    {
        return GetSceneBlendPassDepthStencilState</*bEnableDepthWrite*/ true>();
    }
    else
    {
        return GetSceneBlendPassDepthStencilState</*bEnableDepthWrite*/ false>();
    }
}

void FGaussianSplattingViewExtension::GenerateKeysAndIndices(
    FRDGBuilder& GraphBuilder,
    ERHIFeatureLevel::Type FeatureLevel,
    const FGaussianSplattingSceneParameters& SceneParameters,
    uint32 SortKeyMaskBitCount,
    uint32 TotalNumElements,
    const FMatrix& ViewMatrix,
    FRDGBufferUAVRef SortKeysUAV,
    FRDGBufferUAVRef IndicesUAV
)
{
    FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
    using FComputeShader = FGaussianSplattingPrepareSortKeyValuesCS;
    const int32 WorkGroupSizeLog2 = 10;
    const int32 WaveOpWaveSizeLog2 = 5;
    const int32 BatchSizeLog2 = FMath::Clamp(
        UGaussianSplattingSettings::GetSortBatchSizeLog2(),
        FComputeShader::FBatchSizeLog2::MinValue,
        FComputeShader::FBatchSizeLog2::MaxValue
    );
    FComputeShader::FPermutationDomain PermutationVector;
    PermutationVector.Set<FComputeShader::FWorkGroupSizeLog2>(WorkGroupSizeLog2);
    PermutationVector.Set<FComputeShader::FWaveOpWaveSizeLog2>(WaveOpWaveSizeLog2);
    PermutationVector.Set<FComputeShader::FBatchSizeLog2>(BatchSizeLog2);
    TShaderMapRef<FComputeShader> PrepareSortKeyValuesCS{ShaderMap, PermutationVector};
    auto* PassParameters = GraphBuilder.AllocParameters<FComputeShader::FParameters>();
    PassParameters->SceneParameters = SceneParameters;
    PassParameters->ViewMatrix.SetMatrixTranspose(ViewMatrix);
    PassParameters->Count = TotalNumElements;
    PassParameters->SortKeyMaskShift = 32 - SortKeyMaskBitCount;
    PassParameters->SortKeys = SortKeysUAV;
    PassParameters->ElementIndices = IndicesUAV;
    FIntVector GroupCount = FComputeShaderUtils::GetGroupCountWrapped(
        StaticCast<int32>(FMath::DivideAndRoundUp(TotalNumElements, 1u << BatchSizeLog2)), 1 << WorkGroupSizeLog2
    );
    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME(UE_MODULE_NAME ".PrepareSortKeyValues"),
        ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
        PrepareSortKeyValuesCS,
        PassParameters,
        GroupCount
    );
}

void FGaussianSplattingViewExtension::SortKeysAndIndices(
    FRDGBuilder& GraphBuilder,
    ERHIFeatureLevel::Type FeatureLevel,
    uint32 SortKeyMaskBitCount,
    uint32 TotalNumElements,
    FRDGBufferSRVRef SortKeysSRV,
    FRDGBufferSRVRef IndicesSRV,
    FRDGBufferUAVRef SortKeysUAV,
    FRDGBufferUAVRef IndicesUAV
)
{
    auto* PassParameters = GraphBuilder.AllocParameters<FGPUSortBuffersParameters>();
    PassParameters->RemoteKeySRVs[0] = SortKeysSRV;
    PassParameters->RemoteValueSRVs[0] = IndicesSRV;
    PassParameters->RemoteKeyUAVs[0] = SortKeysUAV;
    PassParameters->RemoteValueUAVs[0] = IndicesUAV;
    FRDGBufferRef SortKeys2 =
        GraphBuilder.CreateBuffer(SortKeysSRV->GetParent()->Desc, TEXT(UE_MODULE_NAME ".SortKeys2"));
    FRDGBufferRef Indices2 = GraphBuilder.CreateBuffer(IndicesSRV->GetParent()->Desc, TEXT(UE_MODULE_NAME ".Indices2"));
    PassParameters->RemoteKeySRVs[1] = GraphBuilder.CreateSRV(SortKeys2, PF_R32_UINT);
    PassParameters->RemoteValueSRVs[1] = GraphBuilder.CreateSRV(Indices2, PF_R32_UINT);
    PassParameters->RemoteKeyUAVs[1] = GraphBuilder.CreateUAV(SortKeys2, PF_R32_UINT);
    PassParameters->RemoteValueUAVs[1] = GraphBuilder.CreateUAV(Indices2, PF_R32_UINT);
    {
        constexpr uint32 ClearValue = 0;
        AddClearUAVPass(GraphBuilder, PassParameters->RemoteKeyUAVs[1], ClearValue);
        AddClearUAVPass(GraphBuilder, PassParameters->RemoteValueUAVs[1], ClearValue);
    }
    GraphBuilder.AddPass(
        RDG_EVENT_NAME(UE_MODULE_NAME ".Sort"),
        PassParameters,
        ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
        [PassParameters, SortKeyMaskBitCount, TotalNumElements, FeatureLevel](FRHICommandList& CmdList)
        {
            FGPUSortBuffers GPUSortBuffers;
            for (uint32 i = 0; i < 2; ++i)
            {
                GPUSortBuffers.RemoteKeySRVs[i] = PassParameters->RemoteKeySRVs[i]->GetRHI();
                GPUSortBuffers.RemoteKeyUAVs[i] = PassParameters->RemoteKeyUAVs[i]->GetRHI();
                GPUSortBuffers.RemoteValueSRVs[i] = PassParameters->RemoteValueSRVs[i]->GetRHI();
                GPUSortBuffers.RemoteValueUAVs[i] = PassParameters->RemoteValueUAVs[i]->GetRHI();
            }
            constexpr int32 InputBufferIndex = 0;
            const uint32 KeyMask = BitMask<uint32>(SortKeyMaskBitCount);
            const int32 ResultBufferIndex = SortGPUBuffers(
                CmdList, GPUSortBuffers, InputBufferIndex, KeyMask, StaticCast<int32>(TotalNumElements), FeatureLevel
            );
            // TODO(luxaeterna): use GetGPUSortPassCount in render thread instead
            if (InputBufferIndex != ResultBufferIndex)
            {
                const FRHITransitionInfo TransitionInfos[] = {
                    {  GPUSortBuffers.RemoteKeyUAVs[0],    ERHIAccess::Unknown, ERHIAccess::CopyDest},
                    {GPUSortBuffers.RemoteValueUAVs[0],    ERHIAccess::Unknown, ERHIAccess::CopyDest},
                    {  GPUSortBuffers.RemoteKeyUAVs[1], ERHIAccess::UAVCompute,  ERHIAccess::CopySrc},
                    {GPUSortBuffers.RemoteValueUAVs[1], ERHIAccess::UAVCompute,  ERHIAccess::CopySrc},
                };
                CmdList.Transition(TransitionInfos);
                {
                    auto Src = GPUSortBuffers.RemoteKeyUAVs[1]->GetBuffer();
                    auto Dest = GPUSortBuffers.RemoteKeyUAVs[0]->GetBuffer();
                    const uint32 Size = Src->GetSize();
                    check(Dest->GetSize() == Size);
                    CmdList.CopyBufferRegion(
                        Dest,
                        /*DstOffset*/ 0,
                        Src,
                        /*SrcOffset*/ 0,
                        Size
                    );
                }
                {
                    auto Src = GPUSortBuffers.RemoteValueUAVs[1]->GetBuffer();
                    auto Dest = GPUSortBuffers.RemoteValueUAVs[0]->GetBuffer();
                    const uint32 Size = Src->GetSize();
                    check(Dest->GetSize() == Size);
                    CmdList.CopyBufferRegion(
                        Dest,
                        /*DstOffset*/ 0,
                        Src,
                        /*SrcOffset*/ 0,
                        Size
                    );
                }
            }
        }
    );
}

template<typename FDrawPassParameters, typename FPixelShader>
static void AddPassMS(
    FRDGBuilder& GraphBuilder,
    FGlobalShaderMap* ShaderMap,
    TShaderMapRef<FPixelShader> PixelShader,
    FDrawPassParameters* PassParameters,
    FGraphicsPipelineStateInitializer&& GraphicsPSOInit,
    const FIntRect& ViewRect,
    bool bIsDepthPass,
    uint32 TotalNumElements
)
{
    TShaderMapRef<FGaussianSplattingAS> AmplificationShader{ShaderMap};
    ClearUnusedGraphResources(AmplificationShader, &PassParameters->AS);
    GraphicsPSOInit.BoundShaderState.SetAmplificationShader(AmplificationShader.GetAmplificationShader());
    FGaussianSplattingMS::FPermutationDomain PermutationVector;
    PermutationVector.Set<FGaussianSplattingMS::FIsDepthPass>(bIsDepthPass);
    TShaderMapRef<FGaussianSplattingMS> MeshShader{ShaderMap, PermutationVector};
    ClearUnusedGraphResources(MeshShader, &PassParameters->VS);
    GraphicsPSOInit.BoundShaderState.SetMeshShader(MeshShader.GetMeshShader());
    GraphBuilder.AddPass(
        RDG_EVENT_NAME(UE_MODULE_NAME ".DrawMS"),
        PassParameters,
        ERDGPassFlags::Raster,
        [PassParameters,
         ViewRect,
         AmplificationShader,
         MeshShader,
         PixelShader,
         GraphicsPSOInit = MoveTemp(GraphicsPSOInit)](FRHICommandList& CmdList) mutable
        {
            CmdList.SetViewport(
                StaticCast<float>(ViewRect.Min.X),
                StaticCast<float>(ViewRect.Min.Y),
                0.0f,
                StaticCast<float>(ViewRect.Max.X),
                StaticCast<float>(ViewRect.Max.Y),
                1.0f
            );
            CmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
            SetGraphicsPipelineState(CmdList, GraphicsPSOInit, StencilRef);
            SetShaderParameters(
                CmdList, AmplificationShader, AmplificationShader.GetAmplificationShader(), PassParameters->AS
            );
            SetShaderParameters(CmdList, MeshShader, MeshShader.GetMeshShader(), PassParameters->VS);
            SetShaderParameters(CmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);
            CmdList.DispatchMeshShader(1, 1, 1);
        }
    );
}

template<typename FDrawPassParameters, typename FPixelShader>
static void AddPassVS(
    FRDGBuilder& GraphBuilder,
    FGlobalShaderMap* ShaderMap,
    TShaderMapRef<FPixelShader> PixelShader,
    FDrawPassParameters* PassParameters,
    FGraphicsPipelineStateInitializer&& GraphicsPSOInit,
    const FIntRect& ViewRect,
    bool bIsDepthPass,
    uint32 TotalNumElements
)
{
    FGaussianSplattingVS::FPermutationDomain PermutationVector;
    PermutationVector.Set<FGaussianSplattingVS::FIsDepthPass>(bIsDepthPass);
    TShaderMapRef<FGaussianSplattingVS> VertexShader{ShaderMap, PermutationVector};
    ClearUnusedGraphResources(VertexShader, &PassParameters->VS);
    GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
    GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
    GraphBuilder.AddPass(
        RDG_EVENT_NAME(UE_MODULE_NAME ".DrawVS"),
        PassParameters,
        ERDGPassFlags::Raster,
        [PassParameters,
         ViewRect,
         VertexShader,
         PixelShader,
         GraphicsPSOInit = MoveTemp(GraphicsPSOInit),
         TotalNumElements](FRHICommandList& CmdList) mutable
        {
            CmdList.SetViewport(
                StaticCast<float>(ViewRect.Min.X),
                StaticCast<float>(ViewRect.Min.Y),
                0.0f,
                StaticCast<float>(ViewRect.Max.X),
                StaticCast<float>(ViewRect.Max.Y),
                1.0f
            );
            CmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
            SetGraphicsPipelineState(CmdList, GraphicsPSOInit, StencilRef);
            SetShaderParameters(CmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
            SetShaderParameters(CmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);
            CmdList.DrawIndexedPrimitive(
                YaGS::GQuadIndices.GetRHI(),
                /*BaseVertexIndex*/ 0,
                /*FirstInstance*/ 0,
                /*NumVertices*/ YaGS::GVertexCountPerQuad,
                /*StartIndex*/ 0,
                /*NumPrimitives*/ YaGS::GTriangleCountPerQuad,
                /*NumInstances*/ TotalNumElements
            );
        }
    );
}

static bool GetUseMeshShader(
    EShaderPlatform ShaderPlatform
)
{
    if (!UGaussianSplattingSettings::GetPreferMeshShader())
    {
        return false;
    }
    if (!GRHISupportsMeshShadersTier0 || !AreWaveOpsSupported(ShaderPlatform))
    {
        return false;
    }
    return true;
}

template<typename FPixelShader, typename FDrawPassParameters>
static void AddDrawPassPS(
    FRDGBuilder& GraphBuilder,
    EShaderPlatform ShaderPlatform,
    FGlobalShaderMap* ShaderMap,
    FRDGTextureRef SceneDepthTexture,
    const FScreenPassTexture& ColorTexture,
    FRDGTextureRef DepthTexture,
    FRDGBufferSRVRef SortKeysSRV,
    FRDGBufferSRVRef IndicesSRV,
    const FGaussianSplattingSceneParameters& SceneParameters,
    uint32 SortKeyMaskBitCount,
    uint32 TotalNumElements,
    const FMatrix& ViewMatrix,
    const FMatrix& ProjectionMatrix,
    bool AreRenderTargetsInitialized
)
{
    constexpr bool bIsColorPass = std::is_same_v<FPixelShader, FGaussianSplattingColorPS>;
    constexpr bool bIsDepthPass = std::is_same_v<FPixelShader, FGaussianSplattingDepthPS>;
    FGraphicsPipelineStateInitializer GraphicsPSOInit;
    GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
    auto* PassParameters = GraphBuilder.AllocParameters<FDrawPassParameters>();
    typename FPixelShader::FPermutationDomain PermutationVector;
    if constexpr (bIsColorPass)
    {
        const auto RenderTargetLoadAction =
            AreRenderTargetsInitialized ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::EClear;
        PassParameters->PS.RenderTargets.DepthStencil =
            FDepthStencilBinding{/*Texture*/ SceneDepthTexture,
                                 /*DepthLoadAction*/ ERenderTargetLoadAction::ELoad,
                                 /*StencilLoadAction*/ RenderTargetLoadAction,
                                 /*DepthStencilAccess*/
                                 FExclusiveDepthStencil::DepthRead_StencilWrite};
        PassParameters->PS.RenderTargets[0] = FRenderTargetBinding{
            ColorTexture.Texture,
            RenderTargetLoadAction,
        };
        const bool bShowDebugQuadBorder = UGaussianSplattingSettings::GetShowDebugQuadBorder();
        PermutationVector.Set<typename FPixelShader::FShowDebugQuadBorder>(bShowDebugQuadBorder);
        GraphicsPSOInit.BlendState = TStaticBlendState<
            /*RT0ColorWriteMask*/ CW_RGBA,
            /*RT0ColorBlendOp*/ BO_Add,
            /*RT0ColorSrcBlend*/ BF_InverseDestAlpha,
            /*RT0ColorDestBlend*/ BF_One,
            /*RT0AlphaBlendOp*/ BO_Add,
            /*RT0AlphaSrcBlend*/ BF_InverseDestAlpha,
            /*RT0AlphaDestBlend*/ BF_One>::GetRHI();
        GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
            /*bEnableDepthWrite*/ false,
            /*DepthTest*/ CF_DepthNearOrEqual,
            /*bEnableFrontFaceStencil*/ true,
            /*FrontFaceStencilTest*/ CF_Always,
            /*FrontFaceStencilFailStencilOp*/ SO_Keep,
            /*FrontFaceDepthFailStencilOp*/ SO_Keep,
            /*FrontFacePassStencilOp*/ SO_Replace>::GetRHI();
    }
    else if constexpr (bIsDepthPass)
    {
        PassParameters->PS.RenderTargets.DepthStencil =
            FDepthStencilBinding{/*Texture*/ SceneDepthTexture,
                                 /*DepthLoadAction*/ ERenderTargetLoadAction::ELoad,
                                 /*StencilLoadAction*/ ERenderTargetLoadAction::ELoad,
                                 /*DepthStencilAccess*/ FExclusiveDepthStencil::DepthRead_StencilRead};
        const float AlphaDepthClip = UGaussianSplattingSettings::GetAlphaDepthClip();
        PassParameters->PS.AlphaDepthClip = AlphaDepthClip;
        PassParameters->PS.RenderTargets[0] = FRenderTargetBinding{
            DepthTexture,
            ERenderTargetLoadAction::ELoad,
        };
        const bool bEnableAlphaDepthClip = AlphaDepthClip > 0.0f;
        PermutationVector.Set<typename FPixelShader::FEnableAlphaDepthClip>(bEnableAlphaDepthClip);
        GraphicsPSOInit.BlendState = TStaticBlendState<
            /*RT0ColorWriteMask*/ CW_RED,
            /*RT0ColorBlendOp*/ BO_Add,
            /*RT0ColorSrcBlend*/ BF_SourceAlpha,
            /*RT0ColorDestBlend*/ BF_InverseSourceAlpha>::GetRHI();
        GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
            /*bEnableDepthWrite*/ false,
            /*DepthTest*/ CF_DepthNearOrEqual,
            /*bEnableFrontFaceStencil*/ true,
            /*FrontFaceStencilTest*/ CF_Equal>::GetRHI();
    }
    else
    {
        static_assert(sizeof(FPixelShader) == 0);
    }
    verify(IsFeatureSupported(FDataDrivenShaderPlatformInfo::GetSupportsBarycentricsSemantic(ShaderPlatform)));
    TShaderMapRef<FPixelShader> PixelShader{ShaderMap, PermutationVector};
    ClearUnusedGraphResources(PixelShader, &PassParameters->PS);
    GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
    GraphicsPSOInit.PrimitiveType = PT_TriangleList;
    {
        auto& VS = PassParameters->VS;
        VS.SceneParameters = SceneParameters;
        VS.ViewMatrix.SetMatrixTranspose(ViewMatrix);
        VS.ProjectionMatrix = FMatrix44f{ProjectionMatrix};
        VS.Count = TotalNumElements;
        VS.ElementIndices = IndicesSRV;
    }
    if (GetUseMeshShader(ShaderPlatform))
    {
        {
            auto& AS = PassParameters->AS;
            AS.Count = TotalNumElements;
            AS.SortKeyMaskShift = 32 - SortKeyMaskBitCount;
            AS.SortKeys = SortKeysSRV;
        }
        AddPassMS(
            GraphBuilder,
            ShaderMap,
            PixelShader,
            PassParameters,
            MoveTemp(GraphicsPSOInit),
            ColorTexture.ViewRect,
            bIsDepthPass,
            TotalNumElements
        );
    }
    else
    {
        AddPassVS(
            GraphBuilder,
            ShaderMap,
            PixelShader,
            PassParameters,
            MoveTemp(GraphicsPSOInit),
            ColorTexture.ViewRect,
            bIsDepthPass,
            TotalNumElements
        );
    }
}

void FGaussianSplattingViewExtension::AddDrawPass(
    FRDGBuilder& GraphBuilder,
    ERHIFeatureLevel::Type FeatureLevel,
    bool AreRenderTargetsInitialized,
    FRDGTextureRef SceneDepthTexture,
    const FScreenPassTexture& ColorTexture,
    FRDGTextureRef DepthTexture,
    const FGaussianSplattingSceneParameters& SceneParameters,
    uint32 SortKeyMaskBitCount,
    uint32 TotalNumElements,
    const FMatrix& ViewMatrix,
    const FMatrix& ProjectionMatrix
)
{
    check(TotalNumElements > 0);
    RDG_EVENT_SCOPE_STAT(GraphBuilder, YaGS_DrawPass, UE_MODULE_NAME ".DrawPass");
    RDG_GPU_STAT_SCOPE(GraphBuilder, YaGS_DrawPass);
    // DebugPrintBuffer(GraphBuilder, SceneParameters.Volumes->GetParent());
    auto SortKeysDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TotalNumElements);
    FRDGBufferRef SortKeys = GraphBuilder.CreateBuffer(SortKeysDesc, TEXT(UE_MODULE_NAME ".SortKeys"));
    FRDGBufferSRVRef SortKeysSRV = GraphBuilder.CreateSRV(SortKeys, PF_R32_UINT);
    auto IndicesDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TotalNumElements);
    FRDGBufferRef Indices = GraphBuilder.CreateBuffer(IndicesDesc, TEXT(UE_MODULE_NAME ".Indices"));
    FRDGBufferSRVRef IndicesSRV = GraphBuilder.CreateSRV(Indices, PF_R32_UINT);
    {
        FRDGBufferUAVRef SortKeysUAV = GraphBuilder.CreateUAV(SortKeys, PF_R32_UINT);
        FRDGBufferUAVRef IndicesUAV = GraphBuilder.CreateUAV(Indices, PF_R32_UINT);
        GenerateKeysAndIndices(
            GraphBuilder,
            FeatureLevel,
            SceneParameters,
            SortKeyMaskBitCount,
            TotalNumElements,
            ViewMatrix,
            SortKeysUAV,
            IndicesUAV
        );
        SortKeysAndIndices(
            GraphBuilder,
            FeatureLevel,
            SortKeyMaskBitCount,
            TotalNumElements,
            SortKeysSRV,
            IndicesSRV,
            SortKeysUAV,
            IndicesUAV
        );
    }
    const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];
    FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
    AddDrawPassPS<FGaussianSplattingColorPS, FColorPassParameters>(
        GraphBuilder,
        ShaderPlatform,
        ShaderMap,
        SceneDepthTexture,
        ColorTexture,
        DepthTexture,
        SortKeysSRV,
        IndicesSRV,
        SceneParameters,
        SortKeyMaskBitCount,
        TotalNumElements,
        ViewMatrix,
        ProjectionMatrix,
        AreRenderTargetsInitialized
    );
    const bool bEnableDepthWrite = (DepthTexture != nullptr);
    if (bEnableDepthWrite)
    {
        AddDrawPassPS<FGaussianSplattingDepthPS, FDepthPassParameters>(
            GraphBuilder,
            ShaderPlatform,
            ShaderMap,
            SceneDepthTexture,
            ColorTexture,
            DepthTexture,
            SortKeysSRV,
            IndicesSRV,
            SceneParameters,
            SortKeyMaskBitCount,
            TotalNumElements,
            ViewMatrix,
            ProjectionMatrix,
            AreRenderTargetsInitialized
        );
    }
}

void FGaussianSplattingViewExtension::AddSceneBlendPass(
    FRDGBuilder& GraphBuilder,
    ERHIFeatureLevel::Type FeatureLevel,
    const FScreenPassTexture& InputColor,
    FRDGTextureRef DepthTexture,
    const FScreenPassRenderTarget& SceneColorRenderTarget,
    FRDGTextureRef SceneDepthTexture
)
{
    RDG_EVENT_SCOPE_STAT(GraphBuilder, YaGS_SceneBlendPass, UE_MODULE_NAME ".SceneBlendPass");
    RDG_GPU_STAT_SCOPE(GraphBuilder, YaGS_SceneBlendPass);
    FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
    using FPixelShader = FGaussianSplattingSceneBlendPS;
    const bool bDepthWrite = (DepthTexture != nullptr);
    FPixelShader::FPermutationDomain PermutationVector;
    PermutationVector.Set<FPixelShader::FDepthWrite>(bDepthWrite);
    TShaderMapRef<FPixelShader> SceneBlendPixelShader{ShaderMap, PermutationVector};
    FIntRect ViewRect = SceneColorRenderTarget.ViewRect;
    auto* PassParameters = GraphBuilder.AllocParameters<FPixelShader::FParameters>();
    PassParameters->InputScreenTransform = SvPositionToViewportUV(ViewRect);
    PassParameters->InputColor = InputColor.Texture;
    if (bDepthWrite)
    {
        PassParameters->InputDepth = DepthTexture;
    }
    PassParameters->Sampler = TStaticSamplerState<>::GetRHI();
    PassParameters->RenderTargets[0] = SceneColorRenderTarget.GetRenderTargetBinding();
    ERenderTargetLoadAction DepthLoadAction;
    auto DepthStencilAccess = FExclusiveDepthStencil::StencilRead;
    if (bDepthWrite)
    {
        DepthLoadAction = ERenderTargetLoadAction::ELoad;
        EnumAddFlags(DepthStencilAccess, FExclusiveDepthStencil::DepthWrite);
    }
    else
    {
        DepthLoadAction = ERenderTargetLoadAction::ENoAction;
        EnumAddFlags(DepthStencilAccess, FExclusiveDepthStencil::DepthNop);
    }
    PassParameters->RenderTargets.DepthStencil =
        FDepthStencilBinding{/*Texture*/ SceneDepthTexture,
                             /*DepthLoadAction*/ DepthLoadAction,
                             /*StencilLoadAction*/ ERenderTargetLoadAction::ELoad,
                             /*DepthStencilAccess*/ DepthStencilAccess};
    auto BlendState = TStaticBlendState<
        /*RT0ColorWriteMask*/ CW_RGB,
        /*RT0ColorBlendOp*/ BO_Add,
        /*RT0ColorSrcBlend*/ BF_SourceAlpha,
        /*RT0ColorDestBlend*/ BF_InverseSourceAlpha>::GetRHI();
    auto DepthStencilState = GetSceneBlendPassDepthStencilState(bDepthWrite);
    FPixelShaderUtils::AddFullscreenPass(
        GraphBuilder,
        ShaderMap,
        RDG_EVENT_NAME(UE_MODULE_NAME ".SceneBlend"),
        SceneBlendPixelShader,
        PassParameters,
        ViewRect,
        BlendState,
        /*RasterizerState*/ nullptr,
        DepthStencilState,
        /*StencilRef*/ 0
    );
}

bool FGaussianSplattingViewExtension::IsActiveThisFrame_Internal(
    const FSceneViewExtensionContext& Context
) const
{
    check(IsInGameThread());
    if (!FSceneViewExtensionBase::IsActiveThisFrame_Internal(Context))
    {
        return false;
    }
    if (Components.IsEmpty())
    {
        return false;
    }
    return true;
}

void FGaussianSplattingViewExtension::SetupViewFamily(
    FSceneViewFamily& InViewFamily
)
{
    ViewSceneProxyData.Reset();
}

void FGaussianSplattingViewExtension::SetupView(
    FSceneViewFamily& InViewFamily, FSceneView& InView
)
{
    check(IsInGameThread());
    TArray<FGaussianSplattingSceneProxyData>& SceneProxyData = ViewSceneProxyData.AddDefaulted_GetRef();
    const auto IsPrimitiveShown = [&InView,
                                   ShowOnlyPrimitives = InView.ShowOnlyPrimitives.GetPtrOrNull(),
                                   &HiddenPrimitives = InView.HiddenPrimitives](const UPrimitiveComponent* Component)
    {
        {
            const bool bIsInScene = Component->GetScene() == InView.Family->Scene;
            const auto EngineShowFlags = InView.Family->EngineShowFlags;
            const bool bIsVisible = Component->IsShown(EngineShowFlags) && bIsInScene;
            if (!bIsVisible)
            {
                return false;
            }
#if WITH_EDITOR
            const bool bIsWireframe = EngineShowFlags.Wireframe;
            const bool bIsCollision =
                EngineShowFlags.Collision || EngineShowFlags.CollisionPawn || EngineShowFlags.CollisionVisibility;
            if (bIsWireframe || bIsCollision)
            {
                return false;
            }
#endif
        }
        auto OwnerActor = Cast<AGaussianSplattingActor>(Component->GetOwner());
        if (!OwnerActor)
        {
            return false;
        }
        if (OwnerActor->IsHidden())
        {
            return false;
        }
#if WITH_EDITOR
        if (OwnerActor->IsHiddenEd())
        {
            return false;
        }
#endif
        auto PrimitiveSceneId = Component->GetPrimitiveSceneId();
        if (ShowOnlyPrimitives != nullptr)
        {
            if (!ShowOnlyPrimitives->Contains(PrimitiveSceneId))
            {
                return false;
            }
        }
        else if (HiddenPrimitives.Contains(PrimitiveSceneId))
        {
            return false;
        }
        return true;
    };
    TArray<TWeakObjectPtr<UGaussianSplattingComponent>> DeadComponents;
    for (const TWeakObjectPtr<UGaussianSplattingComponent>& ComponentWeakPtr : Components)
    {
        if (auto Component = ComponentWeakPtr.Pin())
        {
            if (!IsPrimitiveShown(Component.Get()))
            {
                continue;
            }
            SceneProxyData.Add(Component->ToSceneProxyData());
        }
        else
        {
            UE_LOG(LogYaGS, Warning, TEXT("Cannot pin Component weak pointer"));
            DeadComponents.Add(ComponentWeakPtr);
        }
    }
    for (const TWeakObjectPtr<UGaussianSplattingComponent>& Dead : DeadComponents)
    {
        Components.Remove(Dead);
    }
    SceneProxyData.StableSort();
}

void FGaussianSplattingViewExtension::PrePostProcessPass_RenderThread(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessingInputs& Inputs
)
{
    FRDGTextureRef SceneDepthTexture = (*Inputs.SceneTextures)->SceneDepthTexture;
    if (SceneDepthTexture == GSystemTextures.GetBlackDummy(GraphBuilder))
    {
        return;
    }
    ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();
    bool AreRenderTargetsInitialized = false;
    FIntRect ViewRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(View);
    FScreenPassTexture ColorTexture = GetColorTexture(GraphBuilder, ViewRect.Size());
    FRDGTextureRef DepthTexture = GetDepthTexture(GraphBuilder, FeatureLevel, SceneDepthTexture, ViewRect);
    {
        const int32 ViewIndex = View.Family->Views.IndexOfByKey(&View);
        if (!ViewSceneProxyData.IsValidIndex(ViewIndex))
        {
            return;
        }
        const TArray<FGaussianSplattingSceneProxyData>& SceneProxyData = ViewSceneProxyData[ViewIndex];
        check(Algo::IsSorted(SceneProxyData));
        auto SceneProxyDataView = MakeConstArrayView(SceneProxyData);
        while (!SceneProxyDataView.IsEmpty())
        {
            const int32 Count = Algo::UpperBoundBy(
                SceneProxyDataView,
                SceneProxyDataView[0].RenderingOrder,
                &FGaussianSplattingSceneProxyData::RenderingOrder
            );
            check(Count > 0);
            FGaussianSplattingSceneParameters SceneParameters;
            uint32 SortKeyMaskBitCount;
            uint32 TotalNumElements;
            YaGS::BuildSceneParameters(
                SceneProxyDataView.Left(Count), GraphBuilder, SceneParameters, &SortKeyMaskBitCount, &TotalNumElements
            );
            AddDrawPass(
                GraphBuilder,
                FeatureLevel,
                AreRenderTargetsInitialized,
                SceneDepthTexture,
                ColorTexture,
                DepthTexture,
                SceneParameters,
                SortKeyMaskBitCount,
                TotalNumElements,
                View.ViewMatrices.GetViewMatrix(),
                View.ViewMatrices.GetProjectionMatrix()
            );
            AreRenderTargetsInitialized = true;
            SceneProxyDataView.RightChopInline(Count);
        }
    }
    if (AreRenderTargetsInitialized)
    {
        FScreenPassTexture SceneColor{
            (*Inputs.SceneTextures)->SceneColorTexture,
            ViewRect,
        };
        FScreenPassRenderTarget SceneColorRenderTarget{
            SceneColor,
            ERenderTargetLoadAction::ELoad,
        };
        AddSceneBlendPass(
            GraphBuilder, FeatureLevel, ColorTexture, DepthTexture, SceneColorRenderTarget, SceneDepthTexture
        );
    }
}
