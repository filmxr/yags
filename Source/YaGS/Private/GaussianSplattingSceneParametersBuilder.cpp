#include "GaussianSplattingSceneParametersBuilder.h"

#include "GaussianSplattingByteAddressBufferView.h"
#include "GaussianSplattingCommon.h"
#include "GaussianSplattingLog.h"
#include "GaussianSplattingQuadIndices.h"
#include "GaussianSplattingSceneProxy.h"
#include "GaussianSplattingSceneProxyData.h"
#include "GaussianSplattingStaticBuffer.h"

#include "RHICommandList.h"

namespace
{

TAutoConsoleVariable<int32> CVarGaussianSplattinMaxSHDegree{
    TEXT("r." UE_MODULE_NAME ".MaxSHDegree"),
    YaGS::GMaxSHOrder - 1,
    *FString::Printf(TEXT(" %i - %i: MaxSHDegree\n"), 0, YaGS::GMaxSHOrder - 1)
         .Appendf(TEXT("       *: Clamped to %i - %i\n"), 0, YaGS::GMaxSHOrder - 1),
    ECVF_RenderThreadSafe
};

TAutoConsoleVariable<float> CVarYaGSMaxLambda{
    TEXT("r." UE_MODULE_NAME ".MaxLambda"),
    1.0f,
    TEXT(" 0.0 - +inf: MaxLambda\n") TEXT(" *: 0.0\n"),
    ECVF_RenderThreadSafe
};

TAutoConsoleVariable<int32> CVarYaGSSortKeyMaskBitCount{
    TEXT("r." UE_MODULE_NAME ".SortKeyMaskBitCount"),
    32,
    TEXT(" 1-32: number of key bits taken into account during splats sorting"),
    ECVF_RenderThreadSafe
};

void CollectSceneParameters(
    TConstArrayView<FGaussianSplattingSceneProxyData> SceneProxyData,
    TGaussianSplattingByteAddressBufferView<FSceneRenderingArrayAllocator> VolumeData,
    TArray<FGaussianSplattingInstance, FSceneRenderingArrayAllocator>& InstanceData,
    FGaussianSplattingSceneParameters& SceneParameters,
    uint32* InSortKeyMaskBitCount,
    uint32* InTotalNumElements
)
{
    SceneParameters.QuadIndices = YaGS::GQuadIndices.ShaderResourceViewRHI;
    const float MaxLambda = FMath::Max(CVarYaGSMaxLambda.GetValueOnRenderThread(), 0.0f);
    const int32 MaxSHDegree =
        FMath::Clamp(CVarGaussianSplattinMaxSHDegree.GetValueOnRenderThread(), 0, YaGS::GMaxSHOrder - 1);
    const uint32 SortKeyMaskBitCount = FMath::Clamp(CVarYaGSSortKeyMaskBitCount.GetValueOnRenderThread(), 1, 32);
    InstanceData.Reserve(SceneProxyData.Num());
    TMap<FRHIShaderResourceViewRef, int32 /*ModelIndex*/> Models;
    Models.Reserve(SceneProxyData.Num());
    constexpr uint32 MaxIndexBits = 32;
    uint32 TotalNumElements = 0;
    uint32 MaxNumElements = 0;
    for (const FGaussianSplattingSceneProxyData& SceneProxyDatum : SceneProxyData)
    {
        const FGaussianSplattingStaticBuffer& StaticBuffer = *SceneProxyDatum.StaticBuffer;
        MaxNumElements = FMath::Max(MaxNumElements, StaticCast<uint32>(StaticBuffer.GetNumElements()));
        const uint32 ElementIndexBits = FMath::CeilLogTwo(MaxNumElements);
        const uint32 InstanceIndexBitCount = FMath::CeilLogTwo(StaticCast<uint32>(InstanceData.Num() + 1));
        if (ElementIndexBits + InstanceIndexBitCount > MaxIndexBits)
        {
            UE_LOG(
                LogYaGS,
                Warning,
                TEXT(
                    "Not enough space for encoding element and"
                    " instance indices together (%u + %u > %u):"
                    " '%s' is skipped"
                ),
                ElementIndexBits,
                InstanceIndexBitCount,
                MaxIndexBits,
                *SceneProxyDatum.Name
            );
            continue;
        }
        int32 BaseModelIndex = -1;
        for (const auto& Buffer : StaticBuffer.Buffers)
        {
            FRHIShaderResourceViewRef StaticBufferSRV = Buffer.SRV.GetReference();
            if (const int32* ModelIndexPtr = Models.Find(StaticBufferSRV))
            {
                if (BaseModelIndex < 0)
                {
                    BaseModelIndex = *ModelIndexPtr;
                }
            }
            else
            {
                if (Models.Num() == SceneParameters.Models.Num())
                {
                    UE_LOG(
                        LogYaGS,
                        Warning,
                        TEXT(
                            "Maximum number of model buffers %i is reached. '%s' "
                            "is skipped"
                        ),
                        SceneParameters.Models.Num(),
                        *SceneProxyDatum.Name
                    );
                    continue;
                }
                const int32 ModelIndex = Models.Emplace(StaticBufferSRV, Models.Num());
                if (BaseModelIndex < 0)
                {
                    BaseModelIndex = ModelIndex;
                }
                SceneParameters.Models[ModelIndex] = MoveTemp(StaticBufferSRV);
            }
        }
        if (BaseModelIndex < 0)
        {
            UE_LOG(LogYaGS, Warning, TEXT("No valid model buffer found for '%s', skipping"), *SceneProxyDatum.Name);
            continue;
        }
        auto& Instance = InstanceData.AddDefaulted_GetRef();
        if (SceneProxyDatum.bIsSRGB)
        {
            EnumAddFlags(Instance.Flags, FGaussianSplattingInstance::EFlags::IsSRGB);
        }
        Instance.BaseModelIndex = BaseModelIndex;
        Instance.BaseIndex = TotalNumElements;
        const FMatrix& LocalToWorld = SceneProxyDatum.LocalToWorld;
        Instance.SetModelMatrix(LocalToWorld);
        Instance.Scale = SceneProxyDatum.Scale;
        Instance.MaxLambda = FMath::Min(SceneProxyDatum.MaxLambda, MaxLambda);
        Instance.Appearance = SceneProxyDatum.Appearance;
        Instance.Appearance.MaxSHDegree = FMath::Min(Instance.Appearance.MaxSHDegree, MaxSHDegree);
        Instance.Crops = VolumeData.Append(SceneProxyDatum.Crops);
        Instance.Culls = VolumeData.Append(SceneProxyDatum.Culls);
        Instance.LocalAppearances = VolumeData.Append(SceneProxyDatum.LocalAppearances);
        Instance.End = VolumeData.GetOffset();
        TotalNumElements += StaticBuffer.GetNumElements();
    }
    check(!InstanceData.IsEmpty());
    if (VolumeData.IsEmpty())
    {
        VolumeData.Append<uint32>(0);
    }
    SceneParameters.InstanceCount = StaticCast<uint32>(InstanceData.Num());
    SceneParameters.InstanceIndexBitCount = FMath::CeilLogTwo(SceneParameters.InstanceCount);
    SceneParameters.InstanceIndexBitmask = (1u << SceneParameters.InstanceIndexBitCount) - 1u;
    if (Models.Num() > 0)
    {
        for (int32 ModelIndex = Models.Num(); ModelIndex < SceneParameters.Models.Num(); ++ModelIndex)
        {
            SceneParameters.Models[ModelIndex] = SceneParameters.Models[ModelIndex - 1];
        }
    }
    if (InSortKeyMaskBitCount != nullptr)
    {
        *InSortKeyMaskBitCount = SortKeyMaskBitCount;
    }
    if (InTotalNumElements != nullptr)
    {
        *InTotalNumElements = TotalNumElements;
    }
}

}  // namespace

namespace YaGS
{

void BuildSceneParameters(
    TConstArrayView<FGaussianSplattingSceneProxyData> SceneProxyData,
    FRDGBuilder& GraphBuilder,
    FGaussianSplattingSceneParameters& SceneParameters,
    uint32* SortKeyMaskBitCount,
    uint32* TotalNumElements,
    TRefCountPtr<FRDGPooledBuffer>* VolumesPooled,
    TRefCountPtr<FRDGPooledBuffer>* InstancesPooled
)
{
    check(IsInRenderingThread());
    check(!SceneProxyData.IsEmpty());
    check(!VolumesPooled == !InstancesPooled);
    auto& VolumeData = GraphBuilder.AllocArray<uint8>();
    auto& InstanceData = GraphBuilder.AllocArray<FGaussianSplattingInstance>();
    CollectSceneParameters(
        SceneProxyData, VolumeData, InstanceData, SceneParameters, SortKeyMaskBitCount, TotalNumElements
    );
    {
        auto Volumes = CreateByteAddressBuffer(
            GraphBuilder, TEXT(UE_MODULE_NAME ".Volumes"), MakeConstArrayView(VolumeData), ERDGInitialDataFlags::NoCopy
        );
        if (VolumesPooled)
        {
            GraphBuilder.QueueBufferExtraction(Volumes, VolumesPooled);
        }
        SceneParameters.Volumes = GraphBuilder.CreateSRV(Volumes);
    }
    {
        auto Instances = CreateStructuredBuffer(
            GraphBuilder,
            TEXT(UE_MODULE_NAME ".Instances"),
            MakeConstArrayView(InstanceData),
            ERDGInitialDataFlags::NoCopy
        );
        if (InstancesPooled)
        {
            GraphBuilder.QueueBufferExtraction(Instances, InstancesPooled);
        }
        SceneParameters.Instances = GraphBuilder.CreateSRV(Instances);
    }
}

void UpdateSceneParameters(
    FGaussianSplattingSceneParameters& SceneParameters,
    FRDGBuilder& GraphBuilder,
    TRefCountPtr<FRDGPooledBuffer>& VolumesPooled,
    TRefCountPtr<FRDGPooledBuffer>& InstancesPooled
)
{
    {
        check(VolumesPooled);
        auto Volumes = GraphBuilder.RegisterExternalBuffer(VolumesPooled);
        SceneParameters.Volumes = GraphBuilder.CreateSRV(Volumes);
    }
    {
        check(InstancesPooled);
        auto Instances = GraphBuilder.RegisterExternalBuffer(InstancesPooled);
        SceneParameters.Instances = GraphBuilder.CreateSRV(Instances);
    }
}

}  // namespace YaGS
