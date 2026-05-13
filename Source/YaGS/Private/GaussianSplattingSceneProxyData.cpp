#include "GaussianSplattingSceneProxyData.h"

#include "GaussianSplattingByteAddressBufferView.h"
#include "GaussianSplattingStaticBuffer.h"
#include "GaussianSplattingVolume.h"

TArray<uint8> FGaussianSplattingSceneProxyData::ToBytes(
    const TArray<TSoftObjectPtr<AGaussianSplattingBooleanVolume>>& Volumes
)
{
    check(IsInGameThread());
    TArray<uint8> Data;
    TGaussianSplattingByteAddressBufferView<> ByteAddressBuffer{Data};
    for (const auto& InVolume : Volumes)
    {
        if (auto Volume = InVolume.Get())
        {
            Volume->CopyToBytes(ByteAddressBuffer);
        }
    }
    return Data;
}

TArray<uint8> FGaussianSplattingSceneProxyData::ToBytes(
    const TArray<TSoftObjectPtr<AGaussianSplattingAppearanceVolume>>& Volumes
)
{
    check(IsInGameThread());
    TArray<uint8> Data;
    TGaussianSplattingByteAddressBufferView<> ByteAddressBuffer{Data};
    for (const auto& InVolume : Volumes)
    {
        if (auto Volume = InVolume.Get())
        {
            Volume->CopyToBytes(ByteAddressBuffer);
        }
    }
    return Data;
}

bool FGaussianSplattingSceneProxyData::operator<(
    const FGaussianSplattingSceneProxyData& Rhs
) const
{
    return MakeTuple(RenderingOrder, StaticBuffer->GetNumElements()) <
           MakeTuple(Rhs.RenderingOrder, Rhs.StaticBuffer->GetNumElements());
}
