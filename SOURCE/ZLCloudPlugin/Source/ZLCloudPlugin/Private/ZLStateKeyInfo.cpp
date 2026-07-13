//Copyright ZeroLight ltd.All Rights Reserved.

#include "ZLStateKeyInfo.h"

namespace ZLStateKeyInfoDimeMetadataInternal
{
	static bool IsMetadataEmpty(const TArray<FDIMEModelMetadata>& InDimeModelData)
	{
		for (const FDIMEModelMetadata& ModelMetadata : InDimeModelData)
		{
			if (!ModelMetadata.ModelName.TrimStartAndEnd().IsEmpty() ||
				ModelMetadata.DescriptionLookupById.Num() > 0 ||
				ModelMetadata.Codes.Num() > 0)
			{
				return false;
			}
		}
		return true;
	}
}

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
		EntryObject->SetBoolField(TEXT("bUseMinMax"), Info.bUseMinMax);
		EntryObject->SetBoolField(TEXT("bAllowNullValue"), Info.bAllowNullValue);
		EntryObject->SetBoolField(TEXT("bDefaultValueIsNull"), Info.bDefaultValueIsNull);
		if (Info.bDisplayDescriptionAsOptions)
		{
			EntryObject->SetBoolField(TEXT("bDisplayDescriptionAsOptions"), Info.bDisplayDescriptionAsOptions);
		}
		if (Info.bUseMinMax)
		{
			EntryObject->SetNumberField(TEXT("minValue"), Info.MinValue);
			EntryObject->SetNumberField(TEXT("maxValue"), Info.MaxValue);
		}
		if (Info.GetDataTypeEnum() == EStateKeyDataType::StringArray)
		{
			EntryObject->SetBoolField(TEXT("bAllowDynamicArraySize"), Info.bAllowDynamicArraySize);
		}

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

			if (Info.AcceptedStringValues.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> AcceptedArray;
				for (const FString& Val : Info.AcceptedStringValues)
					AcceptedArray.Add(MakeShared<FJsonValueString>(Val));
				EntryObject->SetArrayField(TEXT("AcceptedValues"), AcceptedArray);
			}
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
	if (Info.bAllowNullValue)
	{
		Node->SetBoolField("x-zl-allowNullValue", true);
	}
	if (Info.bAllowNullValue && Info.bDefaultValueIsNull)
	{
		Node->SetBoolField("x-zl-defaultValueIsNull", true);
	}
	if (Info.bDisplayDescriptionAsOptions)
	{
		Node->SetBoolField("x-zl-displayDescriptionAsOptions", true);
	}

	if (SchemaType == "number" && Info.bUseMinMax)
	{
		Node->SetNumberField("minimum", Info.MinValue);
		Node->SetNumberField("maximum", Info.MaxValue);
	}

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

		if (ItemType == "number" && Info.bUseMinMax)
		{
			ItemsNode->SetNumberField("minimum", Info.MinValue);
			ItemsNode->SetNumberField("maximum", Info.MaxValue);
		}

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

		if (ItemType == "string")
		{
			Node->SetBoolField("x-zl-allowDynamicArraySize", Info.bAllowDynamicArraySize);
			if (!Info.bAllowDynamicArraySize)
			{
				const int32 FixedSize = Info.DefaultStringArray.Num() > 0 ? Info.DefaultStringArray.Num() : 1;
				Node->SetNumberField("minItems", FixedSize);
				Node->SetNumberField("maxItems", FixedSize);
			}
		}
	}

	if (Info.bAllowNullValue && Info.bDefaultValueIsNull)
	{
		// Intentionally omit "default" when null is selected.
	}
	else if (!bIsArray)
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

TSharedRef<FJsonObject> UStateKeyInfoAsset::BuildJsonSchemaCompliantFromKeyInfos(const TMap<FString, FStateKeyInfo>& InKeyInfos, const FString& SchemaTitle)
{
	TSharedRef<FJsonObject> RootSchema = MakeShared<FJsonObject>();

	RootSchema->SetStringField("$schema", "https://json-schema.org/draft/2020-12/schema");
	RootSchema->SetStringField("type", "object");
	RootSchema->SetStringField("title", SchemaTitle.IsEmpty() ? TEXT("Schema") : SchemaTitle);

	TSharedPtr<FJsonObject> RootProperties = MakeShared<FJsonObject>();
	RootSchema->SetObjectField("properties", RootProperties);

	for (const TPair<FString, FStateKeyInfo>& Entry : InKeyInfos)
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

TSharedRef<FJsonObject> UStateKeyInfoAsset::SerializeStateKeyAsset_JsonSchemaCompliant()
{
	return BuildJsonSchemaCompliantFromKeyInfos(KeyInfos, GetName());
}

TSharedPtr<FJsonValue> UStateKeyInfoAsset::SerializeDimeModelDataToJsonValue(const TArray<FDIMEModelMetadata>& InDimeModelData)
{
	TArray<TSharedPtr<FJsonValue>> ModelsArray;
	for (const FDIMEModelMetadata& ModelMetadata : InDimeModelData)
	{
		const FString TrimmedModelName = ModelMetadata.ModelName.TrimStartAndEnd();
		if (TrimmedModelName.IsEmpty())
		{
			continue;
		}

		TSharedPtr<FJsonObject> ModelObject = MakeShared<FJsonObject>();
		ModelObject->SetStringField(TEXT("modelName"), TrimmedModelName);

		TArray<int32> SortedDescriptionIds;
		ModelMetadata.DescriptionLookupById.GetKeys(SortedDescriptionIds);
		SortedDescriptionIds.Sort();

		TArray<TSharedPtr<FJsonValue>> DescriptionLookupArray;
		for (const int32 DescriptionId : SortedDescriptionIds)
		{
			const FString* DescriptionText = ModelMetadata.DescriptionLookupById.Find(DescriptionId);
			if (!DescriptionText)
			{
				continue;
			}

			TSharedPtr<FJsonObject> DescriptionObject = MakeShared<FJsonObject>();
			DescriptionObject->SetNumberField(TEXT("id"), DescriptionId);
			DescriptionObject->SetStringField(TEXT("text"), *DescriptionText);
			DescriptionLookupArray.Add(MakeShared<FJsonValueObject>(DescriptionObject));
		}
		ModelObject->SetArrayField(TEXT("descriptionLookup"), DescriptionLookupArray);

		TArray<FDIMEModelCodeMetadata> SortedCodes = ModelMetadata.Codes;
		SortedCodes.Sort([](const FDIMEModelCodeMetadata& A, const FDIMEModelCodeMetadata& B)
		{
			const int32 GroupCompare = A.Group.Compare(B.Group, ESearchCase::IgnoreCase);
			if (GroupCompare != 0)
			{
				return GroupCompare < 0;
			}
			return A.Code.Compare(B.Code, ESearchCase::IgnoreCase) < 0;
		});

		TArray<TSharedPtr<FJsonValue>> CodesArray;
		for (const FDIMEModelCodeMetadata& CodeMetadata : SortedCodes)
		{
			const FString TrimmedCode = CodeMetadata.Code.TrimStartAndEnd();
			const FString TrimmedGroup = CodeMetadata.Group.TrimStartAndEnd();
			if (TrimmedCode.IsEmpty() || TrimmedGroup.IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> CodeObject = MakeShared<FJsonObject>();
			CodeObject->SetStringField(TEXT("code"), TrimmedCode);
			CodeObject->SetStringField(TEXT("group"), TrimmedGroup);
			if (CodeMetadata.DescriptionId != INDEX_NONE)
			{
				CodeObject->SetNumberField(TEXT("descriptionId"), CodeMetadata.DescriptionId);
			}

			CodesArray.Add(MakeShared<FJsonValueObject>(CodeObject));
		}
		ModelObject->SetArrayField(TEXT("codes"), CodesArray);

		ModelsArray.Add(MakeShared<FJsonValueObject>(ModelObject));
	}

	return MakeShared<FJsonValueArray>(ModelsArray);
}

void UStateKeyInfoAsset::DeserializeDimeModelDataFromJsonValue(const TSharedPtr<FJsonValue>& InValue, TArray<FDIMEModelMetadata>& OutData)
{
	OutData.Empty();
	if (!InValue.IsValid() || InValue->Type != EJson::Array)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& ModelValue : InValue->AsArray())
	{
		if (!ModelValue.IsValid() || ModelValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> ModelObject = ModelValue->AsObject();
		if (!ModelObject.IsValid())
		{
			continue;
		}

		FDIMEModelMetadata ModelMetadata;
		FString ModelName;
		if (!ModelObject->TryGetStringField(TEXT("modelName"), ModelName))
		{
			continue;
		}
		ModelMetadata.ModelName = ModelName.TrimStartAndEnd();
		if (ModelMetadata.ModelName.IsEmpty())
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* DescriptionLookupArray = nullptr;
		if (ModelObject->TryGetArrayField(TEXT("descriptionLookup"), DescriptionLookupArray))
		{
			for (const TSharedPtr<FJsonValue>& DescriptionValue : *DescriptionLookupArray)
			{
				if (!DescriptionValue.IsValid() || DescriptionValue->Type != EJson::Object)
				{
					continue;
				}

				const TSharedPtr<FJsonObject> DescriptionObject = DescriptionValue->AsObject();
				if (!DescriptionObject.IsValid())
				{
					continue;
				}

				double DescriptionIdNumber = 0.0;
				FString DescriptionText;
				if (!DescriptionObject->TryGetNumberField(TEXT("id"), DescriptionIdNumber) ||
					!DescriptionObject->TryGetStringField(TEXT("text"), DescriptionText))
				{
					continue;
				}
				const int32 DescriptionId = static_cast<int32>(DescriptionIdNumber);
				ModelMetadata.DescriptionLookupById.FindOrAdd(DescriptionId) = DescriptionText;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* CodesArray = nullptr;
		if (ModelObject->TryGetArrayField(TEXT("codes"), CodesArray))
		{
			for (const TSharedPtr<FJsonValue>& CodeValue : *CodesArray)
			{
				if (!CodeValue.IsValid() || CodeValue->Type != EJson::Object)
				{
					continue;
				}

				const TSharedPtr<FJsonObject> CodeObject = CodeValue->AsObject();
				if (!CodeObject.IsValid())
				{
					continue;
				}

				FDIMEModelCodeMetadata CodeMetadata;
				FString CodeValueString;
				FString GroupValueString;
				if (!CodeObject->TryGetStringField(TEXT("code"), CodeValueString) ||
					!CodeObject->TryGetStringField(TEXT("group"), GroupValueString))
				{
					continue;
				}
				CodeMetadata.Code = CodeValueString.TrimStartAndEnd();
				CodeMetadata.Group = GroupValueString.TrimStartAndEnd();
				if (CodeMetadata.Code.IsEmpty() || CodeMetadata.Group.IsEmpty())
				{
					continue;
				}

				double DescriptionIdNumber = 0.0;
				if (CodeObject->TryGetNumberField(TEXT("descriptionId"), DescriptionIdNumber))
				{
					CodeMetadata.DescriptionId = static_cast<int32>(DescriptionIdNumber);
				}
				else
				{
					CodeMetadata.DescriptionId = INDEX_NONE;
				}

				ModelMetadata.Codes.Add(CodeMetadata);
			}
		}

		OutData.Add(MoveTemp(ModelMetadata));
	}
}

TSharedRef<FJsonObject> UStateKeyInfoAsset::BuildZLSchemaFileObject(
	const TMap<FString, FStateKeyInfo>& InKeyInfos,
	const FString& SchemaTitle,
	const TArray<FDIMEModelMetadata>& InDimeModelData)
{
	TSharedRef<FJsonObject> JsonSchemaObject = BuildJsonSchemaCompliantFromKeyInfos(InKeyInfos, SchemaTitle);
	if (ZLStateKeyInfoDimeMetadataInternal::IsMetadataEmpty(InDimeModelData))
	{
		return JsonSchemaObject;
	}

	TSharedRef<FJsonObject> WrapperObject = MakeShared<FJsonObject>();
	WrapperObject->SetObjectField(TEXT("jsonSchema"), JsonSchemaObject);
	WrapperObject->SetField(TEXT("dimeModelData"), SerializeDimeModelDataToJsonValue(InDimeModelData));
	return WrapperObject;
}

TSharedRef<FJsonObject> UStateKeyInfoAsset::SerializeStateKeyAsset_ZLSchemaFile()
{
	return BuildZLSchemaFileObject(KeyInfos, GetName(), DimeModelData);
}

