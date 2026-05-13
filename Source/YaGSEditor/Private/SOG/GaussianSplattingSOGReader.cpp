#include "SOG/GaussianSplattingSOGReader.h"

#include "GaussianSplattingCommon.h"
#include "GaussianSplattingEditorLog.h"

#include "FileUtilities/ZipArchiveReader.h"
#include "HAL/PlatformFileManager.h"
#include "Json.h"
#include "WebP.h"

namespace YaGS
{

namespace
{

double ToComp(
    double Value
)
{
    return (2.0 * Value / 255.0 - 1.0) * UE_DOUBLE_INV_SQRT_2;
}

double Unlog(
    double Value
)
{
    return FMath::Sign(Value) * (FMath::Exp(FMath::Abs(Value)) - 1.0);
}

}  // namespace

TArray<TTuple<FString, FString>> GetSOGExtensionsAndDescriptions()
{
    constexpr auto Description = TEXT("Gaussian splats in Bundled Spatial Ordered Gaussians Format");
    return {
        MakeTuple(TEXT("sog"), Description),
    };
}

bool ReadSOG(
    const FString& Filename, FGaussianSplats& GaussianSplatData, int32& MaxSHDegree
)
{
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("UGaussianSplattingAssetFactory::ReadSOG");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    FZipArchiveReader File{PlatformFile.OpenRead(*Filename)};
    const FString MetaJsonFilename = TEXT("meta.json");
    if (File.GetFileNames().Find(MetaJsonFilename) == INDEX_NONE)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot find '%s' in '%s' zip archive"), *MetaJsonFilename, *Filename);
        return false;
    }
    TArray<uint8> MetaJsonData;
    if (!File.TryReadFile(MetaJsonFilename, MetaJsonData))
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot read '%s' from '%s' zip archive"), *MetaJsonFilename, *Filename);
        return false;
    }
    const FUtf8StringView MetaJsonStringView =
        MakeStringView(reinterpret_cast<const UTF8CHAR*>(MetaJsonData.GetData()), MetaJsonData.Num());
    UE_LOG(LogYaGSEditor, Log, TEXT("'%s' contents: %s"), *MetaJsonFilename, *FString{MetaJsonStringView});
    TSharedRef<TJsonReader<UTF8CHAR>> MetaJsonReader = TJsonReaderFactory<UTF8CHAR>::CreateFromView(MetaJsonStringView);
    TSharedPtr<FJsonObject> MetaJsonObject;
    if (!FJsonSerializer::Deserialize(MetaJsonReader, MetaJsonObject) || !MetaJsonObject.IsValid())
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to deserialize '%s' JSON file"), *MetaJsonFilename);
        return false;
    }
    int32 Version = -1;
    if (!MetaJsonObject->TryGetNumberField(TEXT("version"), Version))
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("There is no 'version' field in '%s'"), *MetaJsonFilename);
        return false;
    }
    if (Version != 2)
    {
        UE_LOG(
            LogYaGSEditor,
            Warning,
            TEXT("SOG format of version %i is not supported. Only version 2 is currently supported"),
            Version
        );
        return false;
    }
    int32 GaussianSplatCount = -1;
    if (!MetaJsonObject->TryGetNumberField(TEXT("count"), GaussianSplatCount))
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("There is no 'count' field in '%s'"), *MetaJsonFilename);
        return false;
    }
    if (GaussianSplatCount < 1)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Gaussian splat count %i is not supported"), GaussianSplatCount);
        return false;
    }
    TArray<double> Mins;
    TArray<double> Maxs;
    const FString MeansLFilename = TEXT("means_l.webp");
    const FString MeansUFilename = TEXT("means_u.webp");
    {
        const TSharedPtr<FJsonObject>* MeansObject = nullptr;
        if (!MetaJsonObject->TryGetObjectField(TEXT("means"), MeansObject))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("There is no 'means' object in '%s'"), *MetaJsonFilename);
            return false;
        }
        {
            const TArray<TSharedPtr<FJsonValue>>* MinsArray = nullptr;
            if (!(*MeansObject)->TryGetArrayField(TEXT("mins"), MinsArray))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot get 'mins' array"));
                return false;
            }
            if (MinsArray->Num() != 3)
            {
                UE_LOG(
                    LogYaGSEditor, Warning, TEXT("Expected 3-element 'mins' array, got %i-element"), MinsArray->Num()
                );
                return false;
            }
            Mins.Reserve(3);
            for (const TSharedPtr<FJsonValue>& MinValue : *MinsArray)
            {
                if (!MinValue->TryGetNumber(Mins.Emplace_GetRef()))
                {
                    UE_LOG(LogYaGSEditor, Warning, TEXT("Expected 'mins' array of numbers"));
                    return false;
                }
            }
        }
        {
            const TArray<TSharedPtr<FJsonValue>>* MaxsArray = nullptr;
            if (!(*MeansObject)->TryGetArrayField(TEXT("maxs"), MaxsArray))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot get 'maxs' array"));
                return false;
            }
            if (MaxsArray->Num() != 3)
            {
                UE_LOG(
                    LogYaGSEditor, Warning, TEXT("Expected 3-element 'maxs' array, got %i-element"), MaxsArray->Num()
                );
                return false;
            }
            Maxs.Reserve(3);
            for (const TSharedPtr<FJsonValue>& MaxValue : *MaxsArray)
            {
                if (!MaxValue->TryGetNumber(Maxs.Emplace_GetRef()))
                {
                    UE_LOG(LogYaGSEditor, Warning, TEXT("Expected 'maxs' array of numbers"));
                    return false;
                }
            }
        }
        {
            const TArray<TSharedPtr<FJsonValue>>* FilesArray = nullptr;
            if (!(*MeansObject)->TryGetArrayField(TEXT("files"), FilesArray))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot get 'files' array"));
                return false;
            }
            if (FilesArray->Num() != 2)
            {
                UE_LOG(
                    LogYaGSEditor, Warning, TEXT("Expected 2-element 'files' array, got %i-element"), FilesArray->Num()
                );
                return false;
            }
            FString Filenames[2] = {(*FilesArray)[0]->AsString(), (*FilesArray)[1]->AsString()};
            if (Filenames[0] != MeansLFilename)
            {
                UE_LOG(
                    LogYaGSEditor, Warning, TEXT("Expected '%s' filename: got '%s'"), *MeansLFilename, *Filenames[0]
                );
                return false;
            }
            if (Filenames[1] != MeansUFilename)
            {
                UE_LOG(
                    LogYaGSEditor, Warning, TEXT("Expected '%s' filename: got '%s'"), *MeansUFilename, *Filenames[1]
                );
                return false;
            }
        }
    }
    TArray<double> ScalesCodebook;
    const FString ScalesFilename = TEXT("scales.webp");
    {
        const TSharedPtr<FJsonObject>* ScalesObject = nullptr;
        if (!MetaJsonObject->TryGetObjectField(TEXT("scales"), ScalesObject))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("There is no 'scales' object in '%s'"), *MetaJsonFilename);
            return false;
        }
        {
            const TArray<TSharedPtr<FJsonValue>>* CodebookArray = nullptr;
            if (!(*ScalesObject)->TryGetArrayField(TEXT("codebook"), CodebookArray))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot get 'codebook' array"));
                return false;
            }
            if (CodebookArray->Num() != 256)
            {
                UE_LOG(
                    LogYaGSEditor,
                    Warning,
                    TEXT("Expected %i-element 'codebook' array, got %i-element"),
                    256,
                    CodebookArray->Num()
                );
                return false;
            }
            ScalesCodebook.Reserve(256);
            for (const TSharedPtr<FJsonValue>& ScaleValue : *CodebookArray)
            {
                if (!ScaleValue->TryGetNumber(ScalesCodebook.Emplace_GetRef()))
                {
                    UE_LOG(LogYaGSEditor, Warning, TEXT("Expected 'codebook' array of numbers"));
                    return false;
                }
            }
        }
        {
            const TArray<TSharedPtr<FJsonValue>>* FilesArray = nullptr;
            if (!(*ScalesObject)->TryGetArrayField(TEXT("files"), FilesArray))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot get 'files' array"));
                return false;
            }
            if (FilesArray->Num() != 1)
            {
                UE_LOG(
                    LogYaGSEditor, Warning, TEXT("Expected 1-element 'files' array, got %i-element"), FilesArray->Num()
                );
                return false;
            }
            FString Filenames[] = {(*FilesArray)[0]->AsString()};
            if (Filenames[0] != ScalesFilename)
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Expected '%s' filename: got '%s'"), *ScalesFilename, *Filename);
                return false;
            }
        }
    }
    const FString QuatsFilename = TEXT("quats.webp");
    {
        const TSharedPtr<FJsonObject>* QuatsObject = nullptr;
        if (!MetaJsonObject->TryGetObjectField(TEXT("quats"), QuatsObject))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("There is no 'quats' object in '%s'"), *MetaJsonFilename);
            return false;
        }
        {
            const TArray<TSharedPtr<FJsonValue>>* FilesArray = nullptr;
            if (!(*QuatsObject)->TryGetArrayField(TEXT("files"), FilesArray))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot get 'files' array"));
                return false;
            }
            if (FilesArray->Num() != 1)
            {
                UE_LOG(
                    LogYaGSEditor, Warning, TEXT("Expected 1-element 'files' array, got %i-element"), FilesArray->Num()
                );
                return false;
            }
            FString Filenames[] = {(*FilesArray)[0]->AsString()};
            if (Filenames[0] != QuatsFilename)
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Expected '%s' filename: got '%s'"), *QuatsFilename, *Filename);
                return false;
            }
        }
    }
    TArray<double> SH0Codebook;
    const FString SH0Filename = TEXT("sh0.webp");
    {
        const TSharedPtr<FJsonObject>* SH0Object = nullptr;
        if (!MetaJsonObject->TryGetObjectField(TEXT("sh0"), SH0Object))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("There is no 'sh0' object in '%s'"), *MetaJsonFilename);
            return false;
        }
        {
            const TArray<TSharedPtr<FJsonValue>>* CodebookArray = nullptr;
            if (!(*SH0Object)->TryGetArrayField(TEXT("codebook"), CodebookArray))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot get 'codebook' array"));
                return false;
            }
            if (CodebookArray->Num() != 256)
            {
                UE_LOG(
                    LogYaGSEditor,
                    Warning,
                    TEXT("Expected %i-element 'codebook' array, got %i-element"),
                    256,
                    CodebookArray->Num()
                );
                return false;
            }
            SH0Codebook.Reserve(256);
            for (const TSharedPtr<FJsonValue>& SH0Value : *CodebookArray)
            {
                if (!SH0Value->TryGetNumber(SH0Codebook.Emplace_GetRef()))
                {
                    UE_LOG(LogYaGSEditor, Warning, TEXT("Expected 'codebook' array of numbers"));
                    return false;
                }
            }
        }
        {
            const TArray<TSharedPtr<FJsonValue>>* FilesArray = nullptr;
            if (!(*SH0Object)->TryGetArrayField(TEXT("files"), FilesArray))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot get 'files' array"));
                return false;
            }
            if (FilesArray->Num() != 1)
            {
                UE_LOG(
                    LogYaGSEditor, Warning, TEXT("Expected 1-element 'files' array, got %i-element"), FilesArray->Num()
                );
                return false;
            }
            FString Filenames[] = {(*FilesArray)[0]->AsString()};
            if (Filenames[0] != SH0Filename)
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Expected '%s' filename: got '%s'"), *SH0Filename, *Filename);
                return false;
            }
        }
    }
    int32 SHNCount = -1;
    MaxSHDegree = 0;
    TArray<double> SHNCodebook;
    const FString SHNCentroidsFilename = TEXT("shN_centroids.webp");
    const FString SHNLabelsFilename = TEXT("shN_labels.webp");
    const bool bHasSHN = MetaJsonObject->HasField(TEXT("shN"));
    if (bHasSHN)
    {
        const TSharedPtr<FJsonObject>* SHNObject = nullptr;
        if (!MetaJsonObject->TryGetObjectField(TEXT("shN"), SHNObject))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("There is no 'shN' object in '%s'"), *MetaJsonFilename);
            return false;
        }
        if (!(*SHNObject)->TryGetNumberField(TEXT("count"), SHNCount))
        {
            UE_LOG(
                LogYaGSEditor, Warning, TEXT("There is no 'count' field in 'shN' object in '%s'"), *MetaJsonFilename
            );
            return false;
        }
        if (SHNCount < 1)
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("SHNCount %i is not supported"), SHNCount);
            return false;
        }
        if (!(*SHNObject)->TryGetNumberField(TEXT("bands"), MaxSHDegree))
        {
            UE_LOG(
                LogYaGSEditor, Warning, TEXT("There is no 'bands' field in 'shN' object in '%s'"), *MetaJsonFilename
            );
            return false;
        }
        if ((MaxSHDegree < 1) || (MaxSHDegree > 3))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("MaxSHDegree %i is not supported"), MaxSHDegree);
            return false;
        }
        {
            const TArray<TSharedPtr<FJsonValue>>* CodebookArray = nullptr;
            if (!(*SHNObject)->TryGetArrayField(TEXT("codebook"), CodebookArray))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot get 'codebook' array"));
                return false;
            }
            if (CodebookArray->Num() != 256)
            {
                UE_LOG(
                    LogYaGSEditor,
                    Warning,
                    TEXT("Expected %i-element 'codebook' array, got %i-element"),
                    256,
                    CodebookArray->Num()
                );
                return false;
            }
            SHNCodebook.Reserve(256);
            for (const TSharedPtr<FJsonValue>& SHNValue : *CodebookArray)
            {
                if (!SHNValue->TryGetNumber(SHNCodebook.Emplace_GetRef()))
                {
                    UE_LOG(LogYaGSEditor, Warning, TEXT("Expected 'codebook' array of numbers"));
                    return false;
                }
            }
        }
        {
            const TArray<TSharedPtr<FJsonValue>>* FilesArray = nullptr;
            if (!(*SHNObject)->TryGetArrayField(TEXT("files"), FilesArray))
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot get 'files' array"));
                return false;
            }
            if (FilesArray->Num() != 2)
            {
                UE_LOG(
                    LogYaGSEditor, Warning, TEXT("Expected 2-element 'files' array, got %i-element"), FilesArray->Num()
                );
                return false;
            }
            const FString Filenames[2] = {(*FilesArray)[0]->AsString(), (*FilesArray)[1]->AsString()};
            if (Filenames[0] != SHNCentroidsFilename)
            {
                UE_LOG(
                    LogYaGSEditor,
                    Warning,
                    TEXT("Expected '%s' filename: got '%s'"),
                    *SHNCentroidsFilename,
                    *Filenames[0]
                );
                return false;
            }
            if (Filenames[1] != SHNLabelsFilename)
            {
                UE_LOG(
                    LogYaGSEditor, Warning, TEXT("Expected '%s' filename: got '%s'"), *SHNLabelsFilename, *Filenames[1]
                );
                return false;
            }
        }
    }
    auto MeansLImage = FWebPImageRGB::Read(File, MeansLFilename);
    if (!MeansLImage)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to read '%s' image"), *MeansLFilename);
        return false;
    }
    auto MeansUImage = FWebPImageRGB::Read(File, MeansUFilename);
    if (!MeansUImage)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to read '%s' image"), *MeansUFilename);
        return false;
    }
    auto QuatsImage = FWebPImageRGBA::Read(File, QuatsFilename);
    if (!QuatsImage)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to read '%s' image"), *QuatsFilename);
        return false;
    }
    auto ScalesImage = FWebPImageRGB::Read(File, ScalesFilename);
    if (!ScalesImage)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to read '%s' image"), *ScalesFilename);
        return false;
    }
    auto SH0Image = FWebPImageRGBA::Read(File, SH0Filename);
    if (!SH0Image)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to read '%s' image"), *SH0Filename);
        return false;
    }
    FWebPImageRGB SHNCentroidsImage;
    FWebPImageRGB SHNLabelsImage;
    int32 SHNCoeffNum = -1;
    TArray<float> SH;
    if (bHasSHN)
    {
        SHNCentroidsImage = FWebPImageRGB::Read(File, SHNCentroidsFilename);
        if (!SHNCentroidsImage)
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to read '%s' image"), *SHNCentroidsFilename);
            return false;
        }
        SHNLabelsImage = FWebPImageRGB::Read(File, SHNLabelsFilename);
        if (!SHNLabelsImage)
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to read '%s' image"), *SHNLabelsFilename);
            return false;
        }
        SHNCoeffNum = FMath::Square(MaxSHDegree + 1) - 1;
        SH.SetNumZeroed(SHNCoeffNum * 3);
    }
    GaussianSplatData.SetNumUninitialized(GaussianSplatCount);
    for (int32 Index = 0; Index < GaussianSplatCount; ++Index)
    {
        FQuat4d Orientation = {};
        {
            const WebP::FPixelRGBA& QuatPixel = QuatsImage[Index];
            if (QuatPixel.a < 252u)
            {
                Orientation = FQuat4d::Identity;
            }
            else
            {
                const uint8 Mode = QuatPixel.a - 252u;  // 0..3
                const double A = ToComp(QuatPixel.r);
                const double B = ToComp(QuatPixel.g);
                const double C = ToComp(QuatPixel.b);
                const double T = A * A + B * B + C * C;
                const double D = FMath::Sqrt(FMath::Max(0.0, 1.0 - T));
                switch (Mode)
                {
                    case 0:
                    {
                        Orientation = {A, B, C, D};
                        break;
                    }
                    case 1:
                    {
                        Orientation = {D, B, C, A};
                        break;
                    }
                    case 2:
                    {
                        Orientation = {B, D, C, A};
                        break;
                    }
                    case 3:
                    {
                        Orientation = {B, C, D, A};
                        break;
                    }
                    default:
                    {
                        checkNoEntry();
                    }
                }
            }
        }
        FVector3d Scale = {};
        {
            const WebP::FPixelRGB& ScalesPixel = ScalesImage[Index];
            Scale.X = FMath::Exp(ScalesCodebook[ScalesPixel.r]);
            Scale.Y = FMath::Exp(ScalesCodebook[ScalesPixel.g]);
            Scale.Z = FMath::Exp(ScalesCodebook[ScalesPixel.b]);
        }
        FVector3d Position = {};
        {
            const WebP::FPixelRGB& MeanLPixel = MeansLImage[Index];
            const WebP::FPixelRGB& MeanUPixel = MeansUImage[Index];
            const uint16 QuantizedX = MeanLPixel.r | (StaticCast<uint16>(MeanUPixel.r) << 8);
            const uint16 QuantizedY = MeanLPixel.g | (StaticCast<uint16>(MeanUPixel.g) << 8);
            const uint16 QuantizedZ = MeanLPixel.b | (StaticCast<uint16>(MeanUPixel.b) << 8);
            Position.X = Unlog(FMath::Lerp(Mins[0], Maxs[0], QuantizedX / 65535.0));
            Position.Y = Unlog(FMath::Lerp(Mins[1], Maxs[1], QuantizedY / 65535.0));
            Position.Z = Unlog(FMath::Lerp(Mins[2], Maxs[2], QuantizedZ / 65535.0));
        }
        FTransform3d Transform{
            Orientation,
            Position,
            /* Scale */ FVector3d::OneVector,
        };
        auto& GaussianSplat = GaussianSplatData[Index];
        GaussianSplat.Transform.Set(
            FMatrix44f{Transform.ToMatrixNoScale()}, YaGS::GGaussianSplatScale * FVector3f{Scale}
        );
        GaussianSplat.Normal = FVector3f::UpVector;  // omitted in the format
        {
            const WebP::FPixelRGBA& SH0Pixel = SH0Image[Index];
            FVector3d DiffuseColor{SH0Codebook[SH0Pixel.r], SH0Codebook[SH0Pixel.g], SH0Codebook[SH0Pixel.b]};
            GaussianSplat.AlbedoColor = YaGS::GSHCoeffData[0] * FVector3f{DiffuseColor} + 0.5f;
            GaussianSplat.AlbedoAlpha = SH0Pixel.a / 255.0f;
        }
        if (bHasSHN)
        {
            // playcanvas/splat-transform/src/readers/read-sog.ts
            const WebP::FPixelRGB& SHNLabel = SHNLabelsImage[Index];
            const uint16 CentroidIndex = SHNLabel.r | (StaticCast<uint16>(SHNLabel.g) << 8);
            if (CentroidIndex < SHNCount)
            {
                const int32 CentroidCoeffsBase = CentroidIndex * SHNCoeffNum;
                for (int32 SHNCoeffIndex = 0; SHNCoeffIndex < SHNCoeffNum; ++SHNCoeffIndex)
                {
                    const WebP::FPixelRGB& SHNCentroid = SHNCentroidsImage[CentroidCoeffsBase + SHNCoeffIndex];
                    SH[0 * SHNCoeffNum + SHNCoeffIndex] = StaticCast<float>(SHNCodebook[SHNCentroid.r]);
                    SH[1 * SHNCoeffNum + SHNCoeffIndex] = StaticCast<float>(SHNCodebook[SHNCentroid.g]);
                    SH[2 * SHNCoeffNum + SHNCoeffIndex] = StaticCast<float>(SHNCodebook[SHNCentroid.b]);
                }
                GaussianSplat.SetSH(SH);
            }
        }
    }
    return true;
}

}  // namespace YaGS
