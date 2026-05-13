#include "GaussianSplattingPLYReader.h"

#include "GaussianSplattingCommon.h"
#include "GaussianSplattingEditorLog.h"
#include "GaussianSplattingPLYImportExport.h"

#include "HAL/FileManager.h"

#include <charconv>
#include <cinttypes>
#include <type_traits>
#include <variant>

namespace YaGS
{

namespace
{

using TFieldFieldOffset = TPair<FVector3f FGaussianSplatPLY::*, float FVector3f::*>;
using TOffset = std::variant<float FGaussianSplatPLY::*, TFieldFieldOffset>;
using TFieldMap = TArray<TPair<int32 /*InputOffset*/, TOffset /*OutputOffset*/>>;

template<typename T>
bool FromChars(
    FAnsiStringView Str, T& Value
)
{
    const auto StrEnd = Str.GetData() + Str.Len();
    auto [Ptr, Ec] = std::from_chars(Str.GetData(), StrEnd, Value);
    if (Ec == std::errc::invalid_argument)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("'%hs' is invalid argument for FromChars"), *FAnsiString{Str});
        return false;
    }
    if (Ec == std::errc::result_out_of_range)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("'%hs' is out of range for int32"), *FAnsiString{Str});
        return false;
    }
    if (Ec != std::errc{})
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Error code: %i"), StaticCast<int32>(Ec));
        return false;
    }
    if (Ptr != StrEnd)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Extra characters at end of number '%hs'"), *FAnsiString{Str});
        return false;
    }
    return true;
}

bool ParsePLYHeader(
    FAnsiStringView Header,
    int32& GaussianSplatCount,
    int32& MaxSHDegree,
    TFieldMap& FieldMap,
    TArray<int32>& SHOffsets,
    TMap<FAnsiStringView, int32>& Properties,
    bool& HasDegreeAtEnd
)
{
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("UGaussianSplattingAssetFactory::ParsePLYHeader");
    check(MaxSHDegree >= 0);
    check(FieldMap.IsEmpty());
    check(SHOffsets.IsEmpty());
    check(Properties.IsEmpty());
    check(!HasDegreeAtEnd);
    const auto GetNextLine = [&Header]
    {
        int32 LineLen;
        if (!Header.FindChar('\n', LineLen))
        {
            checkNoEntry();
        }
        FAnsiStringView Line = Header.Left(LineLen);
        Header.RemovePrefix(LineLen + 1);
        return Line;
    };
    if (auto Line = GetNextLine(); Line != YaGS::BeginHeader)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Invalid PLY header first line: '%hs'"), *FAnsiString{Line});
        return false;
    }
    FAnsiStringView Format;
    constexpr auto ElementVertexPrefix = ANSITEXTVIEW("vertex ");
    constexpr auto ElementSHDegreePrefix = ANSITEXTVIEW("sh_degree ");
    FAnsiStringView Element;
    for (;;)
    {
        auto Line = GetNextLine();
        if (Line == YaGS::EndHeader.LeftChop(1))
        {
            check(Header.IsEmpty());
            break;
        }
        constexpr auto FormatPrefix = ANSITEXTVIEW("format ");
        constexpr auto CommentPrefix = ANSITEXTVIEW("comment ");
        constexpr auto ElementPrefix = ANSITEXTVIEW("element ");
        constexpr auto PropertyPrefix = ANSITEXTVIEW("property ");
        if (Line.StartsWith(FormatPrefix))
        {
            Line.RemovePrefix(FormatPrefix.Len());
            if (Line.IsEmpty())
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Wrong format line format: value is empty"));
                return false;
            }
            if (!Format.IsEmpty())
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Format line occurred twice"));
                return false;
            }
            Format = Line;
        }
        else if (Line.StartsWith(CommentPrefix))
        {
            Line.RemovePrefix(CommentPrefix.Len());
            UE_LOG(LogYaGSEditor, Log, TEXT("PLY comment: %hs"), *FAnsiString{Line});
        }
        else if (Line.StartsWith(ElementPrefix))
        {
            Line.RemovePrefix(ElementPrefix.Len());
            if (Line.StartsWith(ElementVertexPrefix))
            {
                Line.RemovePrefix(ElementVertexPrefix.Len());
                if (!Element.IsEmpty())
                {
                    UE_LOG(
                        LogYaGSEditor,
                        Warning,
                        TEXT(
                            "'element vertex' should be a first section: "
                            "'%hs'"
                        ),
                        *FAnsiString{Line}
                    );
                    return false;
                }
                Element = ElementVertexPrefix;
                if (!FromChars(Line, GaussianSplatCount))
                {
                    UE_LOG(
                        LogYaGSEditor,
                        Warning,
                        TEXT(
                            "'element vertex' should have a valid value: "
                            "'%hs'"
                        ),
                        *FAnsiString{Line}
                    );
                    return false;
                }
            }
            else if (Line.StartsWith(ElementSHDegreePrefix))
            {
                Line.RemovePrefix(ElementSHDegreePrefix.Len());
                if (Element != ElementVertexPrefix)
                {
                    UE_LOG(
                        LogYaGSEditor,
                        Warning,
                        TEXT(
                            "'element sh_degree' section should follow "
                            "'element vertex' section if any: '%hs'"
                        ),
                        *FAnsiString{Line}
                    );
                    return false;
                }
                Element = ElementSHDegreePrefix;
                int32 SHDegree = -1;
                if (!FromChars(Line, SHDegree))
                {
                    UE_LOG(
                        LogYaGSEditor,
                        Warning,
                        TEXT(
                            "'element sh_degree' should have a valid "
                            "value: '%hs'"
                        ),
                        *FAnsiString{Line}
                    );
                    return false;
                }
                if (SHDegree < 0)
                {
                    UE_LOG(
                        LogYaGSEditor,
                        Warning,
                        TEXT(
                            "Value of 'element sh_degree' should be "
                            "non-negative: '%i'"
                        ),
                        SHDegree
                    );
                    return false;
                }
            }
            else
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Unknown 'element': '%hs'"), *FAnsiString{Line});
                return false;
            }
        }
        else if (Line.StartsWith(PropertyPrefix))
        {
            Line.RemovePrefix(PropertyPrefix.Len());
            constexpr auto PropertyFloatPrefix = ANSITEXTVIEW("float ");
            if (Line.StartsWith(PropertyFloatPrefix))
            {
                Line.RemovePrefix(PropertyFloatPrefix.Len());
                if (Element == ElementVertexPrefix)
                {
                    if (Properties.Contains(Line))
                    {
                        UE_LOG(
                            LogYaGSEditor,
                            Warning,
                            TEXT(
                                "'property float %hs' encountered second "
                                "time"
                            ),
                            *FAnsiString{Line}
                        );
                        return false;
                    }
                    Properties.Emplace(Line, Properties.Num());
                }
                else if (Element == ElementSHDegreePrefix)
                {
                    if (Line != ANSITEXTVIEW("degree"))
                    {
                        UE_LOG(
                            LogYaGSEditor,
                            Warning,
                            TEXT(
                                "Only 'property float degree' is expected "
                                "in 'element sh_degree' section: '%hs'"
                            ),
                            *FAnsiString{Line}
                        );
                        return false;
                    }
                    if (HasDegreeAtEnd)
                    {
                        UE_LOG(
                            LogYaGSEditor,
                            Warning,
                            TEXT(
                                "'property float degree' encountered "
                                "second time"
                            )
                        );
                        return false;
                    }
                    HasDegreeAtEnd = true;
                }
                else
                {
                    checkNoEntry();
                }
            }
            else
            {
                UE_LOG(
                    LogYaGSEditor,
                    Warning,
                    TEXT(
                        "Only 'property float' are currently supported: "
                        "'%hs'"
                    ),
                    *FAnsiString{Line}
                );
                return false;
            }
        }
    }
    if (Format != ANSITEXTVIEW("binary_little_endian 1.0"))
    {
        if (Format.IsEmpty())
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("'format' have to be specified"));
        }
        else
        {
            UE_LOG(
                LogYaGSEditor,
                Warning,
                TEXT(
                    "Only 'format binary_little_endian 1.0' is currently "
                    "supported: '%hs'"
                ),
                *FAnsiString{Format}
            );
        }
        return false;
    }
    {
        const auto AddScalarOffset =
            [&Properties, &FieldMap]<typename T>(FAnsiStringView PropertyName, T FGaussianSplatPLY::* ScalarPtr) -> bool
        {
            if (const int32* PropertyOffset = Properties.Find(PropertyName))
            {
                FieldMap.Emplace(*PropertyOffset, TOffset{ScalarPtr});
                verify(Properties.Remove(PropertyName) != 0);
                return true;
            }
            return false;
        };
        const auto AddFieldOffset =
            [&Properties, &FieldMap]<typename T, typename F>(
                FAnsiStringView PropertyName, T FGaussianSplatPLY::* FieldPtr, F T::* FieldFieldPtr
            ) -> bool
        {
            if (const int32* PropertyOffset = Properties.Find(PropertyName))
            {
                FieldMap.Emplace(*PropertyOffset, TFieldFieldOffset{FieldPtr, FieldFieldPtr});
                verify(Properties.Remove(PropertyName) != 0);
                return true;
            }
            UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot add field '%hs'"), *FAnsiString{PropertyName});
            return false;
        };
        const auto AddVector3fOffsets =
            [&AddFieldOffset](FAnsiString PropertyName, auto Suffixes, FVector3f FGaussianSplatPLY::* FieldPtr) -> bool
        {
            int32 Index = 0;
            for (auto P : {&FVector3f::X, &FVector3f::Y, &FVector3f::Z})
            {
                if (!AddFieldOffset(PropertyName + Suffixes[Index++], FieldPtr, P))
                {
                    return false;
                }
            }
            return true;
        };

        if (!AddVector3fOffsets("", "xyz", &FGaussianSplatPLY::Position))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot add Position"));
            return false;
        }
        using TSuffixes = FAnsiStringView(&&)[3];
        if (!AddVector3fOffsets("n", ANSITEXTVIEW("xyz"), &FGaussianSplatPLY::Normal) &&
            !AddVector3fOffsets("n", TSuffixes{"xx", "y", "z"}, &FGaussianSplatPLY::Normal) &&
            !AddVector3fOffsets("normal_", ANSITEXTVIEW("012"), &FGaussianSplatPLY::Normal))
        {
            return false;
        }
        if (!AddVector3fOffsets("f_dc_", ANSITEXTVIEW("012"), &FGaussianSplatPLY::Color))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot add Color"));
            return false;
        }
        for (;;)
        {
            auto PropertyName = FAnsiString::Printf("f_rest_%i", SHOffsets.Num());
            if (const int32* PropertyOffset = Properties.Find(PropertyName))
            {
                SHOffsets.Push(*PropertyOffset);
                verify(Properties.Remove(PropertyName) != 0);
            }
            else
            {
                break;
            }
        }
        if (SHOffsets.Num() % 3 != 0)
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("%i is not multiple of 3"), SHOffsets.Num());
            return false;
        }
        {
            const int32 MaxSHOrderSqr = 1 + SHOffsets.Num() / 3;  // MaxSHOrder^2
            const int32 MaxSHOrder = StaticCast<int32>(FMath::Sqrt(StaticCast<double>(MaxSHOrderSqr)));
            if (FMath::Square(MaxSHOrder) != MaxSHOrderSqr)
            {
                UE_LOG(LogYaGSEditor, Warning, TEXT("Bad number of SH coefficients %i"), SHOffsets.Num());
                return false;
            }
            MaxSHDegree = FMath::Min(MaxSHDegree, MaxSHOrder - 1);
        }
        if (!AddScalarOffset("opacity", &FGaussianSplatPLY::Opacity))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot add Opacity"));
            return false;
        }
        if (!AddVector3fOffsets("scale_", ANSITEXTVIEW("012"), &FGaussianSplatPLY::Scale))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot add Scale"));
            return false;
        }
        if (!AddScalarOffset(ANSITEXTVIEW("rot_0"), &FGaussianSplatPLY::OrientationQuatRe))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot add OrientationQuatRe"));
            return false;
        }
        if (!AddVector3fOffsets("rot_", ANSITEXTVIEW("123"), &FGaussianSplatPLY::OrientationQuatIm))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot add OrientationQuatIm"));
            return false;
        }
    }
    for (const auto& Property : Properties)
    {
        UE_LOG(
            LogYaGSEditor,
            Warning,
            TEXT("Property with key %hs and value %i is ignored"),
            *FAnsiString{Property.Key},
            Property.Value
        );
    }
    Algo::SortBy(FieldMap, &TFieldMap::ElementType::Key);
    return true;
}

}  // namespace

TArray<TTuple<FString, FString>> GetPLYExtensionsAndDescriptions()
{
    constexpr auto Description = TEXT("Gaussian splats in Stanford Triangle Format");
    return {
        MakeTuple(TEXT("ply"), Description),
    };
}

bool ReadPLY(
    const FString& Filename, FGaussianSplats& GaussianSplatData, int32& MaxSHDegree
)
{
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("UGaussianSplattingAssetFactory::ReadPLY");
    TUniquePtr<FArchive> File{IFileManager::Get().CreateFileReader(*Filename)};
    if (!File)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Cannot read file '%s'"), *Filename);
        return false;
    }
    ON_SCOPE_EXIT
    {
        verify(File->Close());
        // clang-format off
    };
    // clang-format on
    int32 GaussianSplatCount = 0;
    TFieldMap FieldMap;
    TArray<int32> SHOffsets;
    TMap<FAnsiStringView, int32> Properties;
    bool bHasDegreeAtEnd = false;
    TArray<ANSICHAR> BinaryData;
    {
        int32 StartPosition = 0;
        int32 BinaryChunkSize = 1536;
        for (;;)
        {
            BinaryChunkSize = FMath::Min(BinaryChunkSize, File->TotalSize() - BinaryData.Num());
            if (BinaryChunkSize <= 0)
            {
                return false;
            }
            const int32 Num = BinaryData.AddUninitialized(BinaryChunkSize);
            File->Serialize(BinaryData.GetData() + Num, BinaryChunkSize);
            FAnsiStringView Header = MakeStringView(BinaryData.GetData(), BinaryData.Num());
            const int32 EndHeaderPosition = Header.Find(YaGS::EndHeader, StartPosition);
            if (EndHeaderPosition != INDEX_NONE)
            {
                Header.LeftInline(EndHeaderPosition + YaGS::EndHeader.Len());
                UE_LOG(LogYaGSEditor, Log, TEXT("PLY header (Len %i):\n%hs"), Header.Len(), *FAnsiString{Header});
                if (!ParsePLYHeader(
                        Header, GaussianSplatCount, MaxSHDegree, FieldMap, SHOffsets, Properties, bHasDegreeAtEnd
                    ))
                {
                    UE_LOG(LogYaGSEditor, Warning, TEXT("Failed to parse PLY header"));
                    return false;
                }
                if (GaussianSplatCount < 1)
                {
                    UE_LOG(
                        LogYaGSEditor, Warning, TEXT("Gaussian splat count %i is not supported"), GaussianSplatCount
                    );
                    return false;
                }
                if (MaxSHDegree < 0)
                {
                    UE_LOG(
                        LogYaGSEditor,
                        Warning,
                        TEXT(
                            "Negative MaxSHDegree values are not "
                            "supported: %i"
                        ),
                        MaxSHDegree
                    );
                    return false;
                }
                BinaryData.RemoveAt(0, Header.Len());
                break;
            }
            StartPosition = Header.Len() - YaGS::EndHeader.Len();
        }
    }
    const int32 SHNum = SHOffsets.Num();
    UE_LOG(
        LogYaGSEditor,
        Log,
        TEXT("GaussianSplatCount %i, MaxSHDegree %i, SHNum %i"),
        GaussianSplatCount,
        MaxSHDegree,
        SHNum
    );
    const int64 BinarySize =
        File->TotalSize() - File->Tell() + BinaryData.Num() - (bHasDegreeAtEnd ? sizeof(float) : 0);
    if (BinarySize < 0)
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("There is no room for sh_degree at the end of file: %" PRIi64), BinarySize);
        return false;
    }
    check(SHNum % 3 == 0);
    const int32 SHOrder = StaticCast<int32>(FMath::Sqrt(StaticCast<double>(1 + SHNum / 3)));
    check(FMath::Square(SHOrder) * 3 == 3 + SHNum);
    TArray<float> SH;
    SH.SetNumUninitialized(SHNum);
    const auto GaussianSplatSize =
        StaticCast<int64>(sizeof(FGaussianSplatPLY) + SH.NumBytes() + Properties.Num() * sizeof(float));
    if (BinarySize % GaussianSplatSize != 0)
    {
        UE_LOG(
            LogYaGSEditor,
            Warning,
            TEXT("%" PRIi64 " mod %" PRIi64 " == %" PRIi64),
            BinarySize,
            GaussianSplatSize,
            BinarySize % GaussianSplatSize
        );
        return false;
    }
    if (BinarySize / GaussianSplatSize > TNumericLimits<int32>::Max())
    {
        UE_LOG(
            LogYaGSEditor,
            Warning,
            TEXT("Too many splats to fit count in int32: %" PRIi64),
            BinarySize / GaussianSplatSize
        );
        return false;
    }
    if (GaussianSplatCount * GaussianSplatSize != BinarySize)
    {
        UE_LOG(
            LogYaGSEditor,
            Warning,
            TEXT(
                "There is no room or there is extra space for %i "
                "splats: " PRIi64 " != " PRIi64
            ),
            GaussianSplatCount,
            GaussianSplatCount * GaussianSplatSize,
            BinarySize
        );
        return false;
    }
    GaussianSplatData.Reserve(GaussianSplatCount);
    int32 ChunkSize = 1 << 17;
    while (GaussianSplatData.Num() < GaussianSplatCount)
    {
        ChunkSize = FMath::Min(ChunkSize, GaussianSplatCount - GaussianSplatData.Num());
        const int32 BinaryChunkSize = GaussianSplatSize * ChunkSize;
        if (BinaryChunkSize > BinaryData.Num())
        {
            const int32 Offset = BinaryData.AddUninitialized(BinaryChunkSize - BinaryData.Num());
            File->Serialize(BinaryData.GetData() + Offset, BinaryChunkSize - Offset);
        }
        const char* Src = BinaryData.GetData();
        for (int32 ChunkSplatIndex = 0; ChunkSplatIndex < ChunkSize; ++ChunkSplatIndex)
        {
            FGaussianSplatPLY GaussianSplatInput;
            const auto GetFieldRef = [&GaussianSplatInput]<typename T>(const T& Field) -> float&
            {
                if constexpr (std::is_same_v<T, float FGaussianSplatPLY::*>)
                {
                    return GaussianSplatInput.*Field;
                }
                else if constexpr (std::is_same_v<T, TFieldFieldOffset>)
                {
                    return GaussianSplatInput.*Field.Key.*Field.Value;
                }
                else
                {
                    static_assert(sizeof Field == 0);
                }
            };
            const auto FloatSrc = reinterpret_cast<const float*>(Src);
            for (const auto& Field : FieldMap)
            {
                std::visit(GetFieldRef, Field.Value) = FloatSrc[Field.Key];
            }
            {
                int32 Index = 0;
                for (int32 Offset : SHOffsets)
                {
                    SH[Index++] = FloatSrc[Offset];
                }
            }
            GaussianSplatData.Add(YaGS::ConvertGaussianSplat(GaussianSplatInput, SH));
            Src += GaussianSplatSize;
        }
        check(Src == BinaryData.GetData() + BinaryChunkSize);
        BinaryData.RemoveAt(0, BinaryChunkSize);
    }
    check(ChunkSize > 0);
    check(GaussianSplatData.Num() == GaussianSplatCount);
    if (bHasDegreeAtEnd)
    {
        float Degree = -1.0f;
        File->Serialize(&Degree, sizeof Degree);
        if (FMath::IsNaN(Degree))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("'sh_degree' is not a number"));
            return false;
        }
        if (!FMath::IsFinite(Degree))
        {
            UE_LOG(LogYaGSEditor, Warning, TEXT("'sh_degree' is not a finite number"));
            return false;
        }
        MaxSHDegree = FMath::Min(MaxSHDegree, StaticCast<int32>(Degree));
    }
    if (File->Tell() != File->TotalSize())
    {
        UE_LOG(LogYaGSEditor, Warning, TEXT("Extra space at end of file"));
        return false;
    }
    return true;
}

}  // namespace YaGS
