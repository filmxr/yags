#pragma once

#include "GaussianSplattingFwd.h"

#include "CoreMinimal.h"

#include <type_traits>

template<typename AllocatorType>
class TGaussianSplattingByteAddressBufferView
{
public:
    using FData = TArray<uint8, AllocatorType>;

    class FSizeGuard
    {
    public:
        ~FSizeGuard()
        {
            const int32 Size = ByteAddressBuffer.GetOffset() - SizeOffset;
            ByteAddressBuffer.Set(SizeOffset, Size);
        }

    private:
        friend TGaussianSplattingByteAddressBufferView;

        TGaussianSplattingByteAddressBufferView& ByteAddressBuffer;
        const int32 SizeOffset;

        FSizeGuard(
            TGaussianSplattingByteAddressBufferView& InByteAddressBuffer
        )
            : ByteAddressBuffer{InByteAddressBuffer}
            , SizeOffset{ByteAddressBuffer.Skip<int32>()}
        {
        }
    };

    TGaussianSplattingByteAddressBufferView(
        FData& InData
    )
        : Data{InData}
    {
    }

    template<typename T>
    int32 Append(
        const T& Value
    )
    {
        TCheckType<T>{};
        static_assert((sizeof(T) % 4) == 0);
        const int32 Offset = Data.Num();
        Data.Append(reinterpret_cast<const uint8*>(&Value), StaticCast<int32>(sizeof Value));
        return Offset;
    }

    template<typename T>
    int32 Skip()
    {
        TCheckType<T>{};
        static_assert((sizeof(T) % 4) == 0);
        const int32 Offset = Data.Num();
        Data.AddUninitialized(StaticCast<int32>(sizeof(T)));
        return Offset;
    }

    template<typename T>
    void Set(
        int32 Offset, const T& Value
    )
    {
        TCheckType<T>{};
        static_assert((sizeof(T) % 4) == 0);
        checkSlow(Offset + StaticCast<int32>(sizeof Value) < Data.Num());
        *reinterpret_cast<T*>(Data.GetData() + Offset) = Value;
    }

    FSizeGuard GetSizeGuard() &
    {
        return {*this};
    }

    template<typename T, typename A>
    int32 Append(
        const TArray<T, A>& Values
    )
    {
        TCheckType<T>{};
        checkSlow((Values.NumBytes() % 4) == 0);
        const int32 Offset = Data.Num();
        Data.Append(reinterpret_cast<const uint8*>(Values.GetData()), StaticCast<int32>(Values.NumBytes()));
        return Offset;
    }

    int32 GetOffset() const
    {
        return Data.Num();
    }

    bool IsEmpty() const
    {
        return Data.IsEmpty();
    }

    template<typename T>
    static FData ToBytes(
        const T& Value
    )
    {
        FData OutData;
        Value.CopyToBytes(OutData);
        return OutData;
    }

public:
    FData& Data;

    template<typename T>
    struct TCheckType
    {
        static_assert(std::is_standard_layout_v<T>);
        static_assert(std::is_trivially_copyable_v<T>);
    };
};

template<typename AllocatorType>
TGaussianSplattingByteAddressBufferView(TArray<uint8, AllocatorType>& InData)
    -> TGaussianSplattingByteAddressBufferView<AllocatorType>;
