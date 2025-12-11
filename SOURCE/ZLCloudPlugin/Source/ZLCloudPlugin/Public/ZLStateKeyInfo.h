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

UCLASS(BlueprintType)
class ZLCLOUDPLUGIN_API UStateKeyInfoAsset : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "StateKeyData", BlueprintReadWrite)
	TMap<FString, FStateKeyInfo> KeyInfos;

    TSharedRef<FJsonObject> SerializeStateKeyAssetToJson();

    TSharedRef<FJsonObject> SerializeStateKeyAsset_JsonSchemaCompliant();
};