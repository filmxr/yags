#include "GaussianSplattingComponent.h"

#include "GaussianSplattingAsset.h"
#include "GaussianSplattingSceneProxy.h"
#include "GaussianSplattingSceneProxyData.h"
#include "GaussianSplattingSubsystem.h"
#include "GaussianSplattingVolume.h"

#include "Components/BrushComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/SphereElem.h"
#include "RenderingThread.h"

FString UGaussianSplattingComponent::GetName() const
{
#if WITH_EDITOR
    return GetOwner()->GetActorLabel();
#else
    return GetOwner()->GetName();
#endif
}

UBodySetup* UGaussianSplattingComponent::GetBodySetup()
{
    if (BodySetup)
    {
        return BodySetup;
    }
    if (!Asset)
    {
        return nullptr;
    }
    FKAggregateGeom AggregateGeom;
    for (const auto& [ConvexHullVertices, ConvexHullIndices] : Asset->GetConvexHulls())
    {
        if (Asset->bUseSphereShape)
        {
            FBox AABB{ ConvexHullVertices };
            FVector Center = AABB.GetCenter();
            if (Asset->bPreferBoxShape)
            {
                FKBoxElem& BoxElems = AggregateGeom.BoxElems.AddDefaulted_GetRef();
                BoxElems.Center = Center;
                BoxElems.Rotation = FRotator::ZeroRotator;
                FVector Extent = AABB.GetExtent();
                BoxElems.X = Extent.X;
                BoxElems.Y = Extent.Y;
                BoxElems.Z = Extent.Z;
            }
            else
            {
                double MaxDistanceSquared = 0.0;
                for (const FVector& Vertex : ConvexHullVertices)
                {
                    MaxDistanceSquared = FMath::Max(MaxDistanceSquared, FVector::DistSquared(Center, Vertex));
                }
                FKSphereElem& SphereElem = AggregateGeom.SphereElems.AddDefaulted_GetRef();
                SphereElem.Radius = StaticCast<float>(FMath::Sqrt(MaxDistanceSquared));
                SphereElem.Center = Center;
            }
        }
        else
        {
            FKConvexElem& ConvexElem = AggregateGeom.ConvexElems.AddDefaulted_GetRef();
            ConvexElem.VertexData = ConvexHullVertices;
            ConvexElem.IndexData = ConvexHullIndices;
            ConvexElem.UpdateElemBox();
        }
    }
    BodySetup = NewObject<UBodySetup>();
    BodySetup->AddCollisionFrom(AggregateGeom);
    return BodySetup;
}

FGaussianSplattingSceneProxyData UGaussianSplattingComponent::ToSceneProxyData() const
{
    check(Asset);
    FGaussianSplattingSceneProxyData SceneProxyData = {
        .Name = GetName(),
        .RenderingOrder = RenderingOrder,
        .LocalToWorld = GetRenderMatrix(),
        .BoundingBox = Bounds.GetBox(),
        .StaticBuffer = Asset->GetStaticBuffer(),
        .bIsSRGB = Asset->bIsSRGB,
        .Scale = Scale,
        .MaxLambda = MaxLambda,
        .Appearance = Appearance.ToAppearance(),
        .Crops = FGaussianSplattingSceneProxyData::ToBytes(Crops),
        .Culls = FGaussianSplattingSceneProxyData::ToBytes(Culls),
        .LocalAppearances = FGaussianSplattingSceneProxyData::ToBytes(LocalAppearances),
    };
    int32& MaxSHDegree = SceneProxyData.Appearance.MaxSHDegree;
    MaxSHDegree = FMath::Min(MaxSHDegree, Asset->GetMaxSHDegree());
    return SceneProxyData;
}

void UGaussianSplattingComponent::OnRegister()
{
    Super::OnRegister();
    if (auto World = GetWorld())
    {
        if (auto Subsystem = World->GetSubsystem<UGaussianSplattingSubsystem>())
        {
            Subsystem->RegisterComponent(MakeWeakObjectPtr(this));
        }
    }
}

void UGaussianSplattingComponent::OnUnregister()
{
    if (auto World = GetWorld())
    {
        if (auto Subsystem = World->GetSubsystem<UGaussianSplattingSubsystem>())
        {
            Subsystem->UnregisterComponent(MakeWeakObjectPtr(this));
        }
    }
    Super::OnUnregister();
}

FPrimitiveSceneProxy* UGaussianSplattingComponent::CreateSceneProxy()
{
    if (!Asset)
    {
        return nullptr;
    }
    return new FGaussianSplattingSceneProxy{this};
}

FBoxSphereBounds UGaussianSplattingComponent::CalcBounds(
    const FTransform& LocalToWorld
) const
{
    if (!BodySetup)
    {
        return Super::CalcBounds(LocalToWorld);
    }
    FBoxSphereBounds BoxSphereBounds;
    BodySetup->AggGeom.CalcBoxSphereBounds(BoxSphereBounds, LocalToWorld);
    return BoxSphereBounds;
}

#if WITH_EDITOR

void UGaussianSplattingComponent::GetUsedMaterials(
    TArray<UMaterialInterface*>& OutMaterials,
    bool bGetDebugMaterials
) const
{
    Super::GetUsedMaterials(OutMaterials, bGetDebugMaterials);
    if (!bGetDebugMaterials)
    {
        return;
    }
    check(GEngine);
    OutMaterials.Add(GEngine->GeomMaterial);
    OutMaterials.Add(GEngine->ShadedLevelColorationUnlitMaterial);
    OutMaterials.Add(GEngine->WireframeMaterial);
}

bool UGaussianSplattingComponent::ShouldCollideWhenPlacing() const
{
    return Asset && Asset->bShouldCollideWhenPlacing;
}

#endif
