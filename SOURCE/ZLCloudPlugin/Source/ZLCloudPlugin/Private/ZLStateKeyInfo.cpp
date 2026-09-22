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

	Node->SetStringField(TEXT("type"), SchemaType);
	if (Info.bAllowNullValue)
	{
		Node->SetBoolField(TEXT("x-zl-allowNullValue"), true);
	}
	if (Info.bAllowNullValue && Info.bDefaultValueIsNull)
	{
		Node->SetBoolField(TEXT("x-zl-defaultValueIsNull"), true);
	}
	if (Info.bDisplayDescriptionAsOptions)
	{
		Node->SetBoolField(TEXT("x-zl-displayDescriptionAsOptions"), true);
	}

	if (SchemaType == "number" && Info.bUseMinMax)
	{
		Node->SetNumberField(TEXT("minimum"), Info.MinValue);
		Node->SetNumberField(TEXT("maximum"), Info.MaxValue);
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
				Node->SetArrayField(TEXT("enum"), EnumValues);
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
		ItemsNode->SetStringField(TEXT("type"), ItemType);

		if (ItemType == "number" && Info.bUseMinMax)
		{
			ItemsNode->SetNumberField(TEXT("minimum"), Info.MinValue);
			ItemsNode->SetNumberField(TEXT("maximum"), Info.MaxValue);
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
				ItemsNode->SetArrayField(TEXT("enum"), ItemEnumValues);
			}
		}

		Node->SetObjectField(TEXT("items"), ItemsNode);

		if (ItemType == "string")
		{
			Node->SetBoolField(TEXT("x-zl-allowDynamicArraySize"), Info.bAllowDynamicArraySize);
			if (!Info.bAllowDynamicArraySize)
			{
				const int32 FixedSize = Info.DefaultStringArray.Num() > 0 ? Info.DefaultStringArray.Num() : 1;
				Node->SetNumberField(TEXT("minItems"), FixedSize);
				Node->SetNumberField(TEXT("maxItems"), FixedSize);
			}
			if (Info.MaximumAdditionalModelSlots > 0)
			{
				Node->SetNumberField(TEXT("x-zl-maximumAdditionalModelSlots"), Info.MaximumAdditionalModelSlots);
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
			Node->SetStringField(TEXT("default"), Info.DefaultStringValue);
		}
		else if (SchemaType == "number")
		{
			Node->SetNumberField(TEXT("default"), Info.DefaultNumberValue);
		}
		else if (SchemaType == "boolean")
		{
			Node->SetBoolField(TEXT("default"), Info.DefaultBoolValue);
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
			Node->SetArrayField(TEXT("default"), DefaultArray);
		}
	}

	return Node;
}

TSharedRef<FJsonObject> UStateKeyInfoAsset::BuildJsonSchemaCompliantFromKeyInfos(const TMap<FString, FStateKeyInfo>& InKeyInfos, const FString& SchemaTitle)
{
	TSharedRef<FJsonObject> RootSchema = MakeShared<FJsonObject>();

	RootSchema->SetStringField(TEXT("$schema"), TEXT("https://json-schema.org/draft/2020-12/schema"));
	RootSchema->SetStringField(TEXT("type"), TEXT("object"));
	RootSchema->SetStringField(TEXT("title"), SchemaTitle.IsEmpty() ? TEXT("Schema") : SchemaTitle);

	TSharedPtr<FJsonObject> RootProperties = MakeShared<FJsonObject>();
	RootSchema->SetObjectField(TEXT("properties"), RootProperties);

	TArray<FString> SortedKeys;
	InKeyInfos.GetKeys(SortedKeys);
	SortedKeys.Sort([](const FString& A, const FString& B)
	{
		return ZLNaturalStringLessThan(A, B);
	});

	for (const FString& FullKey : SortedKeys)
	{
		const FStateKeyInfo* InfoPtr = InKeyInfos.Find(FullKey);
		if (!InfoPtr)
		{
			continue;
		}
		const FStateKeyInfo& Info = *InfoPtr;

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

					if (!ExistingObj->HasField(TEXT("properties")))
					{
						ExistingObj->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
					}

					CurrentContext = ExistingObj->GetObjectField(TEXT("properties"));
				}
				else
				{
					TSharedPtr<FJsonObject> NewContainer = MakeShared<FJsonObject>();
					NewContainer->SetStringField(TEXT("type"), TEXT("object"));

					TSharedPtr<FJsonObject> NewProperties = MakeShared<FJsonObject>();
					NewContainer->SetObjectField(TEXT("properties"), NewProperties);

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

