#include "GaussianSplattingShaders.h"

#include "GaussianSplattingLog.h"

#include "Async/ParallelFor.h"
#include "GaussianSplat.h"
#include "RHIDefinitions.h"
#include "RHIGlobals.h"
#include "Shader.h"
#include "ShaderPermutationUtils.h"

ENUM_CLASS_FLAGS(FGaussianSplattingInstance::EFlags)

namespace
{

constexpr uint32 GMeshShaderMaxVertexCount = 256;
constexpr uint32 GMeshShaderMaxPrimitiveCount = 256;

bool ShouldCompilePermutation_Common(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    if (!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6))
    {
        return false;
    }
    return FGlobalShader::ShouldCompilePermutation(Parameters);
}

void ModifyCompilationEnvironment_Common(
    const FGlobalShaderPermutationParameters& Parameters,
    FShaderCompilerEnvironment& OutEnvironment
)
{
    OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
    OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
    OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
    OutEnvironment.SetDefine(TEXT("MAX_SH_ORDER"), YaGS::GMaxSHOrder);
    OutEnvironment.SetDefine(TEXT("CHUNK_LEN"), StaticCast<int32>(YaGS::GChunkLen));
    {
        using EFlags = FGaussianSplattingInstance::EFlags;
        OutEnvironment.SetDefine(TEXT("INSTANCE_FLAG_IS_SRGB"), StaticCast<uint32>(EFlags::IsSRGB));
    }
    return FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
}

}

FGaussianSplattingVolume FGaussianSplattingVolume::Make(const FMatrix& InBox)
{
    FGaussianSplattingVolume Volume;
    FMatrix3x4 M;
    M.SetMatrixTranspose(InBox);
    FMemory::Memcpy(Volume.Box, M.M);
    return Volume;
}

void FGaussianSplattingInstance::SetModelMatrix(
    FMatrix InModelMatrix
)
{
    FVector3f ModelScale{InModelMatrix.ExtractScaling()};
    FMatrix44f ModelMatrix{InModelMatrix};
    ModelTransform.Set(ModelMatrix, ModelScale);
}

bool FGaussianSplattingComputeShaderBase::ShouldCompilePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    FPermutationDomain PermutationVector{Parameters.PermutationId};
    const int32 WorkGroupSizeLog2 = PermutationVector.Get<FWorkGroupSizeLog2>();
    const int32 WaveOpWaveSizeLog2 = PermutationVector.Get<FWaveOpWaveSizeLog2>();
    if (WorkGroupSizeLog2 < WaveOpWaveSizeLog2)
    {
        return false;
    }
    if (!UE::ShaderPermutationUtils::ShouldCompileWithWaveSize(Parameters, 1 << WaveOpWaveSizeLog2))
    {
        return false;
    }
    const int32 BatchSizeLog2 = PermutationVector.Get<FBatchSizeLog2>();
    if (BatchSizeLog2 > YaGS::WaveOpWaveSizeMaxLog2)
    {
        return false;
    }
    return ShouldCompilePermutation_Common(Parameters);
}

void FGaussianSplattingComputeShaderBase::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment
)
{
    OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
    OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
    {
        FPermutationDomain PermutationVector{Parameters.PermutationId};
        const int32 WorkGroupSizeLog2 = PermutationVector.Get<FWorkGroupSizeLog2>();
        const int32 WaveOpWaveSizeLog2 = PermutationVector.Get<FWaveOpWaveSizeLog2>();
        const int32 BatchSizeLog2 = PermutationVector.Get<FBatchSizeLog2>();
        OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), 1u << WorkGroupSizeLog2);
        OutEnvironment.SetDefine(TEXT("WAVE_OP_WAVE_SIZE"), 1u << WaveOpWaveSizeLog2);
        OutEnvironment.SetDefine(TEXT("BATCH_SIZE"), 1u << BatchSizeLog2);
    }
    OutEnvironment.SetDefine(TEXT("BITMASK_BITSIZE"), 1u << YaGS::WaveOpBitmaskBitsizeLog2);
    OutEnvironment.SetDefine(TEXT("BITMASK_COMPONENT_BITSIZE"), 1u << YaGS::WaveOpBitmaskComponentBitsizeLog2);
    return ModifyCompilationEnvironment_Common(Parameters, OutEnvironment);
}

EShaderPermutationPrecacheRequest FGaussianSplattingComputeShaderBase::ShouldPrecachePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    FPermutationDomain PermutationVector{Parameters.PermutationId};
    const int32 WaveOpWaveSize = 1 << PermutationVector.Get<FWaveOpWaveSizeLog2>();
    if (!UE::ShaderPermutationUtils::ShouldPrecacheWithWaveSize(Parameters, WaveOpWaveSize))
    {
        return EShaderPermutationPrecacheRequest::NotUsed;
    }
    const int32 WorkGroupSize = 1 << PermutationVector.Get<FWorkGroupSizeLog2>();
    if (WorkGroupSize > GMaxWorkGroupInvocations)
    {
        return EShaderPermutationPrecacheRequest::NotUsed;
    }
    return FGlobalShader::ShouldPrecachePermutation(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingScanForwardCS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplattingScan.usf",
    "ScanForwardCS",
    EShaderFrequency::SF_Compute
);

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingScanBackwardCS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplattingScan.usf",
    "ScanBackwardCS",
    EShaderFrequency::SF_Compute
);

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingFilterInitCS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplattingFilter.usf",
    "InitCS",
    EShaderFrequency::SF_Compute
);

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingFilterGatherIndicesCS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplattingFilter.usf",
    "GatherIndicesCS",
    EShaderFrequency::SF_Compute
);

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingFilterGatherCS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplattingFilter.usf",
    "GatherCS",
    EShaderFrequency::SF_Compute
);

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingPrepareSortKeyValuesCS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplattingSortUtils.usf",
    "PrepareSortKeyValuesCS",
    EShaderFrequency::SF_Compute
);

FIntPoint FGaussianSplattingDepthCopyCS::GetGroupSize()
{
    return {FComputeShaderUtils::kGolden2DGroupSize};
}

bool FGaussianSplattingDepthCopyCS::ShouldCompilePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return ShouldCompilePermutation_Common(Parameters);
}

void FGaussianSplattingDepthCopyCS::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment
)
{
    FIntPoint GroupSize = GetGroupSize();
    OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), GroupSize.X);
    OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), GroupSize.Y);
    return ModifyCompilationEnvironment_Common(Parameters, OutEnvironment);
}

EShaderPermutationPrecacheRequest FGaussianSplattingDepthCopyCS::ShouldPrecachePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return FGlobalShader::ShouldPrecachePermutation(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingDepthCopyCS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplattingDepthCopy.usf",
    "DepthCopyCS",
    EShaderFrequency::SF_Compute
);

bool FGaussianSplattingVS::ShouldCompilePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return ShouldCompilePermutation_Common(Parameters);
}

void FGaussianSplattingVS::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment
)
{
    OutEnvironment.SetDefine(TEXT("VERTEX_COUNT_PER_QUAD"), YaGS::GVertexCountPerQuad);
    return ModifyCompilationEnvironment_Common(Parameters, OutEnvironment);
}

EShaderPermutationPrecacheRequest FGaussianSplattingVS::ShouldPrecachePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return FGlobalShader::ShouldPrecachePermutation(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingVS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplatting.usf",
    "MainVS",
    EShaderFrequency::SF_Vertex
);

bool FGaussianSplattingMS::ShouldCompilePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return ShouldCompilePermutation_Common(Parameters);
}

uint32 FGaussianSplattingMS::GetThreadGroupSize(const EShaderPlatform& ShaderPlatform)
{
    const uint32 MaxMeshShaderThreadGroupSize =
        FDataDrivenShaderPlatformInfo::GetMaxMeshShaderThreadGroupSize(ShaderPlatform);
    return FMath::Min3(
        MaxMeshShaderThreadGroupSize,
        GMeshShaderMaxVertexCount / YaGS::GVertexCountPerQuad,
        GMeshShaderMaxPrimitiveCount / YaGS::GTriangleCountPerQuad
    );
}

void FGaussianSplattingMS::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters,
    FShaderCompilerEnvironment& OutEnvironment
)
{
    OutEnvironment.SetDefine(TEXT("TRIANGLE_COUNT_PER_QUAD"), YaGS::GTriangleCountPerQuad);
    const uint32 MeshShaderThreadGroupSize = GetThreadGroupSize(Parameters.Platform);
    OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), MeshShaderThreadGroupSize);
    OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
    OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
    return FGaussianSplattingVS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
}

EShaderPermutationPrecacheRequest FGaussianSplattingMS::ShouldPrecachePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return FGlobalShader::ShouldPrecachePermutation(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingMS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplatting.usf",
    "MainMS",
    EShaderFrequency::SF_Mesh
);

bool FGaussianSplattingAS::ShouldCompilePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return ShouldCompilePermutation_Common(Parameters);
}

void FGaussianSplattingAS::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters,
    FShaderCompilerEnvironment& OutEnvironment
)
{
    OutEnvironment.SetDefine(TEXT("DIMENSION_LIMIT_X"), GRHIMaxDispatchThreadGroupsPerDimension.X);
    OutEnvironment.SetDefine(TEXT("DIMENSION_LIMIT_Y"), GRHIMaxDispatchThreadGroupsPerDimension.Y);
    return FGaussianSplattingMS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
}

EShaderPermutationPrecacheRequest FGaussianSplattingAS::ShouldPrecachePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return FGlobalShader::ShouldPrecachePermutation(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingAS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplatting.usf",
    "MainAS",
    EShaderFrequency::SF_Amplification
);

bool FGaussianSplattingColorPS::ShouldCompilePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return ShouldCompilePermutation_Common(Parameters);
}

void FGaussianSplattingColorPS::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment
)
{
    return ModifyCompilationEnvironment_Common(Parameters, OutEnvironment);
}

EShaderPermutationPrecacheRequest FGaussianSplattingColorPS::ShouldPrecachePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return FGlobalShader::ShouldPrecachePermutation(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingColorPS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplatting.usf",
    "ColorPS",
    EShaderFrequency::SF_Pixel
);

bool FGaussianSplattingDepthPS::ShouldCompilePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return ShouldCompilePermutation_Common(Parameters);
}

void FGaussianSplattingDepthPS::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment
)
{
    return ModifyCompilationEnvironment_Common(Parameters, OutEnvironment);
}

EShaderPermutationPrecacheRequest FGaussianSplattingDepthPS::ShouldPrecachePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return FGlobalShader::ShouldPrecachePermutation(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingDepthPS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplatting.usf",
    "DepthPS",
    EShaderFrequency::SF_Pixel
);

bool FGaussianSplattingSceneBlendPS::ShouldCompilePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return ShouldCompilePermutation_Common(Parameters);
}

void FGaussianSplattingSceneBlendPS::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters,
    FShaderCompilerEnvironment& OutEnvironment
)
{
    return ModifyCompilationEnvironment_Common(Parameters, OutEnvironment);
}

EShaderPermutationPrecacheRequest FGaussianSplattingSceneBlendPS::ShouldPrecachePermutation(
    const FGlobalShaderPermutationParameters& Parameters
)
{
    return FGlobalShader::ShouldPrecachePermutation(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(
    FGaussianSplattingSceneBlendPS,
    "/Plugin/" UE_PLUGIN_NAME "/Private/GaussianSplattingSceneBlend.usf",
    "MainPS",
    EShaderFrequency::SF_Pixel
);
