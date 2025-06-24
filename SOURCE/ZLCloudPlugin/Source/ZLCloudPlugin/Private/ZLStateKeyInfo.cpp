//Copyright ZeroLight ltd.All Rights Reserved.

#include "ZLStateKeyInfo.h"

TSharedRef<FJsonObject> UStateKeyInfoAsset::SerializeStateKeyAssetToJson()
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> SchemaData = MakeShared<FJsonObject>();

	for (const TPair<FString, FStateKeyInfo>& Pair : KeyInfos)
	{
		const FString& DottedKey = Pair.Key;
		const FStateKeyInfo& Info = Pair.Value;

		TSharedRef<FJsonObject> EntryObject = MakeShared<FJsonObject>();
		EntryObject->SetStringField(TEXT("DataType"), Info.DataType);
		EntryObject->SetBoolField(TEXT("bLimitValues"), Info.bLimitValues);

		switch (Info.GetDataTypeEnum())
		{
		case EStateKeyDataType::String:
			EntryObject->SetStringField(TEXT("DefaultValue"), Info.DefaultStringValue);
			if (Info.bLimitValues)
			{
				TArray<TSharedPtr<FJsonValue>> JsonArray;
				for (const FString& Val : Info.AcceptedStringValues)
					JsonArray.Add(MakeShared<FJsonValueString>(Val));
				EntryObject->SetArrayField(TEXT("AcceptedValues"), JsonArray);
			}
			break;

		case EStateKeyDataType::Number:
			EntryObject->SetNumberField(TEXT("DefaultValue"), Info.DefaultNumberValue);
			if (Info.bLimitValues)
			{
				TArray<TSharedPtr<FJsonValue>> JsonArray;
				for (double Val : Info.AcceptedNumberValues)
					JsonArray.Add(MakeShared<FJsonValueNumber>(Val));
				EntryObject->SetArrayField(TEXT("AcceptedValues"), JsonArray);
			}
			break;

		case EStateKeyDataType::Bool:
			EntryObject->SetBoolField(TEXT("DefaultValue"), Info.DefaultBoolValue);
			break;

		case EStateKeyDataType::StringArray:
		{
			TArray<TSharedPtr<FJsonValue>> JsonArray;
			for (const FString& Val : Info.DefaultStringArray)
				JsonArray.Add(MakeShared<FJsonValueString>(Val));
			EntryObject->SetArrayField(TEXT("DefaultValue"), JsonArray);

			if (Info.bLimitValues)
			{
				TArray<TSharedPtr<FJsonValue>> AcceptedArray;
				for (const FString& Val : Info.AcceptedStringValues)
					AcceptedArray.Add(MakeShared<FJsonValueString>(Val));
				EntryObject->SetArrayField(TEXT("AcceptedValues"), AcceptedArray);
			}
			break;
		}

		case EStateKeyDataType::NumberArray:
		{
			TArray<TSharedPtr<FJsonValue>> JsonArray;
			for (double Val : Info.DefaultNumberArray)
				JsonArray.Add(MakeShared<FJsonValueNumber>(Val));
			EntryObject->SetArrayField(TEXT("DefaultValue"), JsonArray);

			if (Info.bLimitValues)
			{
				TArray<TSharedPtr<FJsonValue>> AcceptedArray;
				for (double Val : Info.AcceptedNumberValues)
					AcceptedArray.Add(MakeShared<FJsonValueNumber>(Val));
				EntryObject->SetArrayField(TEXT("AcceptedValues"), AcceptedArray);
			}
			break;
		}

		case EStateKeyDataType::BoolArray:
		{
			TArray<TSharedPtr<FJsonValue>> JsonArray;
			for (bool Val : Info.DefaultBoolArray)
				JsonArray.Add(MakeShared<FJsonValueBoolean>(Val));
			EntryObject->SetArrayField(TEXT("DefaultValue"), JsonArray);
			break;
		}

		case EStateKeyDataType::Invalid:
		default:
			continue;
		}

		TArray<FString> KeyParts;
		DottedKey.ParseIntoArray(KeyParts, TEXT("."), true);

		TSharedRef<FJsonObject> CurrentLevel = SchemaData;
		for (int32 i = 0; i < KeyParts.Num(); ++i)
		{
			const FString& Part = KeyParts[i];

			if (i == KeyParts.Num() - 1)
			{
				CurrentLevel->SetObjectField(Part, EntryObject);
			}
			else
			{
				TSharedPtr<FJsonObject> Child = CurrentLevel->Values.Contains(Part) ? CurrentLevel->GetObjectField(Part) : MakeShared<FJsonObject>();

				CurrentLevel->SetObjectField(Part, Child);
				CurrentLevel = Child.ToSharedRef();
			}
		}
	}

	Root->SetObjectField("ZEROLIGHT_SCHEMA_DATA", SchemaData);

	return Root;
}

