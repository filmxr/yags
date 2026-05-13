#include "GaussianSplattingSceneProxy.h"

#include "GaussianSplattingAsset.h"
#include "GaussianSplattingComponent.h"
#include "GaussianSplattingSubsystem.h"

#include "Materials/MaterialRenderProxy.h"

FGaussianSplattingSceneProxy::FGaussianSplattingSceneProxy(
    UGaussianSplattingComponent* Component
)
    : FPrimitiveSceneProxy{Component}
#if WITH_EDITOR
    , BodySetup{Component->GetBodySetup()}
    , ConvexHullVertexFactory{GetScene().GetFeatureLevel(), "FGaussianSplattingSceneProxy"}
#endif
{
#if WITH_EDITOR
    check(Component->Asset);
    TArray<FDynamicMeshVertex> DynamicMeshVertices;
    Component->Asset->GetConvexHulls(DynamicMeshVertices, ConvexHullIndexBuffer.Indices);
    ConvexHullVertexBuffers.InitFromDynamicVertex(&ConvexHullVertexFactory, DynamicMeshVertices);
    BeginInitResource(&ConvexHullIndexBuffer);
#endif
}

bool FGaussianSplattingSceneProxy::IsVisible(
    const FSceneView& View
) const
{
    const bool bIsInScene = &GetScene() == View.Family->Scene;
    const bool bIsVisible = IsShown(&View) && bIsInScene;
#if WITH_EDITOR
    const FEngineShowFlags& Flags = View.Family->EngineShowFlags;
    const bool bIsWireframe = Flags.Wireframe;
    const bool bIsCollision =
        Flags.Collision ||
        Flags.CollisionPawn ||
        Flags.CollisionVisibility;
    return !bIsWireframe && !bIsCollision && bIsVisible;
#else
    return bIsVisible;
#endif
}

void FGaussianSplattingSceneProxy::CreateRenderThreadResources(
    FRHICommandListBase& CmdList
)
{
}

void FGaussianSplattingSceneProxy::DestroyRenderThreadResources()
{
#if WITH_EDITOR
    ConvexHullVertexFactory.ReleaseResource();
    ConvexHullIndexBuffer.ReleaseResource();
    ConvexHullVertexBuffers.PositionVertexBuffer.ReleaseResource();
    ConvexHullVertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
    ConvexHullVertexBuffers.ColorVertexBuffer.ReleaseResource();
#endif
}

uint32 FGaussianSplattingSceneProxy::GetMemoryFootprint() const
{
    return sizeof *this + GetAllocatedSize();
}

SIZE_T FGaussianSplattingSceneProxy::GetTypeHash() const
{
    static const char UniqueObject{};
    return PointerHash(&UniqueObject);
}

#if WITH_EDITOR
void FGaussianSplattingSceneProxy::GetDynamicMeshElements(
    const TArray<const FSceneView*>& Views,
    const FSceneViewFamily& ViewFamily,
    uint32 VisibilityMap,
    class FMeshElementCollector& Collector
) const
{
    check(GEngine);
    if (!GIsEditor)
    {
        return;
    }
    FTransform LocalToWorldTransform{GetLocalToWorld()};
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        if ((VisibilityMap & (1u << ViewIndex)) == 0)
        {
            continue;
        }
        const bool bDrawPawnCollision =
            ViewFamily.EngineShowFlags.CollisionPawn;
        const bool bIsCollisionView = AllowDebugViewmodes() && IsCollisionEnabled() && bDrawPawnCollision;
        if (bIsCollisionView)
        {
            FLinearColor SelectionColor =
                GetSelectionColor(FColor::Purple, IsSelected(), IsHovered());
            const bool bDrawCollisionOverlay =
                ViewFamily.EngineShowFlags.Collision;
            const bool bDrawSolid = !bDrawCollisionOverlay;
            TSoftObjectPtr<UMaterial> Material;
            if (bDrawSolid)
            {
                Material = GEngine->ShadedLevelColorationUnlitMaterial;
            }
            else
            {
                Material = GEngine->WireframeMaterial;
            }
            auto CollisionMaterialInstance =
                MakeUnique<FColoredMaterialRenderProxy>(
                    Material->GetRenderProxy(),
                    SelectionColor
                );
            BodySetup->AggGeom.GetAggGeom(
                LocalToWorldTransform,
                SelectionColor.ToFColor(/*bSRGB*/ false),
                CollisionMaterialInstance.Get(),
                /*bPerHullColor*/ false,
                bDrawSolid,
                AlwaysHasVelocity(),
                ViewIndex,
                Collector
            );
            Collector.RegisterOneFrameMaterialProxy(
                CollisionMaterialInstance.Release()
            );
        }
        else
        {
            const bool bIsWireframeView =
                AllowDebugViewmodes() &&
                ViewFamily.EngineShowFlags.Wireframe;
            TUniquePtr<FColoredMaterialRenderProxy> MaterialInstance;
            if (bIsWireframeView)
            {
                FLinearColor ViewWireframeColor =
                    (ViewFamily.EngineShowFlags.ActorColoration != 0)
                    ? GetPrimitiveColor()
                    : GetWireframeColor();
                MaterialInstance = MakeUnique<FColoredMaterialRenderProxy>(
                    GEngine->WireframeMaterial->GetRenderProxy(),
                    GetSelectionColor(
                        ViewWireframeColor,
                        IsSelected(),
                        IsHovered(),
                        /*bUseOverlayIntensity*/ false
                    )
                );
            }
            else
            {
                MaterialInstance = MakeUnique<FColoredMaterialRenderProxy>(
                    GEngine->GeomMaterial->GetRenderProxy(), FLinearColor::Transparent
                );
            }
            FMeshBatch& Mesh = Collector.AllocateMesh();
            Mesh.bDisableBackfaceCulling = true;
            Mesh.LODIndex = 0;
            Mesh.MaterialRenderProxy = MaterialInstance.Get();
            Mesh.VertexFactory = &ConvexHullVertexFactory;
            if (bIsWireframeView)
            {
                Mesh.bUseWireframeSelectionColoring = IsSelected();
                Mesh.bWireframe = true;
            }
            FMeshBatchElement& BatchElement = Mesh.Elements[0];
            BatchElement.FirstIndex = 0;
            BatchElement.IndexBuffer = &ConvexHullIndexBuffer;
            BatchElement.NumPrimitives = ConvexHullIndexBuffer.Indices.Num() / 3;
            Collector.AddMesh(ViewIndex, Mesh);
            Collector.RegisterOneFrameMaterialProxy(MaterialInstance.Release());
        }
    }
}

FPrimitiveViewRelevance FGaussianSplattingSceneProxy::GetViewRelevance(const FSceneView* View) const
{
    FPrimitiveViewRelevance Result;
    if (GIsEditor)
    {
        Result.bDrawRelevance = IsShown(View);
        Result.bDynamicRelevance = true;
        Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
        Result.bEditorStaticSelectionRelevance = IsSelected() || IsHovered();
    }
    return Result;
}
#endif
