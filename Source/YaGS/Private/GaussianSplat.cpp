#include "GaussianSplat.h"

FArchive& operator<<(FArchive& Ar, FGaussianSplat& GaussianSplat)
{
    Ar.Serialize(&GaussianSplat, sizeof GaussianSplat);
    return Ar;
}
