#include "GaussianSplattingAssetExporter.h"

#include "GaussianSplattingAsset.h"
#include "GaussianSplattingEditorLog.h"
#include "GaussianSplattingStaticBuffer.h"
#include "PLY/GaussianSplattingPLYImportExport.h"
#include "PLY/GaussianSplattingPLYReader.h"

#include "Algo/TiedTupleOutput.h"
#include "Templates/Tuple.h"

namespace
{

// clang-format: off
const FAnsiStringView PropertiesHeader = ANSITEXTVIEW(R"ply(
property float x
property float y
property float z
property float nx
property float ny
property float nz
property float f_dc_0
property float f_dc_1
property float f_dc_2
)ply")
                                             .TrimStartAndEnd();

const FAnsiStringView PropertiesFooter = ANSITEXTVIEW(R"ply(
property float opacity
property float scale_0
property float scale_1
property float scale_2
property float rot_0
property float rot_1
property float rot_2
property float rot_3
)ply")
                                             .TrimStartAndEnd();
// clang-format: on

}  // namespace

UGaussianSplattingAssetExporter::UGaussianSplattingAssetExporter(
    const FObjectInitializer& ObjectInitializer
)
    : Super{ObjectInitializer}
{
    SupportedClass = UGaussianSplattingAsset::StaticClass();
    for (auto& ExtAndDescr : YaGS::GetPLYExtensionsAndDescriptions())
    {
        Algo::TieTupleAdd(FormatExtension, FormatDescription).Add(MoveTemp(ExtAndDescr));
    }
    Algo::TieTupleAdd(FormatExtension, FormatDescription).Add(MakeTuple(TEXT("ply"), FormatDescription[0]));
    PreferredFormatIndex = 0;
    bText = false;
    bForceFileOperations = true;
}

bool UGaussianSplattingAssetExporter::SupportsObject(
    UObject* Object
) const
{
    if (!Super::SupportsObject(Object))
    {
        return false;
    }
    bool bSupportsObject = false;
    if (auto Asset = Cast<UGaussianSplattingAsset>(Object))
    {
        bSupportsObject = (Asset->GetStaticBuffer()->GetNumElements() != 0);
    }
    return bSupportsObject;
}

bool UGaussianSplattingAssetExporter::ExportBinary(
    UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags
)
{
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("UGaussianSplattingAssetExporter::ExportBinary");
    auto Asset = CastChecked<UGaussianSplattingAsset>(Object);
    const int32 MaxSHDegree = Asset->GetMaxSHDegree();
    const int32 SHPropertyCount = 3 * (FMath::Square(MaxSHDegree + 1) - 1);
    const auto& StaticBuffer = *Asset->GetStaticBuffer();
    {
        const auto AddNextLine = [&Ar](FAnsiStringView Line)
        {
            Ar.Serialize(const_cast<ANSICHAR*>(Line.GetData()), GetNum(Line));
            ANSICHAR EndL = '\n';
            Ar.Serialize(&EndL, 1);
        };
        AddNextLine(YaGS::BeginHeader);
        AddNextLine("format binary_little_endian 1.0");
        {
            FAnsiString Element{"element vertex "};
            Element.AppendInt(StaticCast<int32>(StaticBuffer.GetNumElements()));
            AddNextLine(Element);
        }
        AddNextLine(PropertiesHeader);
        UE_LOG(LogYaGSEditor, Log, TEXT("SHPropertyCount = %i"), SHPropertyCount);
        if (MaxSHDegree > 0)
        {
            FAnsiString Property{"property float f_rest_"};
            const int32 Len = Property.Len();
            for (int32 Index = 0; Index < SHPropertyCount; ++Index)
            {
                Property.AppendInt(Index);
                AddNextLine(Property);
                Property.LeftInline(Len);
            }
        }
        AddNextLine(PropertiesFooter);
        AddNextLine(YaGS::EndHeader.LeftChop(1));
    }
    {
        const auto StoreGaussianSplats = [&](const FGaussianSplats& GaussianSplats)
        {
            TArray<float> SH;
            SH.SetNum(SHPropertyCount);
            FBox3f BoundingBox;
            for (const FGaussianSplat& GaussianSplat : GaussianSplats)
            {
                BoundingBox += GaussianSplat.Transform.GetTranslation();
            }
            FVector3f Origin = BoundingBox.Max;
            for (const FGaussianSplat& GaussianSplat : GaussianSplats)
            {
                FGaussianSplatPLY OutGaussianSplat = YaGS::ConvertGaussianSplat(GaussianSplat, SH);
                OutGaussianSplat.Position -= Origin;
                constexpr auto HeadSize = offsetof(FGaussianSplatPLY, Opacity);
                Ar.Serialize(&OutGaussianSplat, HeadSize);
                if (MaxSHDegree > 0)
                {
                    Ar.Serialize(GetData(SH), StaticCast<int64>(SH.GetTypeSize()) * GetNum(SH));
                }
                Ar.Serialize(&OutGaussianSplat.Opacity, sizeof OutGaussianSplat - HeadSize);
            }
        };
        if (!StaticBuffer.GetGaussianSplats().IsEmpty())
        {
            StoreGaussianSplats(StaticBuffer.GetGaussianSplats());
        }
        else
        {
            FGaussianSplats GaussianSplats;
            StaticBuffer.FetchGaussianSplats(GaussianSplats);
            StoreGaussianSplats(GaussianSplats);
        }
    }
    return true;
}
