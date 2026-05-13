#include "GaussianSplattingDebugReadback.h"
#include "GaussianSplattingLog.h"
#include "RHIGPUReadback.h"
#include "RenderGraphUtils.h"
#include "Tasks/Task.h"
#include <cinttypes>

namespace YaGS
{

namespace
{

TAutoConsoleVariable<bool> CVarYaGSDebugPrintfBufferEnable{
    TEXT("r." UE_MODULE_NAME ".DebugPrintfBuffer.Enable"),
    true,
    TEXT(" Enable logging of dumps in DebugPrintBuffer"),
    ECVF_RenderThreadSafe
};

}  // namespace

void DebugPrintBuffer(FRDGBuilder& GraphBuilder, FRDGBufferRef SourceBuffer)
{
    check(IsInRenderingThread());
    if (!CVarYaGSDebugPrintfBufferEnable.GetValueOnRenderThread())
    {
        return;
    }
    auto DebugReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("DebugReadback"));
    AddEnqueueCopyPass(GraphBuilder, DebugReadback.Get(), SourceBuffer, 0);
    auto SourceBufferPooled = GraphBuilder.ConvertToExternalBuffer(SourceBuffer);
    auto Continuation = [SourceBufferPooled = MoveTemp(SourceBufferPooled),
                         DebugReadback = MoveTemp(DebugReadback)](auto& Continuation) mutable -> void
    {
        if (DebugReadback && DebugReadback->IsReady())
        {
            const uint32 Size = DebugReadback->GetGPUSizeBytes();
            check(Size >= SourceBufferPooled->Desc.GetSize());
            UE_LOG(LogYaGS, Verbose, TEXT("Debug readback memory is ready. Size: %" PRIu64), Size);
            if (auto Data = StaticCast<const uint32*>(DebugReadback->Lock(SourceBufferPooled->Desc.GetSize())))
            {
                ON_SCOPE_EXIT
                {
                    DebugReadback->Unlock();
                    // clang-format off
                };
                // clang-format on
                FString HexDump, FltDump;
                const uint32 Count = SourceBufferPooled->Desc.NumElements;
                if (Count != 0)
                {
                    {
                        HexDump = FString::Printf(TEXT("%08x"), Data[0]);
                        const float FltValue = reinterpret_cast<const float&>(Data[0]);
                        FltDump = FString::Printf(TEXT("%f"), FltValue);
                    }
                    for (uint32 Index = 1; Index < Count; ++Index)
                    {
                        auto Delimiter = ((Index % 4) == 0) ? ((Index % 8) == 0) ? TEXT("\n") : TEXT(" | ") : TEXT(" ");
                        HexDump += FString::Printf(TEXT("%s%08x"), Delimiter, Data[Index]);
                        const float FltValue = reinterpret_cast<const float&>(Data[Index]);
                        FltDump += FString::Printf(TEXT("%s% 8.3g"), Delimiter, FltValue);
                    }
                }
                UE_LOG(LogYaGS, Log, TEXT("DEBUG HEX (Size %u):\n%s"), Count, *HexDump);
                UE_LOG(LogYaGS, Log, TEXT("DEBUG FLT (Size %u):\n%s"), Count, *FltDump);
            }
            else
            {
                UE_LOG(LogYaGS, Warning, TEXT("Cannot lock readback memory"));
            }
            DebugReadback.Reset();
        }
        if (DebugReadback)
        {
            UE::Tasks::Launch(
                UE_SOURCE_LOCATION,
                [Continuation = MoveTemp(Continuation)]() mutable
                {
                    Continuation(Continuation);
                },
                UE::Tasks::ETaskPriority::Default,
                UE::Tasks::EExtendedTaskPriority::RenderThreadNormalPri
            );
        }
    };
    Continuation(Continuation);
}

}  // namespace YaGS
