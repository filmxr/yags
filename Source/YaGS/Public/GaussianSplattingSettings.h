#pragma once

#include "Engine/DeveloperSettings.h"

#include "GaussianSplattingSettings.generated.h"

UCLASS(
    Config = Engine, DefaultConfig, meta = (DisplayName = "YaGS Settings")
)
class YAGS_API UGaussianSplattingSettings final : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    static bool GetPreferMeshShader();
    static bool GetShowDebugQuadBorder();
    static int32 GetSortBatchSizeLog2();
    static bool GetDepthWrite();
    static float GetAlphaDepthClip();

    void PostInitProperties() override;
#if WITH_EDITOR
    void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    // Syncs all UPROPERTY values to their corresponding TAutoConsoleVariables so
    // that render-thread readers always get a consistent snapshot.
    void SyncCVars() const;
    UPROPERTY(
        Category = "GaussianSplatting",
        Config,
        EditAnywhere,
        meta = (
            // ConsoleVariable = "r.YaGS.DepthWrite",  // not works properly
            DisplayName = "Prefer mesh shader over vertex shader",
            ToolTip = "Prefer to use mesh shader over vertex shader if possible",
            ConfigRestartRequired = false
        )
    )
    bool bPreferMeshShader = true;

    UPROPERTY(
        Category = "GaussianSplatting",
        Config,
        EditAnywhere,
        meta =
            (DisplayName = "Show debug quad border",
             ToolTip = "Show debug quad border colored by barycentrics",
             ConfigRestartRequired = false)
    )
    bool bShowDebugQuadBorder = false;

    UPROPERTY(
        Category = "GaussianSplatting",
        Config,
        EditAnywhere,
        meta =
            (DisplayName = "Log2 of sort batch size",
             ToolTip = "Log2 of batch size used to configure GS sorting",
             ConfigRestartRequired = false,
             UIMin = 0,     // keep in sync with
                            // FGaussianSplattingPrepareSortKeyValuesCS::FBatchSizeLog2::MinValue
             ClampMin = 0,  // keep in sync with
                            // FGaussianSplattingPrepareSortKeyValuesCS::FBatchSizeLog2::MinValue
             UIMax = 7,     // keep in sync with
                            // FGaussianSplattingPrepareSortKeyValuesCS::FBatchSizeLog2::MaxValue
             ClampMax = 7   // keep in sync with
                            // FGaussianSplattingPrepareSortKeyValuesCS::FBatchSizeLog2::MaxValue
            )
    )
    int32 SortBatchSizeLog2 = 7;

    UPROPERTY(
        Category = "GaussianSplatting",
        Config,
        EditAnywhere,
        meta =
            (DisplayName = "Enable depth write",
             ToolTip = "If checked, depth render target will be created and filled for GS",
             ConfigRestartRequired = false)
    )
    bool bDepthWrite = false;

    UPROPERTY(
        Category = "GaussianSplatting",
        Config,
        EditAnywhere,
        meta =
            (DisplayName = "Alpha depth clip",
             ToolTip = "Alpha clip value to clip depth write for a splat",
             ConfigRestartRequired = false,
             UIMin = 0.0f,
             ClampMin = 0.0f,
             UIMax = 1.0f,
             ClampMax = 1.0f)
    )
    float AlphaDepthClip = 0.0f;
};
