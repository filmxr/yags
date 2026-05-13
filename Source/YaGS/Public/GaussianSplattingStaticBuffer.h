#pragma once

#include "CoreMinimal.h"
#include "GaussianSplat.h"
#include "RHIUtilities.h"
#include "RenderResource.h"

class FGaussianSplattingStaticBuffer;

struct FGaussianSplattingResourceArrayUploadArrayView : public FResourceArrayUploadInterface
{
public:
    using SizeType = typename FGaussianSplats::SizeType;

    FGaussianSplattingResourceArrayUploadArrayView(
        FGaussianSplattingStaticBuffer* StaticBuffer, TConstArrayView<FGaussianSplat> View
    );

    const void* GetResourceData() const final;
    uint32 GetResourceDataSize() const final;
    void Discard() final;

private:
    FGaussianSplattingStaticBuffer* const StaticBuffer;
    const TConstArrayView<FGaussianSplat, SizeType> View;
};

class YAGS_API FGaussianSplattingStaticBuffer final : public FRenderResource
{
public:
    using SizeType = typename FGaussianSplats::SizeType;

    TArray<FRWBuffer> Buffers;

    FGaussianSplattingStaticBuffer(FString Name);

    void SetGaussianSplats(FGaussianSplats&& GaussianSplats);
    void SetNumElements(SizeType NumElements);
    SizeType GetNumElements() const;

    constexpr uint32 GetStride() const
    {
        return FGaussianSplats::GetTypeSize();
    }

    const FGaussianSplats& GetGaussianSplats() const&;
    void FetchGaussianSplats(FGaussianSplats& GaussianSplats) const;

    void Serialize(FArchive& Ar);

    FString GetFriendlyName() const override;
    void InitRHI(FRHICommandListBase& CmdList) override;
    void ReleaseRHI() override;

private:
    friend FGaussianSplattingResourceArrayUploadArrayView;

    FString Name;
    SizeType NumElements = 0;
    FGaussianSplats GaussianSplats;

    // RHI thread:
    SizeType ChunksToUpload = 0;
    TArray<FGaussianSplattingResourceArrayUploadArrayView> ResourceArrayViews;
};
