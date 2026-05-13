#pragma once

#include "GaussianSplattingCommon.h"
#include "GaussianSplattingTransform.h"
#include "GlobalShader.h"
#include "Matrix3x4.h"
#include "ScreenPass.h"
#include "ShaderCore.h"
#include "ShaderParameterMacros.h"
#include "ShaderParameterStruct.h"
#include <type_traits>

namespace YaGS {
inline constexpr uint32 GMaxModelCount = 60;
inline constexpr uint32 GMaxModelOutCount = 16;

inline constexpr int32 WaveOpBitmaskComponentBitsizeLog2 =
    FMath::ConstExprCeilLogTwo(std::numeric_limits<uint32>::digits); // uint
inline constexpr int32 WaveOpBitmaskBitsizeLog2 =
    WaveOpBitmaskComponentBitsizeLog2 + FMath::ConstExprCeilLogTwo(4); // uint4
inline constexpr int32 WaveOpWaveSizeMinLog2 =
    FMath::ConstExprCeilLogTwo(4); // Minimum wave/warp size is 4 in all GAPIs
inline constexpr int32 WaveOpWaveSizeMaxLog2 = WaveOpBitmaskBitsizeLog2;
inline constexpr int32 WaveOpWaveSizeLog2Count = WaveOpWaveSizeMaxLog2 - WaveOpWaveSizeMinLog2 + 1;
} // namespace YaGS

#pragma pack(push, 1)

struct FGaussianSplattingVolume
{
    decltype(FMatrix3x4::M) Box = {};

    static FGaussianSplattingVolume Make(const FMatrix& Box);
};
static_assert(std::is_standard_layout_v<FGaussianSplattingVolume>);
static_assert(std::is_trivially_copyable_v<FGaussianSplattingVolume>);

struct FGaussianSplattingAppearance
{
    int32 MaxSHDegree = 0;
    FVector3f HSV;
    FVector3f Tint;
    float Gamma = 1.0f;

    auto operator <=> (const FGaussianSplattingAppearance&) const = default;
};
static_assert(std::is_standard_layout_v<FGaussianSplattingAppearance>);
static_assert(std::is_trivially_copyable_v<FGaussianSplattingAppearance>);

struct FGaussianSplattingLocalAppearance
{
    FGaussianSplattingVolume Volume;
    float FalloffDistance = 1.0f;
    FGaussianSplattingAppearance Appearance;
};
static_assert(std::is_standard_layout_v<FGaussianSplattingLocalAppearance>);
static_assert(std::is_trivially_copyable_v<FGaussianSplattingLocalAppearance>);

struct FGaussianSplattingInstance
{
    UENUM(BlueprintType)
    enum class EFlags : uint32
    {
        IsSRGB = 1u << 0,
    };
    FRIEND_ENUM_CLASS_FLAGS(EFlags)

    EFlags Flags = {};
    uint32 BaseModelIndex = 0;
    uint32 BaseIndex = 0;
    FGaussianSplattingTransform ModelTransform;
    float Scale = 1.0f;
    float MaxLambda = 1.0f;
    FGaussianSplattingAppearance Appearance;

    int32 Crops = 0;
    int32 Culls = 0;
    int32 LocalAppearances = 0;
    int32 End = 0;

    void SetModelMatrix(FMatrix InModelMatrix);
};
static_assert(std::is_standard_layout_v<FGaussianSplattingInstance>);
static_assert(std::is_trivially_copyable_v<FGaussianSplattingInstance>);

#pragma pack(pop)

class FGaussianSplattingComputeShaderBase : public FGlobalShader {
public:
    using FGlobalShader::FGlobalShader;

    class FWorkGroupSizeLog2
        : SHADER_PERMUTATION_RANGE_INT("NUM_THREADS_PER_GROUP_LOG2", YaGS::WaveOpWaveSizeMaxLog2, 4);
    class FWaveOpWaveSizeLog2 : SHADER_PERMUTATION_RANGE_INT("WAVE_OP_WAVE_SIZE_LOG2", YaGS::WaveOpWaveSizeMinLog2,
                                                             YaGS::WaveOpWaveSizeLog2Count);
    class FBatchSizeLog2 : SHADER_PERMUTATION_INT("BATCH_SIZE_LOG2", 8);

    using FPermutationDomain = TShaderPermutationDomain<FWorkGroupSizeLog2, FWaveOpWaveSizeLog2, FBatchSizeLog2>;

    static bool ShouldCompilePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters &Parameters,
                                             FShaderCompilerEnvironment &OutEnvironment);

    static EShaderPermutationPrecacheRequest
    ShouldPrecachePermutation(const FGlobalShaderPermutationParameters &Parameters);
};

class FGaussianSplattingScanForwardCS final : public FGaussianSplattingComputeShaderBase {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingScanForwardCS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingScanForwardCS, FGaussianSplattingComputeShaderBase);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, /*YAGS_API*/)
    SHADER_PARAMETER(uint32, Count)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SumsIn)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, SumsOut)
    END_SHADER_PARAMETER_STRUCT()
};

class FGaussianSplattingScanBackwardCS final : public FGaussianSplattingComputeShaderBase {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingScanBackwardCS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingScanBackwardCS, FGaussianSplattingComputeShaderBase);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, /*YAGS_API*/)
    SHADER_PARAMETER(uint32, Count)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SumsIn)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PrefixSumsIn)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, PrefixSumsOut)
    END_SHADER_PARAMETER_STRUCT()
};

BEGIN_SHADER_PARAMETER_STRUCT(FGaussianSplattingSceneParameters, /*YAGS_API*/)
SHADER_PARAMETER_SRV(Buffer<min16uint>, QuadIndices)
SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, Volumes)
SHADER_PARAMETER(uint32, InstanceCount)
SHADER_PARAMETER(int32, InstanceIndexBitCount)
SHADER_PARAMETER(uint32, InstanceIndexBitmask)
SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGaussianSplattingInstance>, Instances)
SHADER_PARAMETER_SRV_ARRAY(StructuredBuffer<FGaussianSplat>, Models, [YaGS::GMaxModelCount])
END_SHADER_PARAMETER_STRUCT()

class FGaussianSplattingFilterInitCS final : public FGaussianSplattingComputeShaderBase {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingFilterInitCS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingFilterInitCS, FGaussianSplattingComputeShaderBase);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, /*YAGS_API*/)
    SHADER_PARAMETER_STRUCT_INCLUDE(FGaussianSplattingSceneParameters, SceneParameters)
    SHADER_PARAMETER(uint32, Count)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ElementIndicesOut)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, BitmasksOut)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, SumsOut)
    END_SHADER_PARAMETER_STRUCT()
};

class FGaussianSplattingFilterGatherIndicesCS final : public FGaussianSplattingComputeShaderBase {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingFilterGatherIndicesCS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingFilterGatherIndicesCS, FGaussianSplattingComputeShaderBase);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, /*YAGS_API*/)
    SHADER_PARAMETER(uint32, Count)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, BitmasksIn)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PrefixSumsIn)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ElementIndicesIn)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ElementIndicesOut)
    END_SHADER_PARAMETER_STRUCT()
};

class FGaussianSplattingFilterGatherCS final : public FGaussianSplattingComputeShaderBase {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingFilterGatherCS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingFilterGatherCS, FGaussianSplattingComputeShaderBase);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, /*YAGS_API*/)
    SHADER_PARAMETER_STRUCT_INCLUDE(FGaussianSplattingSceneParameters, SceneParameters)
    SHADER_PARAMETER(uint32, Count)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ElementIndicesIn)
    SHADER_PARAMETER_RDG_BUFFER_UAV_ARRAY(RWStructuredBuffer<FGaussianSplat>, ModelsOut, [YaGS::GMaxModelOutCount])
    SHADER_PARAMETER(FVector3f, SelectionOrigin)
    SHADER_PARAMETER(FMatrix3x4, DefaultModelRotationAndTranslation)
    SHADER_PARAMETER(FVector3f, DefaultModelScale)
    END_SHADER_PARAMETER_STRUCT()
};

class FGaussianSplattingPrepareSortKeyValuesCS final : public FGaussianSplattingComputeShaderBase {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingPrepareSortKeyValuesCS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingPrepareSortKeyValuesCS, FGaussianSplattingComputeShaderBase);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, /*YAGS_API*/)
    SHADER_PARAMETER_STRUCT_INCLUDE(FGaussianSplattingSceneParameters, SceneParameters)
    SHADER_PARAMETER(FMatrix3x4, ViewMatrix)
    SHADER_PARAMETER(uint32, Count)
    SHADER_PARAMETER(uint32, SortKeyMaskShift)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, SortKeys)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ElementIndices)
    END_SHADER_PARAMETER_STRUCT()
};

class FGaussianSplattingDepthCopyCS final : public FGlobalShader {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingDepthCopyCS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingDepthCopyCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, /*YAGS_API*/)
    SHADER_PARAMETER(FIntRect, InputRectMinAndSize)
    SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, InputDepth)
    SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputDepth)
    END_SHADER_PARAMETER_STRUCT()

    using FPermutationDomain = TShaderPermutationDomain<>;

    static FIntPoint GetGroupSize();

    static bool ShouldCompilePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );

    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment
    );

    static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );
};

BEGIN_SHADER_PARAMETER_STRUCT(FGaussianSplattingVSParameters, /*YAGS_API*/)
SHADER_PARAMETER_STRUCT_INCLUDE(FGaussianSplattingSceneParameters, SceneParameters)
SHADER_PARAMETER(FMatrix3x4, ViewMatrix)
SHADER_PARAMETER(FMatrix44f, ProjectionMatrix)
SHADER_PARAMETER(uint32, Count)
SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ElementIndices)
END_SHADER_PARAMETER_STRUCT()

class FGaussianSplattingVS final : public FGlobalShader {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingVS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingVS, FGlobalShader);

    using FParameters = FGaussianSplattingVSParameters;

    class FIsDepthPass : SHADER_PERMUTATION_BOOL("DEPTH_PASS");

    using FPermutationDomain = TShaderPermutationDomain<FIsDepthPass>;

    static bool ShouldCompilePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );

    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment
    );

    static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );
};

class FGaussianSplattingMS final : public FGlobalShader {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingMS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingMS, FGlobalShader);

    using FParameters = FGaussianSplattingVSParameters;

    using FIsDepthPass = FGaussianSplattingVS::FIsDepthPass;

    using FPermutationDomain = TShaderPermutationDomain<FIsDepthPass>;

    static bool ShouldCompilePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );

    static uint32 GetThreadGroupSize(const EShaderPlatform& ShaderPlatform);

    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment
    );

    static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );
};

class FGaussianSplattingAS final : public FGlobalShader {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingAS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingAS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, /*YAGS_API*/)
    SHADER_PARAMETER(uint32, Count)
    SHADER_PARAMETER(uint32, SortKeyMaskShift)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SortKeys)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );

    static uint32 GetThreadGroupSize(const EShaderPlatform& ShaderPlatform);

    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment
    );

    static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );
};

class FGaussianSplattingColorPS final : public FGlobalShader {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingColorPS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingColorPS, FGlobalShader);

    using FParameters = FRenderTargetParameters;

    class FShowDebugQuadBorder : SHADER_PERMUTATION_BOOL("SHOW_DEBUG_QUAD_BORDER");

    using FPermutationDomain = TShaderPermutationDomain<FShowDebugQuadBorder>;

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters &Parameters);

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters &Parameters,
                                             FShaderCompilerEnvironment &OutEnvironment);

    static EShaderPermutationPrecacheRequest
    ShouldPrecachePermutation(const FGlobalShaderPermutationParameters &Parameters);
};

class FGaussianSplattingDepthPS final : public FGlobalShader {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingDepthPS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingDepthPS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, /*YAGS_API*/)
    SHADER_PARAMETER(float, AlphaDepthClip)
    RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()

    class FEnableAlphaDepthClip : SHADER_PERMUTATION_BOOL("ENABLE_ALPHA_DEPTH_CLIP");

    using FPermutationDomain = TShaderPermutationDomain<FEnableAlphaDepthClip>;

    static bool ShouldCompilePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );

    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment
    );

    static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );
};

class FGaussianSplattingSceneBlendPS final : public FGlobalShader {
    DECLARE_GLOBAL_SHADER(FGaussianSplattingSceneBlendPS);

public:
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplattingSceneBlendPS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, /*YAGS_API*/)
    SHADER_PARAMETER(FScreenTransform, InputScreenTransform)
    SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputColor)
    SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputDepth)
    SHADER_PARAMETER_SAMPLER(SamplerState, Sampler)
    RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()

    class FDepthWrite : SHADER_PERMUTATION_BOOL("DEPTH_WRITE");

    using FPermutationDomain = TShaderPermutationDomain<FDepthWrite>;

    static bool ShouldCompilePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );

    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment
    );

    static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    );
};

BEGIN_SHADER_PARAMETER_STRUCT(FColorPassParameters, /*YAGS_API*/)
SHADER_PARAMETER_STRUCT_INCLUDE(FGaussianSplattingAS::FParameters, AS)
SHADER_PARAMETER_STRUCT_INCLUDE(FGaussianSplattingVSParameters, VS)
SHADER_PARAMETER_STRUCT_INCLUDE(FGaussianSplattingColorPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FDepthPassParameters, /*YAGS_API*/)
SHADER_PARAMETER_STRUCT_INCLUDE(FGaussianSplattingAS::FParameters, AS)
SHADER_PARAMETER_STRUCT_INCLUDE(FGaussianSplattingVSParameters, VS)
SHADER_PARAMETER_STRUCT_INCLUDE(FGaussianSplattingDepthPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

// mimic GPUSort.h::FGPUSortBuffers
BEGIN_SHADER_PARAMETER_STRUCT(FGPUSortBuffersParameters, /*YAGS_API*/)
SHADER_PARAMETER_RDG_BUFFER_SRV_ARRAY(Buffer<uint>, RemoteKeySRVs, [2])
SHADER_PARAMETER_RDG_BUFFER_UAV_ARRAY(RWBuffer<uint>, RemoteKeyUAVs, [2])
SHADER_PARAMETER_RDG_BUFFER_SRV_ARRAY(Buffer<uint>, RemoteValueSRVs, [2])
SHADER_PARAMETER_RDG_BUFFER_UAV_ARRAY(RWBuffer<uint>, RemoteValueUAVs, [2])
// SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, FirstValuesSRV)
// SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, FinalValuesUAV)
END_SHADER_PARAMETER_STRUCT()
