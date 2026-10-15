// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ZLStateKeyInfo.generated.h"

UENUM(BlueprintType)
enum class EStateKeyDataType : uint8
{
    String,
    StringArray,
    Number,
    NumberArray,
    Bool,
    BoolArray,
    Invalid
};

USTRUCT(BlueprintType)
struct FStateKeyInfo
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    FString DataType = "String";

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    bool bLimitValues = false;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    FString DefaultStringValue;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    double DefaultNumberValue = 0.0f;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    bool DefaultBoolValue = false;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    TArray<FString> DefaultStringArray;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    TArray<double> DefaultNumberArray;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    TArray<bool> DefaultBoolArray;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    TArray<FString> AcceptedStringValues;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    TArray<double> AcceptedNumberValues;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    bool bIgnoredInDataHashes = false;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    bool bUseMinMax = false;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    double MinValue = 0.0;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    double MaxValue = 0.0;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    bool bAllowDynamicArraySize = false;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    int32 MaximumAdditionalModelSlots = 0;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    bool bAllowNullValue = false;

    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    bool bDefaultValueIsNull = false;

    // When true, dropdown / option widgets in the Debug UI display the description
    // (resolved via DIME model metadata) for each accepted value instead of the raw
    // value. The underlying value broadcast to the configuration remains the true value.
    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    bool bDisplayDescriptionAsOptions = false;

    // Relative cost of applying this field when batching cloud jobs. INDEX_NONE means
    // unset: environment defaults to 10, model defaults to 8, all other keys default to 0.
    UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
    int32 Complexity = INDEX_NONE;

    int32 GetEffectiveComplexity(const FString& Key) const;

    inline bool IsArray()
    {
        return DataType == "StringArray" || DataType == "NumberArray" || DataType == "BoolArray";
    }

    inline EStateKeyDataType GetDataTypeEnum() const
    {
        if (DataType == "String")
            return EStateKeyDataType::String;
        else if (DataType == "StringArray")
            return EStateKeyDataType::StringArray;
        else if (DataType == "Number")
            return EStateKeyDataType::Number;
        else if (DataType == "NumberArray")
            return EStateKeyDataType::NumberArray;
        else if (DataType == "Bool")
            return EStateKeyDataType::Bool;
        else if (DataType == "BoolArray")
            return EStateKeyDataType::BoolArray;
        else
            return EStateKeyDataType::Invalid;
    }
};

USTRUCT(BlueprintType)
struct FDIMEModelCodeMetadata
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "DIME", BlueprintReadWrite)
	FString Code;

	UPROPERTY(EditAnywhere, Category = "DIME", BlueprintReadWrite)
	FString Group;

	UPROPERTY(EditAnywhere, Category = "DIME", BlueprintReadWrite)
	int32 DescriptionId = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct FDIMEModelMetadata
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "DIME", BlueprintReadWrite)
	FString ModelName;

	UPROPERTY(EditAnywhere, Category = "DIME", BlueprintReadWrite)
	TMap<int32, FString> DescriptionLookupById;

	UPROPERTY(EditAnywhere, Category = "DIME", BlueprintReadWrite)
	TArray<FDIMEModelCodeMetadata> Codes;
};

inline bool ZLNaturalStringLessThan(const FString& Left, const FString& Right)
{
	const TCHAR* A = *Left;
	const TCHAR* B = *Right;

	while (*A != 0 && *B != 0)
	{
		if (FChar::IsDigit(*A) && FChar::IsDigit(*B))
		{
			int64 NumA = 0;
			int64 NumB = 0;
			while (FChar::IsDigit(*A))
			{
				NumA = (NumA * 10) + (*A - TEXT('0'));
				++A;
			}
			while (FChar::IsDigit(*B))
			{
				NumB = (NumB * 10) + (*B - TEXT('0'));
				++B;
			}
			if (NumA != NumB)
			{
				return NumA < NumB;
			}
			continue;
		}

		const TCHAR CharA = FChar::ToLower(*A);
		const TCHAR CharB = FChar::ToLower(*B);
		if (CharA != CharB)
		{
			return CharA < CharB;
		}

		++A;
		++B;
	}

	return *A == 0 && *B != 0;
}

UCLASS(BlueprintType)
class ZLCLOUDPLUGIN_API UStateKeyInfoAsset : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
	TMap<FString, FStateKeyInfo> KeyInfos;

	UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
	TArray<FDIMEModelMetadata> DimeModelData;

    static TSharedRef<FJsonObject> BuildJsonSchemaCompliantFromKeyInfos(const TMap<FString, FStateKeyInfo>& InKeyInfos, const FString& SchemaTitle);

    TSharedRef<FJsonObject> SerializeStateKeyAsset_JsonSchemaCompliant();

	static TSharedPtr<FJsonValue> SerializeDimeModelDataToJsonValue(const TArray<FDIMEModelMetadata>& InDimeModelData);
	static void DeserializeDimeModelDataFromJsonValue(const TSharedPtr<FJsonValue>& InValue, TArray<FDIMEModelMetadata>& OutData);
	static TSharedRef<FJsonObject> BuildZLSchemaFileObject(
		const TMap<FString, FStateKeyInfo>& InKeyInfos,
		const FString& SchemaTitle,
		const TArray<FDIMEModelMetadata>& InDimeModelData);
	TSharedRef<FJsonObject> SerializeStateKeyAsset_ZLSchemaFile();
};

namespace ZLStateKeyComplexity
{
	static constexpr int32 Unset = INDEX_NONE;
	static constexpr int32 DefaultNegligible = 0;
	static constexpr int32 Environment = 10;
	static constexpr int32 Model = 8;

	inline FString GetLeafKeyName(const FString& FullKey)
	{
		int32 DotIndex = INDEX_NONE;
		if (FullKey.FindLastChar(TEXT('.'), DotIndex))
		{
			return FullKey.Mid(DotIndex + 1);
		}
		return FullKey;
	}

	inline int32 GetDefaultForKey(const FString& FullKey)
	{
		const FString Leaf = GetLeafKeyName(FullKey);
		if (Leaf.Equals(TEXT("environment"), ESearchCase::IgnoreCase))
		{
			return Environment;
		}
		if (Leaf.Equals(TEXT("model"), ESearchCase::IgnoreCase))
		{
			return Model;
		}
		return DefaultNegligible;
	}

	inline int32 GetEffective(int32 StoredComplexity, const FString& FullKey)
	{
		return StoredComplexity == Unset ? GetDefaultForKey(FullKey) : StoredComplexity;
	}

	inline bool ShouldSerialize(int32 EffectiveComplexity, const FString& FullKey)
	{
		return EffectiveComplexity != DefaultNegligible || GetDefaultForKey(FullKey) != DefaultNegligible;
	}
}

inline int32 FStateKeyInfo::GetEffectiveComplexity(const FString& Key) const
{
	return ZLStateKeyComplexity::GetEffective(Complexity, Key);
}