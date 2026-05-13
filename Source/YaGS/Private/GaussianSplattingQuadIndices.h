#pragma once

#include "RenderResource.h"
#include "RHIResources.h"

class FGaussianSplattingQuadIndices : public FIndexBuffer
{
public:
    using FIndexBuffer::FIndexBuffer;

    FShaderResourceViewRHIRef ShaderResourceViewRHI;

private:
    void InitRHI(FRHICommandListBase& CmdList) final;
    void ReleaseRHI() final;
};

namespace YaGS
{

extern TGlobalResource<FGaussianSplattingQuadIndices> GQuadIndices;

}  // namespace YaGS
