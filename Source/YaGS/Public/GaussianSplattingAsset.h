#pragma once

#include "GaussianSplattingFwd.h"

#include "Containers/DynamicRHIResourceArray.h"
#include "CoreMinimal.h"
#include "DynamicMeshBuilder.h"
#include "GaussianSplat.h"
#include "RenderCommandFence.h"
#include "UObject/NoExportTypes.h"

#include "GaussianSplattingAsset.generated.h"

// clang-format off
UCLASS(BlueprintType)
// clang-format on
class YAGS_API UGaussianSplattingAsset final : public UObject
{
    GENERATED_BODY()

public:
    struct FMesh
    {
        TArray<FVector> Vertices;
        TArray<int32> Indices;

        friend FArchive& operator <<(FArchive& Ar, FMesh& Mesh);
    };

    UGaussianSplattingAsset();
    ~UGaussianSplattingAsset();

    TSharedRef<FGaussianSplattingStaticBuffer> GetStaticBuffer() const;

    const TArray<FMesh>& GetConvexHulls() const&;
    void GetConvexHulls(TArray<FDynamicMeshVertex>& Vertices, TArray<uint32>& Indices) const;

    int32 GetMaxSHDegree() const;

    FString GetDescription() const;

    void LoadData(FGaussianSplattingStaticBuffer&& StaticBuffer, TArray<FMesh>&& ConvexHulls, int32 MaxSHDegree);
    bool LoadData(FGaussianSplats&& GaussianSplats, int32 MaxSHDegree);

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite
    )
    bool bShouldCollideWhenPlacing = true;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite
    )
    bool bIsSRGB = true;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite
    )
    bool bUseSphereShape = false;

    UPROPERTY(
        Category = "GaussianSplatting",
        EditAnywhere,
        BlueprintReadWrite
    )
    bool bPreferBoxShape = false;

private:
    FRenderCommandFence ReleaseResourcesFence;
    TSharedPtr<FGaussianSplattingStaticBuffer> StaticBuffer;
    TArray<FMesh> ConvexHulls;
    int32 MaxSHDegree = YaGS::GMaxSHOrder - 1;

    static bool CalculateConvexHull(const FGaussianSplats& GaussianSplats, FMesh& ConvexHull);

    void BeginInit();

    void BeginDestroy() override;
    bool IsReadyForFinishDestroy() override;
    void PostLoad() override;
    void Serialize(FArchive& Ar) override;
};
