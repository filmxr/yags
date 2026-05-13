#include "GaussianSplattingStaticBuffer.h"

#include "GaussianSplattingLog.h"

#include "Containers/ArrayView.h"
#include "RHIGPUReadback.h"
#include "RHIUtilities.h"

FGaussianSplattingResourceArrayUploadArrayView::FGaussianSplattingResourceArrayUploadArrayView(
    FGaussianSplattingStaticBuffer* InStaticBuffer, TConstArrayView<FGaussianSplat> InView
)
    : StaticBuffer{InStaticBuffer}
    , View{InView}
{
}

const void* FGaussianSplattingResourceArrayUploadArrayView::GetResourceData() const
{
    return View.GetData();
}

uint32 FGaussianSplattingResourceArrayUploadArrayView::GetResourceDataSize() const
{
    return StaticCast<uint32>(View.Num()) * View.GetTypeSize();
}

void FGaussianSplattingResourceArrayUploadArrayView::Discard()
{
    check(StaticBuffer->ChunksToUpload > 0);
    if (--StaticBuffer->ChunksToUpload == 0)
    {
        StaticBuffer->ResourceArrayViews.Empty();
        StaticBuffer->GaussianSplats.Empty();
    }
}

FGaussianSplattingStaticBuffer::FGaussianSplattingStaticBuffer(FString InName)
    : Name{ MoveTemp(InName) }
{
}

void FGaussianSplattingStaticBuffer::SetGaussianSplats(
    FGaussianSplats&& InGaussianSplats
)
{
    GaussianSplats = MoveTemp(InGaussianSplats);
    NumElements = GaussianSplats.Num();
}

void FGaussianSplattingStaticBuffer::SetNumElements(
    SizeType InNumElements
)
{
    NumElements = InNumElements;
}

auto FGaussianSplattingStaticBuffer::GetNumElements() const -> SizeType
{
    return NumElements;
}

const FGaussianSplats& FGaussianSplattingStaticBuffer::GetGaussianSplats() const&
{
    return GaussianSplats;
}

void FGaussianSplattingStaticBuffer::FetchGaussianSplats(
    FGaussianSplats& OutGaussianSplats
) const
{
    OutGaussianSplats.SetNumUninitialized(NumElements);
    auto ReadbackGaussianSplats = [this, &OutGaussianSplats](FRHICommandListImmediate& CmdList) -> void
    {
        check(Buffers.Num() == FMath::DivideAndRoundUp(NumElements, YaGS::GChunkLen));
        SizeType NumElementsRemaining = NumElements;
        for (const FRWBuffer& Buffer : Buffers)
        {
            const SizeType ChunkLen = FMath::Min(NumElementsRemaining, YaGS::GChunkLen);
            check(StaticCast<uint32>(ChunkLen) == Buffer.NumBytes / GetStride());
            FRHIGPUBufferReadback Readback{*Name};
            Readback.EnqueueCopy(CmdList, Buffer.Buffer, Buffer.NumBytes);
            CmdList.SubmitAndBlockUntilGPUIdle();
            check(Readback.IsReady());
            check(Readback.GetGPUSizeBytes() >= Buffer.NumBytes);
            if (auto Data = Readback.Lock(Buffer.NumBytes))
            {
                ON_SCOPE_EXIT
                {
                    Readback.Unlock();
                    // clang-format off
                };
                // clang-format on
                const SizeType Offset = NumElements - NumElementsRemaining;
                FMemory::Memcpy(OutGaussianSplats.GetData() + Offset, Data, Buffer.NumBytes);
            }
            else
            {
                UE_LOG(LogYaGS, Warning, TEXT("Cannot lock readback memory"));
            }
            NumElementsRemaining -= ChunkLen;
        }
        check(NumElementsRemaining == 0);
    };
    ENQUEUE_RENDER_COMMAND(FGaussianSplattingStaticBuffer_Serialize)(ReadbackGaussianSplats);
    FlushRenderingCommands();
}

void FGaussianSplattingStaticBuffer::Serialize(
    FArchive& Ar
)
{
    if (Ar.IsSaving() && !Buffers.IsEmpty() && GaussianSplats.IsEmpty())
    {
        FetchGaussianSplats(GaussianSplats);
        GaussianSplats.BulkSerialize(Ar);
        GaussianSplats.Empty();
    }
    else
    {
        if (GaussianSplats.IsEmpty())
        {
            UE_LOG(LogYaGS, Warning, TEXT("GaussianSplats is empty"));
        }
        GaussianSplats.BulkSerialize(Ar);
    }
}

FString FGaussianSplattingStaticBuffer::GetFriendlyName() const
{
    return Name;
}

void FGaussianSplattingStaticBuffer::InitRHI(FRHICommandListBase& CmdList)
{
    if (!Buffers.IsEmpty())
    {
        if (!GaussianSplats.IsEmpty())
        {
            UE_LOG(LogYaGS, Warning, TEXT("GaussianSplats is not yet empty"));
        }
        return;
    }
    if (!GaussianSplats.IsEmpty())
    {
        if (NumElements != 0)
        {
            UE_LOG(LogYaGS, Warning, TEXT("NumElements is not 0: %u"), NumElements);
        }
        NumElements = GaussianSplats.NumBytes() / GetStride();
    }
    if (NumElements == 0)
    {
        UE_LOG(LogYaGS, Error, TEXT("NumElements is 0"));
        return;
    }
    constexpr EBufferUsageFlags AdditionalUsage =
        EBufferUsageFlags::Static |
        EBufferUsageFlags::StructuredBuffer |
        EBufferUsageFlags::SourceCopy;
    check(ResourceArrayViews.IsEmpty());
    check(ChunksToUpload == 0);
    ChunksToUpload = FMath::DivideAndRoundUp(GaussianSplats.Num(), YaGS::GChunkLen);
    if (!GaussianSplats.IsEmpty())
    {
        ResourceArrayViews.Reserve(ChunksToUpload);
    }
    Buffers.Reserve(ChunksToUpload);
    SizeType NumElementsRemaining = NumElements;
    while (NumElementsRemaining != 0)
    {
        const SizeType ChunkLen = FMath::Min(NumElementsRemaining, YaGS::GChunkLen);
        FGaussianSplattingResourceArrayUploadArrayView* ResourceArrayView = nullptr;
        if (!GaussianSplats.IsEmpty())
        {
            const SizeType Offset = NumElements - NumElementsRemaining;
            TConstArrayView<FGaussianSplat, SizeType> Chunk{GaussianSplats.GetData() + Offset, ChunkLen};
            ResourceArrayView = &ResourceArrayViews.Emplace_GetRef(this, Chunk);
        }
        auto& Buffer = Buffers.AddDefaulted_GetRef();
        Buffer.ClassName = TEXT(UE_MODULE_NAME ".StaticBuffer");
        Buffer.OwnerName = GetOwnerName();
        Buffer.NumBytes = StaticCast<uint32>(ChunkLen) * GetStride();

        constexpr auto Usage = BUF_VertexBuffer | BUF_UnorderedAccess | BUF_ShaderResource | AdditionalUsage;
        auto CreateDesc =
            FRHIBufferCreateDesc::Create(*GetFriendlyName(), Buffer.NumBytes, RHI_RAW_VIEW_ALIGNMENT, Usage)
                .SetInitialState(RHIGetDefaultResourceState(Usage, ResourceArrayView != nullptr))
                .SetClassName(Buffer.ClassName)
                .SetOwnerName(Buffer.OwnerName);
        if (ResourceArrayView)
        {
            CreateDesc.SetInitActionResourceArray(ResourceArrayView);
        }
        Buffer.Buffer = CmdList.CreateBuffer(CreateDesc);
        {
            auto Desc = FRHIViewDesc::CreateBufferUAV();
            Desc.SetTypeFromBuffer(Buffer.Buffer);
            Desc.SetStride(GetStride());
            Desc.SetNumElements(Buffer.Buffer->GetSize() / GetStride());
            Buffer.UAV = CmdList.CreateUnorderedAccessView(Buffer.Buffer, Desc);
        }
        {
            auto Desc = FRHIViewDesc::CreateBufferSRV();
            Desc.SetTypeFromBuffer(Buffer.Buffer);
            Desc.SetStride(GetStride());
            Desc.SetNumElements(Buffer.Buffer->GetSize() / GetStride());
            Buffer.SRV = CmdList.CreateShaderResourceView(Buffer.Buffer, Desc);
        }
        NumElementsRemaining -= ChunkLen;
    }
}

void FGaussianSplattingStaticBuffer::ReleaseRHI()
{
    for (auto& Buffer : Buffers)
    {
        Buffer.Release();
    }
    Buffers.Empty();
}
