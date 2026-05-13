#include "GaussianSplattingVolume.h"

#include "GaussianSplattingByteAddressBufferView.h"
#include "GaussianSplattingLog.h"
#include "GaussianSplattingShaders.h"

#include "Components/BrushComponent.h"
#include "Engine/BrushBuilder.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConvexElem.h"

AGaussianSplattingVolume::AGaussianSplattingVolume()
{
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
}

void AGaussianSplattingVolume::CopyToBytes(
    TGaussianSplattingByteAddressBufferView<> ByteAddressBuffer
) const
{
    check(IsInGameThread());
    auto SizeGuard = ByteAddressBuffer.GetSizeGuard();
    ByteAddressBuffer.Append(FGaussianSplattingVolume::Make(GetBrushComponent()->GetComponentTransform().ToInverseMatrixWithScale()));
    const FKAggregateGeom& AggGeom = GetBrushComponent()->GetBodySetup()->AggGeom;
    {
        FBoxSphereBounds BoxSphereBounds;
        AggGeom.CalcBoxSphereBounds(BoxSphereBounds, FTransform::Identity);
        ByteAddressBuffer.Append(FVector3f{ BoxSphereBounds.Origin });
        ByteAddressBuffer.Append(FVector3f{ BoxSphereBounds.BoxExtent });
        ByteAddressBuffer.Append(StaticCast<float>(BoxSphereBounds.SphereRadius));
    }
    {
        const TArray<FKConvexElem>& ConvexElems = AggGeom.ConvexElems;
        check(AggGeom.GetElementCount() == ConvexElems.Num());
        ByteAddressBuffer.Append<int32>(ConvexElems.Num());
        TArray<FPlane> Planes;
        for (const FKConvexElem& ConvexElem : ConvexElems)
        {
            if (!ConvexElem.GetTransform().Equals(FTransform::Identity))
            {
                UE_LOG(LogYaGS, Warning, TEXT("Non-identity transform encountered"));
            }
            ConvexElem.GetPlanes(Planes);
            ByteAddressBuffer.Append<int32>(Planes.Num());
            for (FPlane& Plane : Planes)
            {
                Plane.Normalize();
                FPlane4f OutPlane{ Plane };
                ByteAddressBuffer.Append(OutPlane.GetNormal());
                ByteAddressBuffer.Append(OutPlane.W);
            }
            Planes.Reset();
        }
    }
}

void AGaussianSplattingAppearanceVolume::CopyToBytes(
    TGaussianSplattingByteAddressBufferView<> ByteAddressBuffer
) const
{
    check(IsInGameThread());
    AGaussianSplattingVolume::CopyToBytes(ByteAddressBuffer);
    ByteAddressBuffer.Append<float>(FalloffDistance);
    ByteAddressBuffer.Append(Appearance.ToAppearance());
}
