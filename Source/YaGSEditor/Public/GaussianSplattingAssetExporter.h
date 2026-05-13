#pragma once

#include "CoreMinimal.h"
#include "Exporters/Exporter.h"

#include "GaussianSplattingAssetExporter.generated.h"

UCLASS()
class YAGSEDITOR_API UGaussianSplattingAssetExporter final : public UExporter
{
    GENERATED_UCLASS_BODY()

    bool SupportsObject(UObject* Object) const override;
    bool ExportBinary(
        UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags
    ) override;
};
