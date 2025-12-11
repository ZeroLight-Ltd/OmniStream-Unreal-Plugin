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
		EntryObject->SetBoolField(TEXT("bIgnoredInDataHashes"), Info.bIgnoredInDataHashes);

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

// Helper to convert a single FStateKeyInfo into a JSON Schema Leaf Node
TSharedPtr<FJsonObject> ConvertInfoToSchemaNode(const FStateKeyInfo& Info)
{
	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();

	FString SchemaType;
	bool bIsArray = false;
	FString ItemType;

	switch (Info.GetDataTypeEnum())
	{
	case EStateKeyDataType::String:
		SchemaType = "string";
		break;
	case EStateKeyDataType::Number:
		SchemaType = "number";
		break;
	case EStateKeyDataType::Bool:
		SchemaType = "boolean";
		break;
	case EStateKeyDataType::StringArray:
		SchemaType = "array";
		bIsArray = true;
		ItemType = "string";
		break;
	case EStateKeyDataType::NumberArray:
		SchemaType = "array";
		bIsArray = true;
		ItemType = "number";
		break;
	case EStateKeyDataType::BoolArray:
		SchemaType = "array";
		bIsArray = true;
		ItemType = "boolean";
		break;
	default:
		SchemaType = "string";
		break;
	}

	Node->SetStringField("type", SchemaType);

	if (Info.bLimitValues)
	{
		TArray<TSharedPtr<FJsonValue>> EnumValues;

		if (!bIsArray)
		{
			if (SchemaType == "string")
			{
				for (const FString& Val : Info.AcceptedStringValues)
					EnumValues.Add(MakeShared<FJsonValueString>(Val));
			}
			else if (SchemaType == "number")
			{
				for (double Val : Info.AcceptedNumberValues)
					EnumValues.Add(MakeShared<FJsonValueNumber>(Val));
			}

			if (EnumValues.Num() > 0)
			{
				Node->SetArrayField("enum", EnumValues);
			}
		}
		else
		{
			// Array Items Enums (The array contains items, which must be from the enum list)
			// For arrays, constraints usually go inside the "items" object
		}
	}

	if (bIsArray)
	{
		TSharedPtr<FJsonObject> ItemsNode = MakeShared<FJsonObject>();
		ItemsNode->SetStringField("type", ItemType);

		if (Info.bLimitValues)
		{
			TArray<TSharedPtr<FJsonValue>> ItemEnumValues;
			if (ItemType == "string")
			{
				for (const FString& Val : Info.AcceptedStringValues)
					ItemEnumValues.Add(MakeShared<FJsonValueString>(Val));
			}
			else if (ItemType == "number")
			{
				for (double Val : Info.AcceptedNumberValues)
					ItemEnumValues.Add(MakeShared<FJsonValueNumber>(Val));
			}

			if (ItemEnumValues.Num() > 0)
			{
				ItemsNode->SetArrayField("enum", ItemEnumValues);
			}
		}

		Node->SetObjectField("items", ItemsNode);
	}

	if (!bIsArray)
	{
		if (SchemaType == "string" && !Info.DefaultStringValue.IsEmpty())
		{
			Node->SetStringField("default", Info.DefaultStringValue);
		}
		else if (SchemaType == "number")
		{
			Node->SetNumberField("default", Info.DefaultNumberValue);
		}
		else if (SchemaType == "boolean")
		{
			Node->SetBoolField("default", Info.DefaultBoolValue);
		}
	}
	else
	{
		TArray<TSharedPtr<FJsonValue>> DefaultArray;
		if (ItemType == "string")
		{
			for (const FString& S : Info.DefaultStringArray) DefaultArray.Add(MakeShared<FJsonValueString>(S));
		}
		else if (ItemType == "number")
		{
			for (double D : Info.DefaultNumberArray) DefaultArray.Add(MakeShared<FJsonValueNumber>(D));
		}
		else if (ItemType == "boolean")
		{
			for (bool B : Info.DefaultBoolArray) DefaultArray.Add(MakeShared<FJsonValueBoolean>(B));
		}

		if (DefaultArray.Num() > 0)
		{
			Node->SetArrayField("default", DefaultArray);
		}
	}

	return Node;
}

TSharedRef<FJsonObject> UStateKeyInfoAsset::SerializeStateKeyAsset_JsonSchemaCompliant()
{
	TSharedRef<FJsonObject> RootSchema = MakeShared<FJsonObject>();

	RootSchema->SetStringField("$schema", "https://json-schema.org/draft/2020-12/schema");
	RootSchema->SetStringField("type", "object");
	RootSchema->SetStringField("title", GetName());

	TSharedPtr<FJsonObject> RootProperties = MakeShared<FJsonObject>();
	RootSchema->SetObjectField("properties", RootProperties);

	for (const TPair<FString, FStateKeyInfo>& Entry : KeyInfos)
	{
		FString FullKey = Entry.Key;
		const FStateKeyInfo& Info = Entry.Value;

		TArray<FString> KeyParts;
		FullKey.ParseIntoArray(KeyParts, TEXT("."), true);

		TSharedPtr<FJsonObject> CurrentContext = RootProperties;

		for (int32 i = 0; i < KeyParts.Num(); i++)
		{
			FString PartName = KeyParts[i];
			bool bIsLeaf = (i == KeyParts.Num() - 1);

			if (bIsLeaf)
			{
				TSharedPtr<FJsonObject> LeafNode = ConvertInfoToSchemaNode(Info);
				CurrentContext->SetObjectField(PartName, LeafNode);
			}
			else
			{

				if (CurrentContext->HasField(PartName))
				{
					TSharedPtr<FJsonObject> ExistingObj = CurrentContext->GetObjectField(PartName);

					if (!ExistingObj->HasField("properties"))
					{
						ExistingObj->SetObjectField("properties", MakeShared<FJsonObject>());
					}

					CurrentContext = ExistingObj->GetObjectField("properties");
				}
				else
				{
					TSharedPtr<FJsonObject> NewContainer = MakeShared<FJsonObject>();
					NewContainer->SetStringField("type", "object");

					TSharedPtr<FJsonObject> NewProperties = MakeShared<FJsonObject>();
					NewContainer->SetObjectField("properties", NewProperties);

					CurrentContext->SetObjectField(PartName, NewContainer);

					CurrentContext = NewProperties;
				}
			}
		}
	}

	return RootSchema;
}

