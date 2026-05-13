#include "GaussianSplattingEditingActions.h"

#include "GaussianSplattingActor.h"
#include "GaussianSplattingAsset.h"
#include "GaussianSplattingComponent.h"
#include "GaussianSplattingDebugReadback.h"
#include "GaussianSplattingLog.h"
#include "GaussianSplattingSceneParametersBuilder.h"
#include "GaussianSplattingSceneProxy.h"
#include "GaussianSplattingSceneProxyData.h"
#include "GaussianSplattingShaders.h"
#include "GaussianSplattingStaticBuffer.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "RHI.h"
#include "RHIDefinitions.h"
#include "RHIGPUReadback.h"
#include "RHIGlobals.h"
#include "RHIResources.h"
#include "RHIUtilities.h"
#include "RenderCommandFence.h"
#include "RenderGraph.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "Tasks/Pipe.h"
#include "Tasks/Task.h"
#include "Templates/PointerIsConvertibleFromTo.h"

#include <cinttypes>

namespace
{

ERDGPassFlags GetPassFlagsCompute()
{
    return GSupportsEfficientAsyncCompute ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute;
}

template<typename T>
auto ConvertToStrongPtrs(const TArray<TWeakObjectPtr<T>>& WeakPtrs)
{
    TArray<TStrongObjectPtr<T>> StrongPtrs;
    StrongPtrs.Reserve(WeakPtrs.Num());
    for (const auto& WeakPtr : WeakPtrs)
    {
        if (auto StrongPtr = WeakPtr.Pin())
        {
            StrongPtrs.Push(MoveTemp(StrongPtr));
        }
    }
    return StrongPtrs;
}

TArray<FGaussianSplattingSceneProxyData> GetSceneProxyData(
    const FGaussianSplattingActorStrongPtrs& Actors
)
{
    check(IsInGameThread());
    TArray<FGaussianSplattingSceneProxyData> SceneProxyData;
    for (const auto& Actor : Actors)
    {
        check(Actor);
        SceneProxyData.Add(Actor->GaussianSplattingComponent->ToSceneProxyData());
    }
    return SceneProxyData;
}

class FGaussianSplattingEditingActionsBaseImpl
{
public:
    FGaussianSplattingEditingActionsBaseImpl(
        TArray<FGaussianSplattingSceneProxyData>&& SceneProxyData,
        FGaussianSplattingStaticBuffer& OutStaticBuffer,
        FVector& OutSelectionOrigin
    )
        : SceneProxyData{MoveTemp(SceneProxyData)}
        , StaticBuffer{OutStaticBuffer}
        , SelectionOrigin{OutSelectionOrigin}
    {
    }

    ~FGaussianSplattingEditingActionsBaseImpl()
    {
        check(!VolumesPooled);
        check(!InstancesPooled);
        check(!BitmasksPooled);
        check(!PrefixSumsPooled);
        check(!ElementIndicesPooled);
        check(!ReduceResultReadback);
        check(!FilterInitFence);
        check(ModelsPooled.IsEmpty());
        check(!FilterFinishFence);
    }

    void Fuse_RenderThread()
    {
        check(IsInRenderingThread());
        FilterInit(GetImmediateCommandList_ForRenderCommand());
    }

private:
    static constexpr double InitialDelayMs = 1.0f;
    static constexpr double ExponentialBackoff = 1.5f;

    const ERDGPassFlags PassFlagsCompute = GetPassFlagsCompute();
    const TArray<FGaussianSplattingSceneProxyData> SceneProxyData;
    FGaussianSplattingStaticBuffer& StaticBuffer;
    FVector& SelectionOrigin;

    TRefCountPtr<FRDGPooledBuffer> VolumesPooled;
    TRefCountPtr<FRDGPooledBuffer> InstancesPooled;
    FGaussianSplattingSceneParameters SceneParameters;
    uint32 TotalNumElements = 0;
    TRefCountPtr<FRDGPooledBuffer> BitmasksPooled;
    TRefCountPtr<FRDGPooledBuffer> PrefixSumsPooled;
    TRefCountPtr<FRDGPooledBuffer> ElementIndicesPooled;
    TUniquePtr<FRHIGPUBufferReadback> ReduceResultReadback;
    FGPUFenceRHIRef FilterInitFence;
    uint32 NumElementsOut = TNumericLimits<uint32>::Max();
    TArray<TRefCountPtr<FRDGPooledBuffer>> ModelsPooled;
    FBox SelectionBox;
    FGPUFenceRHIRef FilterFinishFence;

    void FilterInit(
        FRHICommandListImmediate& CmdList
    )
    {
        for (const FGaussianSplattingSceneProxyData& SceneProxyDatum : SceneProxyData)
        {
            SelectionBox += SceneProxyDatum.BoundingBox;
        }
        auto GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
        FRDGBuilder GraphBuilder{CmdList, RDG_EVENT_NAME(UE_MODULE_NAME ".FilterInit"), ERDGBuilderFlags::Parallel};
        YaGS::BuildSceneParameters(
            SceneProxyData,
            GraphBuilder,
            SceneParameters,
            /*SortKeyMaskBitCount*/ nullptr,
            &TotalNumElements,
            &VolumesPooled,
            &InstancesPooled
        );
        UE_LOG(LogYaGS, Verbose, TEXT("Filter started. TotalNumElements: %u"), TotalNumElements);
        auto ElementIndicesDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TotalNumElements);
        auto ElementIndices = GraphBuilder.CreateBuffer(ElementIndicesDesc, TEXT(UE_MODULE_NAME ".ElementIndices"));
        auto BitmasksDesc = FRDGBufferDesc::CreateStructuredDesc(
            sizeof(FUintVector4), FMath::DivideAndRoundUp(TotalNumElements, 1u << YaGS::WaveOpBitmaskBitsizeLog2)
        );
        auto Bitmasks = GraphBuilder.CreateBuffer(BitmasksDesc, TEXT(UE_MODULE_NAME ".Bitmasks"));
        FRDGBufferRef Sums;
        const int32 MaxCompleteTreeHeight =
            FMath::DivideAndRoundUp(FMath::CeilLogTwo(TotalNumElements), FMath::FloorLog2(GRHIGlobals.MinimumWaveSize));
        TArray<FRDGBufferSRVRef> SumsSRVs;
        SumsSRVs.Reserve(MaxCompleteTreeHeight);
        const auto AddSums =
            [&]
            (uint32 SumCount)
            {
                auto SumsDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), SumCount);
                Sums = GraphBuilder.CreateBuffer(
                    SumsDesc, *FString::Printf(TEXT(UE_MODULE_NAME ".Sums_%i"), SumsSRVs.Num())
                );
                SumsSRVs.Add(GraphBuilder.CreateSRV(Sums, PF_R32_UINT));
            };
        AddSums(BitmasksDesc.NumElements);
        {
            using FComputeShader = FGaussianSplattingFilterInitCS;
            const int32 WorkGroupSizeLog2 = 10;
            const int32 WaveOpWaveSizeLog2 = 5;
            const int32 BatchSizeLog2 = 0;
            FComputeShader::FPermutationDomain PermutationVector;
            PermutationVector.Set<FComputeShader::FWorkGroupSizeLog2>(WorkGroupSizeLog2);
            PermutationVector.Set<FComputeShader::FWaveOpWaveSizeLog2>(WaveOpWaveSizeLog2);
            PermutationVector.Set<FComputeShader::FBatchSizeLog2>(BatchSizeLog2);
            TShaderMapRef<FComputeShader> FilterInitCS{GlobalShaderMap, PermutationVector};
            auto* PassParameters = GraphBuilder.AllocParameters<FComputeShader::FParameters>();
            PassParameters->SceneParameters = SceneParameters;
            PassParameters->Count = TotalNumElements;
            PassParameters->ElementIndicesOut = GraphBuilder.CreateUAV(ElementIndices, PF_R32_UINT);
            PassParameters->BitmasksOut = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Bitmasks));
            PassParameters->SumsOut = GraphBuilder.CreateUAV(Sums, PF_R32_UINT);
            FIntVector GroupCount = FComputeShaderUtils::GetGroupCountWrapped(
                StaticCast<int32>(FMath::DivideAndRoundUp(TotalNumElements, 1u << BatchSizeLog2)),
                1 << WorkGroupSizeLog2
            );
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME(UE_MODULE_NAME ".FilterInitCS"),
                PassFlagsCompute,
                FilterInitCS,
                PassParameters,
                GroupCount
            );
        }
        const auto GetSumCount =
            [&SumsSRVs]
            (int32 IndexFromTheEnd = 0) -> uint32
            {
                return SumsSRVs.Last(IndexFromTheEnd)->GetParent()->Desc.NumElements;
            };
        const int32 WaveOpWaveSizeLog2 = 5;
        const int32 BatchSizeLog2 = 0;
        {
            using FComputeShader = FGaussianSplattingScanForwardCS;
            const int32 WorkGroupSizeLog2 = 10;
            FComputeShader::FPermutationDomain PermutationVector;
            PermutationVector.Set<FComputeShader::FWorkGroupSizeLog2>(WorkGroupSizeLog2);
            PermutationVector.Set<FComputeShader::FWaveOpWaveSizeLog2>(WaveOpWaveSizeLog2);
            PermutationVector.Set<FComputeShader::FBatchSizeLog2>(BatchSizeLog2);
            TShaderMapRef<FComputeShader> ScanForwardCS{GlobalShaderMap, PermutationVector};
            for (;;)
            {
                const uint32 SumCount = GetSumCount();
                if (SumCount == 1)
                {
                    break;
                }
                AddSums(
                    FMath::DivideAndRoundUp(SumCount, 1u << WaveOpWaveSizeLog2)
                );
                auto* PassParameters = GraphBuilder.AllocParameters<FComputeShader::FParameters>();
                PassParameters->Count = SumCount;
                PassParameters->SumsIn = SumsSRVs.Last(1);
                PassParameters->SumsOut = GraphBuilder.CreateUAV(Sums, PF_R32_UINT);
                FIntVector GroupCount = FComputeShaderUtils::GetGroupCountWrapped(
                    StaticCast<int32>(FMath::DivideAndRoundUp(SumCount, 1u << BatchSizeLog2)),
                    1 << WorkGroupSizeLog2
                );
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME(UE_MODULE_NAME ".ScanForwardCS_%i", SumsSRVs.Num()),
                    PassFlagsCompute,
                    ScanForwardCS,
                    PassParameters,
                    GroupCount
                );
            }
        }
        ReduceResultReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT(UE_MODULE_NAME ".Scan_ReduceResultReadback"));
        check(Sums->Desc.NumElements == 1);
        AddEnqueueCopyPass(GraphBuilder, ReduceResultReadback.Get(), Sums, /*NumBytes*/ 0);
        FRDGBufferRef PrefixSums = Sums;
        {
            FRDGBufferSRVRef PrefixSumsSRV = SumsSRVs.Pop();
            using FComputeShader = FGaussianSplattingScanBackwardCS;
            const int32 WorkGroupSizeLog2 = 10;
            FComputeShader::FPermutationDomain PermutationVector;
            PermutationVector.Set<FComputeShader::FWorkGroupSizeLog2>(WorkGroupSizeLog2);
            PermutationVector.Set<FComputeShader::FWaveOpWaveSizeLog2>(WaveOpWaveSizeLog2);
            PermutationVector.Set<FComputeShader::FBatchSizeLog2>(BatchSizeLog2);
            TShaderMapRef<FComputeShader> ScanBackwardCS{GlobalShaderMap, PermutationVector};
            while (!SumsSRVs.IsEmpty())
            {
                const uint32 SumCount = GetSumCount();
                auto PrefixSumsDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), SumCount);
                PrefixSums = GraphBuilder.CreateBuffer(
                    PrefixSumsDesc, *FString::Printf(TEXT(UE_MODULE_NAME ".PrefixSums_%i"), SumsSRVs.Num())
                );
                auto* PassParameters = GraphBuilder.AllocParameters<FComputeShader::FParameters>();
                PassParameters->Count = SumCount;
                PassParameters->SumsIn = SumsSRVs.Pop();
                PassParameters->PrefixSumsIn = PrefixSumsSRV;
                PassParameters->PrefixSumsOut = GraphBuilder.CreateUAV(PrefixSums, PF_R32_UINT);
                FIntVector GroupCount = FComputeShaderUtils::GetGroupCountWrapped(
                    StaticCast<int32>(SumCount),
                    1 << WorkGroupSizeLog2
                );
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME(UE_MODULE_NAME ".ScanBackwardCS_%i", SumsSRVs.Num()),
                    PassFlagsCompute,
                    ScanBackwardCS,
                    PassParameters,
                    GroupCount
                );
                PrefixSumsSRV = GraphBuilder.CreateSRV(PrefixSums, PF_R32_UINT);
                if (SumsSRVs.IsEmpty())
                {
                    FilterInitFence = RHICreateGPUFence(TEXT(UE_MODULE_NAME ".FilterInitFence"));
                    GraphBuilder.AddPass(
                        RDG_EVENT_NAME(UE_MODULE_NAME ".Fence_Scan"),
                        PassParameters,
                        PassFlagsCompute,
                        [this](FRHIComputeCommandList& CmdList)
                        {
                            CmdList.WriteGPUFence(FilterInitFence);
                        }
                    );
                }
            }
        }
        GraphBuilder.QueueBufferExtraction(Bitmasks, &BitmasksPooled);
        GraphBuilder.QueueBufferExtraction(PrefixSums, &PrefixSumsPooled);
        GraphBuilder.QueueBufferExtraction(ElementIndices, &ElementIndicesPooled);
        GraphBuilder.Execute();
        check(BitmasksPooled);
        check(PrefixSumsPooled);
        check(ElementIndicesPooled);
        FilterInitPoll();
    }

    void FilterInitPoll(
        double DelayMs = InitialDelayMs
    )
    {
        if (ReduceResultReadback && ReduceResultReadback->IsReady())
        {
            const uint64 Size = ReduceResultReadback->GetGPUSizeBytes();
            check(Size >= sizeof NumElementsOut);
            UE_LOG(LogYaGS, Verbose, TEXT("Readback memory is ready. Size: %" PRIu64), Size);
            if (auto Data = StaticCast<const uint32*>(ReduceResultReadback->Lock(sizeof NumElementsOut)))
            {
                ON_SCOPE_EXIT
                {
                    ReduceResultReadback->Unlock();
                    // clang-format off
                };
                // clang-format on
                NumElementsOut = *Data;
            }
            else
            {
                UE_LOG(LogYaGS, Warning, TEXT("Cannot lock readback memory"));
            }
            ReduceResultReadback.Reset();
        }
        if (FilterInitFence && FilterInitFence->Poll())
        {
            FilterInitFence->Clear();
            UE_LOG(LogYaGS, Verbose, TEXT("FilterInitFence is ready"));
            FilterInitFence.SafeRelease();
        }
        if (ReduceResultReadback || FilterInitFence)
        {
            UE::Tasks::FTask Task = UE::Tasks::Launch(
                UE_SOURCE_LOCATION,
                [this, DelayMs]
                {
                    FilterInitPoll(DelayMs * ExponentialBackoff);
                },
                UE::Tasks::ETaskPriority::Inherit,
                UE::Tasks::EExtendedTaskPriority::RenderThreadNormalPri
            );
            UE_LOG(LogYaGS, Verbose, TEXT("Polling task relaunched for FilterInit"));
            if (!Task.Wait(FTimespan::FromMilliseconds(DelayMs)))  // throttle relaunching a little bit
            {
                UE::Tasks::AddNested(Task);
            }
        }
        else
        {
            check(NumElementsOut != TNumericLimits<uint32>::Max());
            UE_LOG(LogYaGS, Log, TEXT("Filter succeeded. NumElementsOut: %u"), NumElementsOut);
            check(BitmasksPooled);
            check(PrefixSumsPooled);
            check(ElementIndicesPooled);
            if (NumElementsOut == 0)
            {
                return;
            }
            FilterFinish(GetImmediateCommandList_ForRenderCommand());
        }
    }

    void FilterFinish(
        FRHICommandListImmediate& CmdList
    )
    {
        check(NumElementsOut != 0);
        auto GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
        FRDGBuilder GraphBuilder{CmdList, RDG_EVENT_NAME(UE_MODULE_NAME ".FilterFinish"), ERDGBuilderFlags::Parallel};
        YaGS::UpdateSceneParameters(SceneParameters, GraphBuilder, VolumesPooled, InstancesPooled);
        auto Bitmasks = GraphBuilder.RegisterExternalBuffer(BitmasksPooled);
        auto PrefixSums = GraphBuilder.RegisterExternalBuffer(PrefixSumsPooled);
        auto ElementIndices = GraphBuilder.RegisterExternalBuffer(ElementIndicesPooled);
        auto ModelElementIndicesDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumElementsOut);
        auto ModelElementIndices =
            GraphBuilder.CreateBuffer(ModelElementIndicesDesc, TEXT(UE_MODULE_NAME ".ModelElementIndices"));
        {
            const uint32 BitmaskCount = FMath::DivideAndRoundUp(TotalNumElements, 1u << YaGS::WaveOpBitmaskBitsizeLog2);
            using FComputeShader = FGaussianSplattingFilterGatherIndicesCS;
            const int32 WorkGroupSizeLog2 = 10;
            const int32 WaveOpWaveSizeLog2 = 5;
            const int32 BatchSizeLog2 = 0;
            FComputeShader::FPermutationDomain PermutationVector;
            PermutationVector.Set<FComputeShader::FWorkGroupSizeLog2>(WorkGroupSizeLog2);
            PermutationVector.Set<FComputeShader::FWaveOpWaveSizeLog2>(WaveOpWaveSizeLog2);
            PermutationVector.Set<FComputeShader::FBatchSizeLog2>(BatchSizeLog2);
            TShaderMapRef<FComputeShader> FilterGatherIndicesCS{GlobalShaderMap, PermutationVector};
            auto* PassParameters = GraphBuilder.AllocParameters<FComputeShader::FParameters>();
            PassParameters->Count = BitmaskCount;
            PassParameters->BitmasksIn = GraphBuilder.CreateSRV(Bitmasks);
            PassParameters->PrefixSumsIn = GraphBuilder.CreateSRV(PrefixSums, PF_R32_UINT);
            PassParameters->ElementIndicesIn = GraphBuilder.CreateSRV(ElementIndices, PF_R32_UINT);
            PassParameters->ElementIndicesOut = GraphBuilder.CreateUAV(ModelElementIndices, PF_R32_UINT);
            FIntVector GroupCount = FComputeShaderUtils::GetGroupCountWrapped(
                StaticCast<int32>(BitmaskCount),
                1 << WorkGroupSizeLog2
            );
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME(UE_MODULE_NAME ".FilterGatherIndicesCS"),
                PassFlagsCompute,
                FilterGatherIndicesCS,
                PassParameters,
                GroupCount
            );
        }
        StaticBuffer.SetOwnerName(FName{ TEXT("FilterStaticBuffer") });
        StaticBuffer.SetNumElements(NumElementsOut);
        StaticBuffer.InitRHI(CmdList);
        check(StaticBuffer.Buffers.Num() <= YaGS::GMaxModelOutCount);
        check(ModelsPooled.IsEmpty());
        ModelsPooled.Reserve(StaticBuffer.Buffers.Num());
        for (const auto& Buffer : StaticBuffer.Buffers)
        {
            FRDGBufferDesc Desc;
            Desc.Usage = Buffer.Buffer->GetUsage();
            Desc.BytesPerElement = StaticBuffer.GetStride();
            Desc.NumElements = Buffer.Buffer->GetSize() / Desc.BytesPerElement;
            ModelsPooled.Add(
                MakeRefCount<FRDGPooledBuffer>(Buffer.Buffer, Desc, Desc.NumElements, TEXT("ModelPooled"))
            );
        }
        TArray<FRDGBufferRef> Models;
        Models.Reserve(ModelsPooled.Num());
        for (const auto& ModelPooled : ModelsPooled)
        {
            Models.Add(GraphBuilder.RegisterExternalBuffer(ModelPooled));
        }
        {
            using FComputeShader = FGaussianSplattingFilterGatherCS;
            const int32 WorkGroupSizeLog2 = 10;
            const int32 WaveOpWaveSizeLog2 = 5;
            const int32 BatchSizeLog2 = 0;
            FComputeShader::FPermutationDomain PermutationVector;
            PermutationVector.Set<FComputeShader::FWorkGroupSizeLog2>(WorkGroupSizeLog2);
            PermutationVector.Set<FComputeShader::FWaveOpWaveSizeLog2>(WaveOpWaveSizeLog2);
            PermutationVector.Set<FComputeShader::FBatchSizeLog2>(BatchSizeLog2);
            TShaderMapRef<FComputeShader> FilterGatherCS{GlobalShaderMap, PermutationVector};
            auto* PassParameters = GraphBuilder.AllocParameters<FComputeShader::FParameters>();
            PassParameters->SceneParameters = SceneParameters;
            PassParameters->Count = NumElementsOut;
            PassParameters->ElementIndicesIn = GraphBuilder.CreateSRV(ModelElementIndices, PF_R32_UINT);
            int32 ModelIndex = 0;
            for (const auto& Model : Models)
            {
                PassParameters->ModelsOut[ModelIndex++] = GraphBuilder.CreateUAV(Model);
            }
            for (; ModelIndex < PassParameters->ModelsOut.Num(); ++ModelIndex)
            {
                PassParameters->ModelsOut[ModelIndex] = PassParameters->ModelsOut[ModelIndex - 1];
            }
            PassParameters->SelectionOrigin = FVector3f{ SelectionBox.Min };
            PassParameters->DefaultModelRotationAndTranslation.SetMatrixTranspose(
                YaGS::DefaultModelTransform.ToMatrixNoScale()
            );
            PassParameters->DefaultModelScale = FVector3f{YaGS::DefaultModelTransform.GetScale3D()};
            FIntVector GroupCount = FComputeShaderUtils::GetGroupCountWrapped(
                StaticCast<int32>(NumElementsOut),
                1 << WorkGroupSizeLog2
            );
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME(UE_MODULE_NAME ".FilterGatherCS"),
                PassFlagsCompute,
                FilterGatherCS,
                PassParameters,
                GroupCount
            );
            {
                FilterFinishFence = RHICreateGPUFence(TEXT(UE_MODULE_NAME ".FilterFinishFence"));
                GraphBuilder.AddPass(
                    RDG_EVENT_NAME(UE_MODULE_NAME ".Fence_Gather"),
                    PassParameters,
                    PassFlagsCompute,
                    [this](FRHIComputeCommandList& CmdList)
                    {
                        CmdList.WriteGPUFence(FilterFinishFence);
                    }
                );
            }
        }
        GraphBuilder.Execute();
        FilterFinishPoll();
    }

    void FilterFinishPoll(
        double DelayMs = InitialDelayMs
    )
    {
        if (FilterFinishFence && FilterFinishFence->Poll())
        {
            FilterFinishFence->Clear();
            UE_LOG(LogYaGS, Verbose, TEXT("FilterFinishFence is ready"));
            FilterFinishFence.SafeRelease();
            VolumesPooled.SafeRelease();
            InstancesPooled.SafeRelease();
            BitmasksPooled.SafeRelease();
            PrefixSumsPooled.SafeRelease();
            ElementIndicesPooled.SafeRelease();
            for (auto& ModelPooled : ModelsPooled)
            {
                ModelPooled.SafeRelease();
            }
            ModelsPooled.Empty();
        }
        if (FilterFinishFence)
        {
            UE::Tasks::FTask Task = UE::Tasks::Launch(
                UE_SOURCE_LOCATION,
                [this, DelayMs]
                {
                    FilterFinishPoll(DelayMs * ExponentialBackoff);
                },
                UE::Tasks::ETaskPriority::Inherit,
                UE::Tasks::EExtendedTaskPriority::RenderThreadNormalPri
            );
            UE_LOG(LogYaGS, Verbose, TEXT("Polling task relaunched for FilterGather"));
            if (!Task.Wait(FTimespan::FromMilliseconds(DelayMs)))  // throttle relaunching a little bit
            {
                UE::Tasks::AddNested(Task);
            }
        }
        else
        {
            SelectionOrigin = SelectionBox.Min;
        }
    }
};

}

FGaussianSplattingEditingActionsBase::~FGaussianSplattingEditingActionsBase()
{
    if (!Pipe.WaitUntilEmpty())
    {
        checkNoEntry();
    }
}

bool FGaussianSplattingEditingActionsBase::Fuse(
    const FGaussianSplattingActorWeakPtrs& InActors
) const
{
    check(IsInGameThread());
    if (InActors.IsEmpty())
    {
        UE_LOG(LogYaGS, Log, TEXT("Unable to Fuse %i actors"), InActors.Num());
        return false;
    }
    auto Actors = ConvertToStrongPtrs(InActors);
    if (Actors.IsEmpty())
    {
        UE_LOG(LogYaGS, Warning, TEXT("No actors alive"));
        return false;
    }
    auto SceneProxyData = GetSceneProxyData(Actors);
    if (SceneProxyData.IsEmpty())
    {
        UE_LOG(LogYaGS, Warning, TEXT("No scene proxies in resulting selection"));
        return false;
    }
    FGaussianSplattingStaticBuffer StaticBuffer{TEXT(UE_MODULE_NAME ".FilterStaticBuffer")};
    FVector SelectionOrigin;
    {
        FGaussianSplattingEditingActionsBaseImpl EditingActions{
            MoveTemp(SceneProxyData),
            StaticBuffer,
            SelectionOrigin,
        };
        UE::Tasks::FTask Task = Pipe.Launch(
            UE_SOURCE_LOCATION,
            [&]
            {
                EditingActions.Fuse_RenderThread();
            },
            UE::Tasks::ETaskPriority::Default,
            UE::Tasks::EExtendedTaskPriority::RenderThreadNormalPri
        );
        if (!Task.Wait())
        {
            UE_LOG(LogYaGS, Error, TEXT("Fuse failed"));
            return false;
        }
    }
    CreateNewAssetAndActor(MoveTemp(Actors), SelectionOrigin, MoveTemp(StaticBuffer));
    UE_LOG(LogYaGS, Verbose, TEXT("Fuse succeeded"));
    return true;
}
