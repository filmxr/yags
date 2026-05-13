#include "GaussianSplattingQuadIndices.h"
#include "RHIResourceUtils.h"
#include "GaussianSplattingCommon.h"

namespace YaGS
{

TGlobalResource<FGaussianSplattingQuadIndices> GQuadIndices;

}  // namespace YaGS

void FGaussianSplattingQuadIndices::InitRHI(FRHICommandListBase& CmdList)
{
    const uint16 QuadIndexData[] = { 0, 1, 2, 3, 2, 1 };
    static_assert(GetNum(QuadIndexData) == 3 * YaGS::GTriangleCountPerQuad);
    IndexBufferRHI = UE::RHIResourceUtils::CreateIndexBufferFromArray(
        CmdList,
        TEXT(UE_MODULE_NAME ".QuadIndices"),
        EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource,
        MakeConstArrayView(QuadIndexData)
    );
    auto ShaderResourceViewDesc =
        FRHIViewDesc::CreateBufferSRV().SetFormat(PF_R16_UINT).SetTypeFromBuffer(IndexBufferRHI);
    ShaderResourceViewRHI = CmdList.CreateShaderResourceView(IndexBufferRHI, ShaderResourceViewDesc);
}

void FGaussianSplattingQuadIndices::ReleaseRHI()
{
    ShaderResourceViewRHI.SafeRelease();
    FIndexBuffer::ReleaseRHI();
}
