#include "GaussianSplattingSettings.h"

#include "GaussianSplattingShaders.h"

namespace
{

TAutoConsoleVariable<int32> CVarPreferMeshShader{
    TEXT("r.YaGS.PreferMeshShader"),
    1,
    TEXT("Prefer mesh shader over vertex shader if the platform supports it (0/1)"),
    ECVF_RenderThreadSafe
};

TAutoConsoleVariable<int32> CVarShowDebugQuadBorder{
    TEXT("r.YaGS.ShowDebugQuadBorder"),
    0,
    TEXT("Show debug quad border colored by barycentrics (0/1)"),
    ECVF_RenderThreadSafe
};

TAutoConsoleVariable<int32> CVarSortBatchSizeLog2{
    TEXT("r.YaGS.SortBatchSizeLog2"), 7, TEXT("Log2 of sort batch size (0-7)"), ECVF_RenderThreadSafe
};

TAutoConsoleVariable<int32> CVarDepthWrite{
    TEXT("r.YaGS.DepthWrite"), 0, TEXT("Enable depth write pass for gaussian splatting (0/1)"), ECVF_RenderThreadSafe
};

TAutoConsoleVariable<float> CVarAlphaDepthClip{
    TEXT("r.YaGS.AlphaDepthClip"),
    0.0f,
    TEXT("Alpha clip threshold for the depth write pass (0.0-1.0)"),
    ECVF_RenderThreadSafe
};

}  // namespace

void UGaussianSplattingSettings::PostInitProperties()
{
    Super::PostInitProperties();
    // Sync once the config has been loaded into UPROPERTY values.
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        SyncCVars();
    }
}

#if WITH_EDITOR
void UGaussianSplattingSettings::PostEditChangeProperty(
    FPropertyChangedEvent& PropertyChangedEvent
)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    SyncCVars();
}
#endif

void UGaussianSplattingSettings::SyncCVars() const
{
    CVarPreferMeshShader->Set(bPreferMeshShader ? 1 : 0, ECVF_SetByProjectSetting);
    CVarShowDebugQuadBorder->Set(bShowDebugQuadBorder ? 1 : 0, ECVF_SetByProjectSetting);
    CVarSortBatchSizeLog2->Set(
        FMath::Clamp(
            SortBatchSizeLog2,
            FGaussianSplattingPrepareSortKeyValuesCS::FBatchSizeLog2::MinValue,
            FGaussianSplattingPrepareSortKeyValuesCS::FBatchSizeLog2::MaxValue
        ),
        ECVF_SetByProjectSetting
    );
    CVarDepthWrite->Set(bDepthWrite ? 1 : 0, ECVF_SetByProjectSetting);
    CVarAlphaDepthClip->Set(AlphaDepthClip, ECVF_SetByProjectSetting);
}

bool UGaussianSplattingSettings::GetPreferMeshShader()
{
    return CVarPreferMeshShader.GetValueOnAnyThread() != 0;
}

bool UGaussianSplattingSettings::GetShowDebugQuadBorder()
{
    return CVarShowDebugQuadBorder.GetValueOnAnyThread() != 0;
}

int32 UGaussianSplattingSettings::GetSortBatchSizeLog2()
{
    return FMath::Clamp(
        CVarSortBatchSizeLog2.GetValueOnAnyThread(),
        FGaussianSplattingPrepareSortKeyValuesCS::FBatchSizeLog2::MinValue,
        FGaussianSplattingPrepareSortKeyValuesCS::FBatchSizeLog2::MaxValue
    );
}

bool UGaussianSplattingSettings::GetDepthWrite()
{
    return CVarDepthWrite.GetValueOnAnyThread() != 0;
}

float UGaussianSplattingSettings::GetAlphaDepthClip()
{
    return CVarAlphaDepthClip.GetValueOnAnyThread();
}
