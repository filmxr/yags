#include "GaussianSplattingAsset.h"

#include "GaussianSplattingLog.h"
#include "GaussianSplattingStaticBuffer.h"

#include "CompGeom/ConvexHull3.h"
#include "DynamicMeshBuilder.h"

UGaussianSplattingAsset::UGaussianSplattingAsset()
    : StaticBuffer{MakeShared<FGaussianSplattingStaticBuffer>(TEXT(UE_MODULE_NAME ".StaticBuffer.Empty"))}
{
}

UGaussianSplattingAsset::~UGaussianSplattingAsset() = default;

FArchive& operator<<(
    FArchive& Ar, UGaussianSplattingAsset::FMesh& Mesh
)
{
    Mesh.Vertices.BulkSerialize(Ar);
    Mesh.Indices.BulkSerialize(Ar);
    return Ar;
}

TSharedRef<FGaussianSplattingStaticBuffer> UGaussianSplattingAsset::GetStaticBuffer() const
{
    return StaticBuffer.ToSharedRef();
}

auto UGaussianSplattingAsset::GetConvexHulls() const& -> const TArray<FMesh>&
{
    return ConvexHulls;
}

void UGaussianSplattingAsset::GetConvexHulls(TArray<FDynamicMeshVertex>& Vertices, TArray<uint32>& Indices) const
{
    int32 ConvexHullVerticesCount = 0;
    int32 ConvexHullIndicesCount = 0;
    for (const auto& [ConvexHullVertices, ConvexHullIndices] : ConvexHulls)
    {
        ConvexHullVerticesCount += ConvexHullVertices.Num();
        ConvexHullIndicesCount += ConvexHullIndices.Num();
    }
    Vertices.Reserve(ConvexHullVerticesCount);
    Indices.Reserve(ConvexHullIndicesCount);
    for (const auto& [ConvexHullVertices, ConvexHullIndices] : ConvexHulls)
    {
        const int32 Base = Vertices.Num();
        for (const int32& Index : ConvexHullIndices)
        {
            Indices.Add(StaticCast<uint32>(Base + Index));
        }
        for (const FVector& ConvexHullVertex : ConvexHullVertices)
        {
            Vertices.Add(FVector3f{ ConvexHullVertex });
        }
    }
}

int32 UGaussianSplattingAsset::GetMaxSHDegree() const
{
    return MaxSHDegree;
}

FString UGaussianSplattingAsset::GetDescription() const
{
    return FString::Printf(
        TEXT(UE_MODULE_NAME " asset (%i splats, max SH degree %i)"), StaticBuffer->GetNumElements(), MaxSHDegree
    );
}

void UGaussianSplattingAsset::LoadData(
    FGaussianSplattingStaticBuffer&& InStaticBuffer,
    TArray<FMesh>&& InConvexHulls,
    int32 InMaxSHDegree
)
{
    StaticBuffer = MakeShared<FGaussianSplattingStaticBuffer>(MoveTemp(InStaticBuffer));
    check(ConvexHulls.IsEmpty());
    ConvexHulls = MoveTemp(InConvexHulls);
    MaxSHDegree = InMaxSHDegree;
}

bool UGaussianSplattingAsset::LoadData(
    FGaussianSplats&& GaussianSplats, int32 InMaxSHDegree
)
{
    verifyf(GaussianSplats.Num() > 3, TEXT("Gaussian splats count: %i"), GaussianSplats.Num());
    FMesh ConvexHull;
    if (!CalculateConvexHull(GaussianSplats, ConvexHull))
    {
        return false;
    }
    StaticBuffer->SetGaussianSplats(MoveTemp(GaussianSplats));
    ConvexHulls.Add(MoveTemp(ConvexHull));
    MaxSHDegree = InMaxSHDegree;
    BeginInit();
    return true;
}

bool UGaussianSplattingAsset::CalculateConvexHull(
    const FGaussianSplats& GaussianSplats, FMesh& ConvexHull
)
{
    TFunctionRef<FVector(int32)> GetPointFunc = [&GaussianSplats](int32 Index)
    {
        return FVector{GaussianSplats[Index].Transform.GetTranslation()};
    };
    UE::Geometry::TConvexHull3<double> ConvexHullBuilder;
    if (!ConvexHullBuilder.Solve(GaussianSplats.Num(), MoveTemp(GetPointFunc)))
    {
        UE_LOG(LogYaGS, Warning, TEXT("Input is degenerate. Dimension: %i"), ConvexHullBuilder.GetDimension());
        return false;
    }
    const TArray<UE::Geometry::FIndex3i>& Triangles = ConvexHullBuilder.GetTriangles();
    TMap<int, int32> IndexMapping;
    IndexMapping.Reserve(Triangles.Num() * 3);
    for (const auto& VertexIndices : Triangles)
    {
        IndexMapping.FindOrAdd(VertexIndices.A, IndexMapping.Num());
        IndexMapping.FindOrAdd(VertexIndices.B, IndexMapping.Num());
        IndexMapping.FindOrAdd(VertexIndices.C, IndexMapping.Num());
    }
    ConvexHull.Indices.Reserve(IndexMapping.Num());
    for (const auto& VertexIndices : Triangles)
    {
        ConvexHull.Indices.Add(IndexMapping[VertexIndices.A]);
        ConvexHull.Indices.Add(IndexMapping[VertexIndices.B]);
        ConvexHull.Indices.Add(IndexMapping[VertexIndices.C]);
    }
    ConvexHull.Vertices.SetNum(IndexMapping.Num());
    for (const auto& [From, To] : IndexMapping)
    {
        ConvexHull.Vertices[To] = GetPointFunc(From);
    }
    return true;
}

void UGaussianSplattingAsset::BeginInit()
{
    FName Name{ GetPathName() };
    StaticBuffer->SetOwnerName(Name);
    BeginInitResource(StaticBuffer.Get());
}

void UGaussianSplattingAsset::BeginDestroy()
{
    BeginReleaseResource(StaticBuffer.Get());
    ReleaseResourcesFence.BeginFence();
    Super::BeginDestroy();
}

bool UGaussianSplattingAsset::IsReadyForFinishDestroy()
{
    return ReleaseResourcesFence.IsFenceComplete();
}

void UGaussianSplattingAsset::PostLoad()
{
    Super::PostLoad();
    BeginInit();
}

void UGaussianSplattingAsset::Serialize(FArchive& Ar)
{
    Super::Serialize(Ar);
    StaticBuffer->Serialize(Ar);
    Ar << ConvexHulls;
    Ar << MaxSHDegree;
}
