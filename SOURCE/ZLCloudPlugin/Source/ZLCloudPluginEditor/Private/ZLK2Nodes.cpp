//Copyright ZeroLight ltd.All Rights Reserved.

#include "ZLK2Nodes.h"
#include "ZLSchemaK2NodeRemap.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "K2Node_CallFunction.h"
#include "BlueprintNodeSpawner.h"
#include "ZLCloudPluginStateManager.h"
#include "K2Node.h"
#include "K2Node_MakeArray.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Engine/AssetManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Editor.h"
#include "Framework/Commands/UIAction.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "Widgets/Text/STextBlock.h"
#include "Containers/Set.h"

#define LOCTEXT_NAMESPACE "UK2Node_SelectAssetKey"

void SGraphPin_KeySelector::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	this->SetCursor(EMouseCursor::Default);

#if UNREAL_5_3_OR_NEWER
	SetPinObj(InPin);
#else
	SGraphPin::Construct(SGraphPin::FArguments(), InPin);
#endif


	ChildSlot
		[
			GetDefaultValueWidget()
		];
}

TArray<TSharedPtr<FString>> SGraphPin_KeySelector::GatherKeyOptions() const
{
	TArray<TSharedPtr<FString>> Keys;

	if (!GetPinObj()) return Keys;

	UEdGraphPin* PinObj = GetPinObj();

	UEdGraphNode* Node = PinObj->GetOwningNode();
	if (!Node) return Keys;

	UEdGraphPin* AssetPin = Node->FindPin(TEXT("Asset"));
	if (!AssetPin || !AssetPin->DefaultObject) return Keys;

	const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
	if (!Asset) return Keys;

	bool displaySubKeyedJSONObjects = false;
	UEdGraphPin* SubKeysPin = Node->FindPin(TEXT("SubKeys"));
	UEdGraphPin* OutJsonObjectString = Node->FindPin(TEXT("OutObjectString"));
	if (SubKeysPin || OutJsonObjectString)
		displaySubKeyedJSONObjects = true;

	TSet<FString> UniqueKeys;

	for (const auto& Pair : Asset->KeyInfos)
	{
		const FString& FullKey = Pair.Key;

		if (displaySubKeyedJSONObjects)
		{
			if (FullKey.Contains(TEXT(".")))
			{
				TArray<FString> Parts;
				FullKey.ParseIntoArray(Parts, TEXT("."), true);

				FString Path;
				for (int32 i = 0; i < Parts.Num() - 1; ++i)
				{
					if (!Path.IsEmpty())
					{
						Path += TEXT(".");
					}
					Path += Parts[i];

					UniqueKeys.Add(Path);
				}
			}
		}
		else
			UniqueKeys.Add(FullKey);
	}

	for (const FString& Key : UniqueKeys)
	{
		Keys.Add(MakeShared<FString>(Key));
	}

	return Keys;
}

TSharedRef<SWidget> SGraphPin_KeySelector::GetDefaultValueWidget()
{
	KeyOptions = GatherKeyOptions();

	UEdGraphPin* PinObj = GetPinObj();

	ComboBox = SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&KeyOptions)
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> InOption)
	{
		return SNew(STextBlock).Text(FText::FromString(*InOption));
	})
		.OnSelectionChanged(this, &SGraphPin_KeySelector::OnSelectionChanged)
		[
			SNew(STextBlock)
				.Text_Lambda([this]()
			{
				FString Current = GetPinObj()->GetDefaultAsString();
				return FText::FromString(Current.IsEmpty() ? TEXT("Select Key") : Current);
			})
		];

	return ComboBox.ToSharedRef();
}

void SGraphPin_KeySelector::OnSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid())
	{
		GetPinObj()->GetSchema()->TrySetDefaultValue(*GetPinObj(), *NewSelection);
	}
}

static bool ZL_ValuePinNeedsConstrainedWidget(const UEdGraphPin* Pin)
{
	if (!Pin || Pin->Direction != EGPD_Input)
	{
		return false;
	}
	const UEdGraphNode* Node = Pin->GetOwningNode();
	if (!Node || !Node->IsA(UZLK2Node_PerformStateRequest::StaticClass()))
	{
		return false;
	}
	const FString PinNameStr = Pin->PinName.ToString();
	if (!PinNameStr.StartsWith(TEXT("Value_")))
	{
		return false;
	}
	const FString Rest = PinNameStr.RightChop(6);
	if (Rest.IsEmpty() || !Rest.IsNumeric())
	{
		return false;
	}
	const int32 Slot = FCString::Atoi(*Rest);
	const UEdGraphPin* AssetPin = Node->FindPin(TEXT("Asset"));
	if (!AssetPin || !AssetPin->DefaultObject)
	{
		return false;
	}
	const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
	if (!Asset)
	{
		return false;
	}
	const UEdGraphPin* KeyPin = Node->FindPin(*FString::Printf(TEXT("Key_%d"), Slot));
	if (!KeyPin)
	{
		return false;
	}
	const FString Key = KeyPin->DefaultValue;
	if (Key.IsEmpty() || Key == TEXT("Select Key"))
	{
		return false;
	}
	const FStateKeyInfo* Info = Asset->KeyInfos.Find(Key);
	if (!Info || !Info->bLimitValues)
	{
		return false;
	}
	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_String && Pin->PinType.ContainerType == EPinContainerType::None)
	{
		return Info->AcceptedStringValues.Num() > 0;
	}
	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real && Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float
		&& Pin->PinType.ContainerType == EPinContainerType::None)
	{
		return Info->AcceptedNumberValues.Num() > 0;
	}
	return false;
}

void SGraphPin_SchemaConstrainedValue::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InPin);
}

TArray<TSharedPtr<FString>> SGraphPin_SchemaConstrainedValue::GatherValueOptions() const
{
	TArray<TSharedPtr<FString>> Options;
	UEdGraphPin* PinObj = GetPinObj();
	if (!PinObj)
	{
		return Options;
	}

	const FString PinNameStr = PinObj->PinName.ToString();
	if (!PinNameStr.StartsWith(TEXT("Value_")))
	{
		return Options;
	}
	const FString Rest = PinNameStr.RightChop(6);
	if (Rest.IsEmpty() || !Rest.IsNumeric())
	{
		return Options;
	}
	const int32 Slot = FCString::Atoi(*Rest);

	UEdGraphNode* Node = PinObj->GetOwningNode();
	if (!Node)
	{
		return Options;
	}
	UEdGraphPin* AssetPin = Node->FindPin(TEXT("Asset"));
	if (!AssetPin || !AssetPin->DefaultObject)
	{
		return Options;
	}
	const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
	if (!Asset)
	{
		return Options;
	}
	UEdGraphPin* KeyPin = Node->FindPin(*FString::Printf(TEXT("Key_%d"), Slot));
	if (!KeyPin)
	{
		return Options;
	}
	const FString Key = KeyPin->DefaultValue;
	if (Key.IsEmpty() || Key == TEXT("Select Key"))
	{
		return Options;
	}
	const FStateKeyInfo* Info = Asset->KeyInfos.Find(Key);
	if (!Info || !Info->bLimitValues)
	{
		return Options;
	}
	if (PinObj->PinType.PinCategory == UEdGraphSchema_K2::PC_String && PinObj->PinType.ContainerType == EPinContainerType::None)
	{
		for (const FString& S : Info->AcceptedStringValues)
		{
			Options.Add(MakeShared<FString>(S));
		}
	}
	else if (PinObj->PinType.PinCategory == UEdGraphSchema_K2::PC_Real && PinObj->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float
		&& PinObj->PinType.ContainerType == EPinContainerType::None)
	{
		for (double N : Info->AcceptedNumberValues)
		{
			Options.Add(MakeShared<FString>(FString::SanitizeFloat(static_cast<float>(N))));
		}
	}
	return Options;
}

TSharedRef<SWidget> SGraphPin_SchemaConstrainedValue::GetDefaultValueWidget()
{
	ValueOptions = GatherValueOptions();
	if (ValueOptions.Num() == 0)
	{
		return SGraphPin::GetDefaultValueWidget();
	}

	ValueCombo = SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&ValueOptions)
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> InOption)
		{
			return SNew(STextBlock).Text(FText::FromString(InOption.IsValid() ? *InOption : FString()));
		})
		.OnSelectionChanged(this, &SGraphPin_SchemaConstrainedValue::OnSelectionChanged)
		[
			SNew(STextBlock)
				.Text_Lambda([this]()
			{
				FString Current = GetPinObj()->GetDefaultAsString();
				return FText::FromString(Current.IsEmpty() ? TEXT("Select value") : Current);
			})
		];

	return ValueCombo.ToSharedRef();
}

void SGraphPin_SchemaConstrainedValue::OnSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid())
	{
		GetPinObj()->GetSchema()->TrySetDefaultValue(*GetPinObj(), *NewSelection);
	}
}

FText UZLK2Node_GetRequestedStateValue::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("SelectKeyNodeTitle", "Get Requested State Value (Schema Asset)");
}

void UZLK2Node_GetRequestedStateValue::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	if (GIsReconstructingBlueprintInstances || GIsDuplicatingClassForReinstancing)
	{
		return;
	}

	Super::PinDefaultValueChanged(ChangedPin);

	if (ChangedPin && ChangedPin->PinName == TEXT("Asset"))
	{
		UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(ChangedPin->DefaultObject);
		if (!Asset || Asset->KeyInfos.IsEmpty())
			return;

		if (!HasValidBlueprint())
		{
			return;
		}

		ReconstructNode();

		UEdGraphPin* KeyNamePin = FindPin(TEXT("Key"), EGPD_Input);
		if (KeyNamePin)
		{
			KeyNamePin->DefaultValue = TEXT("Select Key");
		}
	}

	if (ChangedPin && ChangedPin->PinName == "Key")
	{
		UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
		if (AssetPin && AssetPin->DefaultObject)
		{
			const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
			if (!Asset || Asset->KeyInfos.IsEmpty())
				return;

			if (Asset->KeyInfos.Contains(ChangedPin->DefaultValue))
			{
				if (!HasValidBlueprint())
				{
					return;
				}

				ReconstructNode();
			}
		}
	}
}

void UZLK2Node_GetRequestedStateValue::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);

	UEdGraphPin* NewAssetPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Object,
		NAME_None,
		TEXT("Asset")
	);
	NewAssetPin->PinType.PinSubCategoryObject = UStateKeyInfoAsset::StaticClass();

	if (SavedAssetObject != nullptr)
	{
		NewAssetPin->DefaultObject = SavedAssetObject;
		SavedAssetObject = nullptr;
	}

	EStateKeyDataType dataType = EStateKeyDataType::Invalid;

	if (UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(NewAssetPin->DefaultObject))
	{
		if (Asset->KeyInfos.IsEmpty()) // asset is not yet loaded
		{
			FSoftObjectPath AssetPath(Asset);
			FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

			Streamable.RequestAsyncLoad(AssetPath, FStreamableDelegate::CreateUObject(
				this,
				&UZLK2Node_GetRequestedStateValue::OnAssetLoaded,
				AssetPath
			));

			//Dont skip pin creation, but use uproperty serialized saved data type to restore pins
			dataType = SavedDataType;

			//return;
		}
	}

	const FName KeySubCat(TEXT("OmniStreamSchemaKey"));
	const FName KeyName(TEXT("Key"));

	UEdGraphPin* NewKeyPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, KeySubCat, KeyName);

	if (!SavedKeyStr.IsEmpty())
	{
		NewKeyPin->DefaultValue = SavedKeyStr;
		SavedKeyStr = "";
	}

	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("Instant Confirm"));

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	UEdGraphPin* ArrayPin;
	UEdGraphPin* OutPin;
	if (dataType == EStateKeyDataType::Invalid)
		dataType = GetDataType();

	if (dataType != EStateKeyDataType::Invalid)
		SavedDataType = dataType;

	switch (dataType)
	{
	case EStateKeyDataType::String:
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_String, TEXT("Value"));
		break;
	case EStateKeyDataType::StringArray:
		ArrayPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_String, TEXT("Value"));
		ArrayPin->PinType.ContainerType = EPinContainerType::Array;
		break;
	case EStateKeyDataType::Number:
		OutPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Real, TEXT("Value"));
		OutPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPin->PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		break;
	case EStateKeyDataType::NumberArray:
		ArrayPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Real, TEXT("Value"));
		ArrayPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		ArrayPin->PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		ArrayPin->PinType.ContainerType = EPinContainerType::Array;
		break;
	case EStateKeyDataType::Bool:
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, TEXT("Value"));
		break;
	case EStateKeyDataType::BoolArray:
		ArrayPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, TEXT("Value"));
		ArrayPin->PinType.ContainerType = EPinContainerType::Array;
		break;
	case EStateKeyDataType::Invalid:
	default:
		break;
	}

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, TEXT("Success"));
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_String, TEXT("Key"));
}

void UZLK2Node_GetRequestedStateValue::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	for (UEdGraphPin* OldPin : OldPins)
	{
		if (OldPin->PinName == TEXT("Asset") && OldPin->Direction == EGPD_Input)
		{
			SavedAssetObject = Cast<UStateKeyInfoAsset>(OldPin->DefaultObject);

			// Try loading by name if DefaultObject is stale
			if (SavedAssetObject && SavedAssetObject->KeyInfos.IsEmpty())
			{
				FString AssetPathName = SavedAssetObject->GetPathName();
				UStateKeyInfoAsset* LoadedAssetAlt = Cast<UStateKeyInfoAsset>(StaticLoadObject(UStateKeyInfoAsset::StaticClass(), nullptr, *AssetPathName, nullptr, 0U, nullptr, false));
				AssetPathStr = FSoftObjectPath(LoadedAssetAlt);

				FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

				Streamable.RequestAsyncLoad(AssetPathStr, FStreamableDelegate::CreateUObject(
					this,
					&UZLK2Node_GetRequestedStateValue::OnAssetLoaded,
					AssetPathStr
				));

				SavedAssetObject = Cast<UStateKeyInfoAsset>(LoadedAssetAlt);
			}
		}

		if (OldPin->PinName == TEXT("Key") && OldPin->Direction == EGPD_Input)
		{
			SavedKeyStr = OldPin->DefaultValue;
		}
	}

	Super::ReallocatePinsDuringReconstruction(OldPins);
}

void UZLK2Node_GetRequestedStateValue::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	static FName FunctionName = GET_FUNCTION_NAME_CHECKED(UZLCloudPluginStateManagerBlueprints, GetRequestedSchemaValue);

	UFunction* TargetFunction = UZLCloudPluginStateManagerBlueprints::StaticClass()->FindFunctionByName(FunctionName);
	if (!TargetFunction)
	{
		CompilerContext.MessageLog.Error(*FString::Printf(TEXT("Function '%s' not found."), *FunctionName.ToString()));
		return;
	}

	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->SetFromFunction(TargetFunction);
	CallNode->AllocateDefaultPins();

	UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
	EStateKeyDataType dataType = EStateKeyDataType::Invalid;

	if (AssetPin && AssetPin->DefaultObject)
	{
		const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);

		if (Asset && Asset->KeyInfos.IsEmpty()) //Reloading from editor launch, needs to wait on full asset loaded
			dataType = SavedDataType;
	}

	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Asset")), *CallNode->FindPin(TEXT("Asset")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Key"), EGPD_Input), *CallNode->FindPin(TEXT("KeyName")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Instant Confirm")), *CallNode->FindPin(TEXT("InstantConfirm")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Key"), EGPD_Output), *CallNode->FindPin(TEXT("KeyOut")));


	// Output selection logic
	FString ReturnPinName = TEXT("Value");
	FString TargetOutputPin;
	if (dataType == EStateKeyDataType::Invalid)
		dataType = GetDataType();

	switch (dataType)
	{
	case EStateKeyDataType::String:
		TargetOutputPin = "OutString";
		break;
	case EStateKeyDataType::StringArray:
		TargetOutputPin = "OutStringArray";
		break;
	case EStateKeyDataType::Number:
		TargetOutputPin = "OutNumber";
		break;
	case EStateKeyDataType::NumberArray:
		TargetOutputPin = "OutNumberArray";
		break;
	case EStateKeyDataType::Bool:
		TargetOutputPin = "OutBool";
		break;
	case EStateKeyDataType::BoolArray:
		TargetOutputPin = "OutBoolArray";
		break;
	case EStateKeyDataType::Invalid:
	default:
		CompilerContext.MessageLog.Error(TEXT("Key on node @@ must be set to a valid key."), this);
		return;
	}

	UEdGraphPin* execTopPin = GetExecPin();
	UEdGraphPin* execInternalPin = CallNode->GetExecPin();

	UEdGraphPin* thenTopPin = GetThenPin();
	UEdGraphPin* thenInternalPin = CallNode->GetThenPin();

	CompilerContext.MovePinLinksToIntermediate(*FindPin(ReturnPinName), *CallNode->FindPin(*TargetOutputPin));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Success")), *CallNode->FindPin(TEXT("Success")));
	CompilerContext.MovePinLinksToIntermediate(*execTopPin, *execInternalPin);
	CompilerContext.MovePinLinksToIntermediate(*thenTopPin, *thenInternalPin);

	BreakAllNodeLinks();
}

void UZLK2Node_GetRequestedStateValue::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UZLK2Node_GetRequestedStateValue::GetMenuCategory() const
{
	return LOCTEXT("NodeCategory", "Zerolight Omnistream State");
}

FName UZLK2Node_GetRequestedStateValue::GetFunctionName() const
{
	return FName(TEXT("GetRequestedSchemaValue"));
}

FText UZLK2Node_GetRequestedStateValueSubKeys::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("SelectKeyNodeTitle", "Get Requested State Value Sub Keys (Schema Asset)");
}

void UZLK2Node_GetRequestedStateValueSubKeys::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	if (GIsReconstructingBlueprintInstances || GIsDuplicatingClassForReinstancing)
	{
		return;
	}

	Super::PinDefaultValueChanged(ChangedPin);

	if (ChangedPin && ChangedPin->PinName == TEXT("Asset"))
	{
		UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(ChangedPin->DefaultObject);
		if (!Asset || Asset->KeyInfos.IsEmpty())
			return;

		if (!HasValidBlueprint())
		{
			return;
		}

		ReconstructNode();

		UEdGraphPin* KeyNamePin = FindPin(TEXT("Key"), EGPD_Input);
		if (KeyNamePin)
		{
			KeyNamePin->DefaultValue = TEXT("Select Key");
		}
	}

	if (ChangedPin && ChangedPin->PinName == "Key")
	{
		UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
		if (AssetPin && AssetPin->DefaultObject)
		{
			const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
			if (!Asset || Asset->KeyInfos.IsEmpty())
				return;

			if (Asset->KeyInfos.Contains(ChangedPin->DefaultValue))
			{
				if (!HasValidBlueprint())
				{
					return;
				}

				ReconstructNode();
			}
		}
	}
}

void UZLK2Node_GetRequestedStateValueSubKeys::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);

	UEdGraphPin* NewAssetPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Object,
		NAME_None,
		TEXT("Asset")
	);
	NewAssetPin->PinType.PinSubCategoryObject = UStateKeyInfoAsset::StaticClass();

	if (SavedAssetObject != nullptr)
	{
		NewAssetPin->DefaultObject = SavedAssetObject;
		SavedAssetObject = nullptr;
	}

	EStateKeyDataType dataType = EStateKeyDataType::Invalid;

	if (UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(NewAssetPin->DefaultObject))
	{
		if (Asset->KeyInfos.IsEmpty()) // asset is not yet loaded
		{
			FSoftObjectPath AssetPath(Asset);
			FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

			Streamable.RequestAsyncLoad(AssetPath, FStreamableDelegate::CreateUObject(
				this,
				&UZLK2Node_GetRequestedStateValueSubKeys::OnAssetLoaded,
				AssetPath
			));

			//return;
		}
	}

	const FName KeySubCat(TEXT("OmniStreamSchemaKey"));
	const FName KeyName(TEXT("Key"));

	UEdGraphPin* NewKeyPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, KeySubCat, KeyName);

	if (!SavedKeyStr.IsEmpty())
	{
		NewKeyPin->DefaultValue = SavedKeyStr;
		SavedKeyStr = "";
	}

	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("Instant Confirm"));

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	UEdGraphPin* ArrayPin;

	ArrayPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Struct, TEXT("SubKeys"));
	ArrayPin->PinType.PinSubCategoryObject = FSubKeyValueResult::StaticStruct();
	ArrayPin->PinType.ContainerType = EPinContainerType::Array;

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, TEXT("Success"));
}

void UZLK2Node_GetRequestedStateValueSubKeys::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	for (UEdGraphPin* OldPin : OldPins)
	{
		if (OldPin->PinName == TEXT("Asset") && OldPin->Direction == EGPD_Input)
		{
			SavedAssetObject = Cast<UStateKeyInfoAsset>(OldPin->DefaultObject);

			// Try loading by name if DefaultObject is stale
			if (SavedAssetObject && SavedAssetObject->KeyInfos.IsEmpty())
			{
				FString AssetPathName = SavedAssetObject->GetPathName();
				UStateKeyInfoAsset* LoadedAssetAlt = Cast<UStateKeyInfoAsset>(StaticLoadObject(UStateKeyInfoAsset::StaticClass(), nullptr, *AssetPathName, nullptr, 0U, nullptr, false));
				AssetPathStr = FSoftObjectPath(LoadedAssetAlt);

				FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

				Streamable.RequestAsyncLoad(AssetPathStr, FStreamableDelegate::CreateUObject(
					this,
					&UZLK2Node_GetRequestedStateValueSubKeys::OnAssetLoaded,
					AssetPathStr
				));

				SavedAssetObject = Cast<UStateKeyInfoAsset>(LoadedAssetAlt);
			}
		}

		if (OldPin->PinName == TEXT("Key") && OldPin->Direction == EGPD_Input)
		{
			SavedKeyStr = OldPin->DefaultValue;
		}
	}

	Super::ReallocatePinsDuringReconstruction(OldPins);
}

void UZLK2Node_GetRequestedStateValueSubKeys::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	static FName FunctionName = GET_FUNCTION_NAME_CHECKED(UZLCloudPluginStateManagerBlueprints, GetRequestedSchemaValueSubKeys);

	UFunction* TargetFunction = UZLCloudPluginStateManagerBlueprints::StaticClass()->FindFunctionByName(FunctionName);
	if (!TargetFunction)
	{
		CompilerContext.MessageLog.Error(*FString::Printf(TEXT("Function '%s' not found."), *FunctionName.ToString()));
		return;
	}

	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->SetFromFunction(TargetFunction);
	CallNode->AllocateDefaultPins();

	UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));

	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Asset")), *CallNode->FindPin(TEXT("Asset")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Key"), EGPD_Input), *CallNode->FindPin(TEXT("ParentKey")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Instant Confirm")), *CallNode->FindPin(TEXT("InstantConfirm")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("SubKeys"), EGPD_Output), *CallNode->FindPin(TEXT("Results")));

	UEdGraphPin* execTopPin = GetExecPin();
	UEdGraphPin* execInternalPin = CallNode->GetExecPin();

	UEdGraphPin* thenTopPin = GetThenPin();
	UEdGraphPin* thenInternalPin = CallNode->GetThenPin();

	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Success")), *CallNode->FindPin(TEXT("Success")));
	CompilerContext.MovePinLinksToIntermediate(*execTopPin, *execInternalPin);
	CompilerContext.MovePinLinksToIntermediate(*thenTopPin, *thenInternalPin);

	BreakAllNodeLinks();
}

void UZLK2Node_GetRequestedStateValueSubKeys::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UZLK2Node_GetRequestedStateValueSubKeys::GetMenuCategory() const
{
	return LOCTEXT("NodeCategory", "Zerolight Omnistream State");
}

FName UZLK2Node_GetRequestedStateValueSubKeys::GetFunctionName() const
{
	return FName(TEXT("GetRequestedSchemaValueSubKeys"));
}

FText UZLK2Node_GetRequestedStateValueObjectString::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("SelectKeyNodeTitle", "Get Requested State Value JSON Object String (Schema Asset)");
}

void UZLK2Node_GetRequestedStateValueObjectString::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	if (GIsReconstructingBlueprintInstances || GIsDuplicatingClassForReinstancing)
	{
		return;
	}

	Super::PinDefaultValueChanged(ChangedPin);

	if (ChangedPin && ChangedPin->PinName == TEXT("Asset"))
	{
		UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(ChangedPin->DefaultObject);
		if (!Asset || Asset->KeyInfos.IsEmpty())
			return;

		if (!HasValidBlueprint())
		{
			return;
		}

		ReconstructNode();

		UEdGraphPin* KeyNamePin = FindPin(TEXT("Key"), EGPD_Input);
		if (KeyNamePin)
		{
			KeyNamePin->DefaultValue = TEXT("Select Key");
		}
	}

	if (ChangedPin && ChangedPin->PinName == "Key")
	{
		UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
		if (AssetPin && AssetPin->DefaultObject)
		{
			const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
			if (!Asset || Asset->KeyInfos.IsEmpty())
				return;

			if (Asset->KeyInfos.Contains(ChangedPin->DefaultValue))
			{
				if (!HasValidBlueprint())
				{
					return;
				}

				ReconstructNode();
			}
		}
	}
}

void UZLK2Node_GetRequestedStateValueObjectString::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);

	UEdGraphPin* NewAssetPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Object,
		NAME_None,
		TEXT("Asset")
	);
	NewAssetPin->PinType.PinSubCategoryObject = UStateKeyInfoAsset::StaticClass();

	if (SavedAssetObject != nullptr)
	{
		NewAssetPin->DefaultObject = SavedAssetObject;
		SavedAssetObject = nullptr;
	}

	EStateKeyDataType dataType = EStateKeyDataType::Invalid;

	if (UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(NewAssetPin->DefaultObject))
	{
		if (Asset->KeyInfos.IsEmpty()) // asset is not yet loaded
		{
			FSoftObjectPath AssetPath(Asset);
			FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

			Streamable.RequestAsyncLoad(AssetPath, FStreamableDelegate::CreateUObject(
				this,
				&UZLK2Node_GetRequestedStateValueObjectString::OnAssetLoaded,
				AssetPath
			));

			//return;
		}
	}

	const FName KeySubCat(TEXT("OmniStreamSchemaKey"));
	const FName KeyName(TEXT("Key"));

	UEdGraphPin* NewKeyPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, KeySubCat, KeyName);

	if (!SavedKeyStr.IsEmpty())
	{
		NewKeyPin->DefaultValue = SavedKeyStr;
		SavedKeyStr = "";
	}

	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("Instant Confirm"));
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("Include All Fields"));

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_String, TEXT("OutObjectString"));

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, TEXT("Success"));
}

void UZLK2Node_GetRequestedStateValueObjectString::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	for (UEdGraphPin* OldPin : OldPins)
	{
		if (OldPin->PinName == TEXT("Asset") && OldPin->Direction == EGPD_Input)
		{
			SavedAssetObject = Cast<UStateKeyInfoAsset>(OldPin->DefaultObject);

			// Try loading by name if DefaultObject is stale
			if (SavedAssetObject && SavedAssetObject->KeyInfos.IsEmpty())
			{
				FString AssetPathName = SavedAssetObject->GetPathName();
				UStateKeyInfoAsset* LoadedAssetAlt = Cast<UStateKeyInfoAsset>(StaticLoadObject(UStateKeyInfoAsset::StaticClass(), nullptr, *AssetPathName, nullptr, 0U, nullptr, false));
				AssetPathStr = FSoftObjectPath(LoadedAssetAlt);

				FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

				Streamable.RequestAsyncLoad(AssetPathStr, FStreamableDelegate::CreateUObject(
					this,
					&UZLK2Node_GetRequestedStateValueObjectString::OnAssetLoaded,
					AssetPathStr
				));

				SavedAssetObject = Cast<UStateKeyInfoAsset>(LoadedAssetAlt);
			}
		}

		if (OldPin->PinName == TEXT("Key") && OldPin->Direction == EGPD_Input)
		{
			SavedKeyStr = OldPin->DefaultValue;
		}
	}

	UK2Node::ReallocatePinsDuringReconstruction(OldPins);
}

void UZLK2Node_GetRequestedStateValueObjectString::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	UK2Node::ExpandNode(CompilerContext, SourceGraph);

	static FName FunctionName = GET_FUNCTION_NAME_CHECKED(UZLCloudPluginStateManagerBlueprints, GetRequestedSchemaValueObjectString);

	UFunction* TargetFunction = UZLCloudPluginStateManagerBlueprints::StaticClass()->FindFunctionByName(FunctionName);
	if (!TargetFunction)
	{
		CompilerContext.MessageLog.Error(*FString::Printf(TEXT("Function '%s' not found."), *FunctionName.ToString()));
		return;
	}

	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->SetFromFunction(TargetFunction);
	CallNode->AllocateDefaultPins();

	UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));

	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Asset")), *CallNode->FindPin(TEXT("Asset")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Key"), EGPD_Input), *CallNode->FindPin(TEXT("ParentKey")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Instant Confirm")), *CallNode->FindPin(TEXT("InstantConfirm")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Include All Fields")), *CallNode->FindPin(TEXT("MergeWithCurrent")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("OutObjectString"), EGPD_Output), *CallNode->FindPin(TEXT("OutObjectString")));

	UEdGraphPin* execTopPin = GetExecPin();
	UEdGraphPin* execInternalPin = CallNode->GetExecPin();

	UEdGraphPin* thenTopPin = GetThenPin();
	UEdGraphPin* thenInternalPin = CallNode->GetThenPin();

	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Success")), *CallNode->FindPin(TEXT("Success")));
	CompilerContext.MovePinLinksToIntermediate(*execTopPin, *execInternalPin);
	CompilerContext.MovePinLinksToIntermediate(*thenTopPin, *thenInternalPin);

	BreakAllNodeLinks();
}

void UZLK2Node_GetRequestedStateValueObjectString::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UZLK2Node_GetRequestedStateValueObjectString::GetMenuCategory() const
{
	return LOCTEXT("NodeCategory", "Zerolight Omnistream State");
}

FName UZLK2Node_GetRequestedStateValueObjectString::GetFunctionName() const
{
	return FName(TEXT("GetRequestedSchemaValueObjectString"));
}

FText UZLK2Node_GetRequestedStateValueObjectString::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Returns the requested value for a object-level key from the state manager.\nThe FJsonObject is serialised to an FString for compatability in Blueprints.\nInclude All Fields when ticked will merge request with any unrequested current state fields so the object is always the whole set of keys."); 
}

FText UZLK2Node_SetCurrentStateValue::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("SelectKeyNodeTitle", "Set Current State Value (Schema Asset)");
}

void UZLK2Node_SetCurrentStateValue::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	if (GIsReconstructingBlueprintInstances || GIsDuplicatingClassForReinstancing)
	{
		return;
	}

	Super::PinDefaultValueChanged(ChangedPin);

	if (ChangedPin && ChangedPin->PinName == TEXT("Asset"))
	{
		UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(ChangedPin->DefaultObject);
		if (!Asset || Asset->KeyInfos.IsEmpty())
			return;

		if (!HasValidBlueprint())
		{
			return;
		}

		ReconstructNode();

		UEdGraphPin* KeyNamePin = FindPin(TEXT("Key"), EGPD_Input);
		if (KeyNamePin)
		{
			KeyNamePin->DefaultValue = TEXT("Select Key");
		}
	}

	if (ChangedPin && ChangedPin->PinName == "Key")
	{
		UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
		if (AssetPin && AssetPin->DefaultObject)
		{
			const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
			if (!Asset || Asset->KeyInfos.IsEmpty())
				return;

			if (Asset->KeyInfos.Contains(ChangedPin->DefaultValue))
			{
				if (!HasValidBlueprint())
				{
					return;
				}

				ReconstructNode();
			}
		}
	}
}

void UZLK2Node_SetCurrentStateValue::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);

	UEdGraphPin* NewAssetPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Object,
		NAME_None,
		TEXT("Asset")
	);
	NewAssetPin->PinType.PinSubCategoryObject = UStateKeyInfoAsset::StaticClass();

	if (SavedAssetObject != nullptr)
	{
		NewAssetPin->DefaultObject = SavedAssetObject;
		SavedAssetObject = nullptr;
	}

	EStateKeyDataType dataType = EStateKeyDataType::Invalid;

	if (UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(NewAssetPin->DefaultObject))
	{
		if (Asset->KeyInfos.IsEmpty()) // asset is not yet loaded
		{
			FSoftObjectPath AssetPath(Asset);
			FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

			Streamable.RequestAsyncLoad(AssetPath, FStreamableDelegate::CreateUObject(
				this,
				&UZLK2Node_SetCurrentStateValue::OnAssetLoaded,
				AssetPath
			));

			//Dont skip pin creation, but use uproperty serialized saved data type to restore pins
			dataType = SavedDataType;

			//return;
		}
	}

	const FName KeySubCat(TEXT("OmniStreamSchemaKey"));
	const FName KeyName(TEXT("Key"));

	UEdGraphPin* NewKeyPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, KeySubCat, KeyName);

	if (!SavedKeyStr.IsEmpty())
	{
		NewKeyPin->DefaultValue = SavedKeyStr;
		SavedKeyStr = "";
	}

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	UEdGraphPin* ArrayPin;
	UEdGraphPin* OutPin;
	if (dataType == EStateKeyDataType::Invalid)
		dataType = GetDataType();

	if (dataType != EStateKeyDataType::Invalid)
		SavedDataType = dataType;

	switch (dataType)
	{
	case EStateKeyDataType::String:
		OutPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, TEXT("Value"));
		OutPin->PinType.PinCategory = UEdGraphSchema_K2::PC_String;
		break;
	case EStateKeyDataType::StringArray:
		ArrayPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, TEXT("Value"));
		ArrayPin->PinType.ContainerType = EPinContainerType::Array;
		break;
	case EStateKeyDataType::Number:
		OutPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Real, TEXT("Value"));
		OutPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPin->PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		break;
	case EStateKeyDataType::NumberArray:
		ArrayPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Real, TEXT("Value"));
		ArrayPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		ArrayPin->PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		ArrayPin->PinType.ContainerType = EPinContainerType::Array;
		break;
	case EStateKeyDataType::Bool:
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("Value"));
		break;
	case EStateKeyDataType::BoolArray:
		ArrayPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("Value"));
		ArrayPin->PinType.ContainerType = EPinContainerType::Array;
		break;
	case EStateKeyDataType::Invalid:
	default:
		break;
	}
}

void UZLK2Node_SetCurrentStateValue::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	for (UEdGraphPin* OldPin : OldPins)
	{
		if (OldPin->PinName == TEXT("Asset") && OldPin->Direction == EGPD_Input)
		{
			SavedAssetObject = Cast<UStateKeyInfoAsset>(OldPin->DefaultObject);

			// Try loading by name if DefaultObject is stale
			if (SavedAssetObject && SavedAssetObject->KeyInfos.IsEmpty())
			{
				FString AssetPathName = SavedAssetObject->GetPathName();
				UStateKeyInfoAsset* LoadedAssetAlt = Cast<UStateKeyInfoAsset>(StaticLoadObject(UStateKeyInfoAsset::StaticClass(), nullptr, *AssetPathName, nullptr, 0U, nullptr, false));
				AssetPathStr = FSoftObjectPath(LoadedAssetAlt);

				FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

				Streamable.RequestAsyncLoad(AssetPathStr, FStreamableDelegate::CreateUObject(
					this,
					&UZLK2Node_SetCurrentStateValue::OnAssetLoaded,
					AssetPathStr
				));

				SavedAssetObject = Cast<UStateKeyInfoAsset>(LoadedAssetAlt);
			}
		}

		if (OldPin->PinName == TEXT("Key") && OldPin->Direction == EGPD_Input)
		{
			SavedKeyStr = OldPin->DefaultValue;
		}
	}

	UK2Node::ReallocatePinsDuringReconstruction(OldPins);
}

void UZLK2Node_SetCurrentStateValue::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	UK2Node::ExpandNode(CompilerContext, SourceGraph);

	static FName FunctionName = GET_FUNCTION_NAME_CHECKED(UZLCloudPluginStateManagerBlueprints, SetCurrentSchemaValue);

	UFunction* TargetFunction = UZLCloudPluginStateManagerBlueprints::StaticClass()->FindFunctionByName(FunctionName);
	if (!TargetFunction)
	{
		CompilerContext.MessageLog.Error(*FString::Printf(TEXT("Function '%s' not found."), *FunctionName.ToString()));
		return;
	}

	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->SetFromFunction(TargetFunction);
	CallNode->AllocateDefaultPins();

	UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
	EStateKeyDataType dataType = EStateKeyDataType::Invalid;

	if (AssetPin && AssetPin->DefaultObject)
	{
		const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);

		if (Asset && Asset->KeyInfos.IsEmpty()) //Reloading from editor launch, needs to wait on full asset loaded
			dataType = SavedDataType;
	}

	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Asset")), *CallNode->FindPin(TEXT("Asset")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Key"), EGPD_Input), *CallNode->FindPin(TEXT("KeyName")));


	// Input selection logic
	FString ReturnPinName = TEXT("Value");
	FString TargetInputPin;
	if (dataType == EStateKeyDataType::Invalid)
		dataType = GetDataType();

	UK2Node_MakeArray* MakeStrArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
	MakeStrArrayNode->AllocateDefaultPins();
	UK2Node_MakeArray* MakeNumArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
	MakeNumArrayNode->AllocateDefaultPins();
	UK2Node_MakeArray* MakeBoolArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
	MakeBoolArrayNode->AllocateDefaultPins();
	UEdGraphPin* InputStrArray = CallNode->FindPin(TEXT("InStringArray"));
	UEdGraphPin* InputNumArray = CallNode->FindPin(TEXT("InNumberArray"));
	UEdGraphPin* InputBoolArray = CallNode->FindPin(TEXT("InBoolArray"));

	UEdGraphPin* StrArrayOutPin = MakeStrArrayNode->FindPin(TEXT("Array"), EGPD_Output);
	UEdGraphPin* NumArrayOutPin = MakeNumArrayNode->FindPin(TEXT("Array"), EGPD_Output);
	UEdGraphPin* BoolArrayOutPin = MakeBoolArrayNode->FindPin(TEXT("Array"), EGPD_Output);
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	K2Schema->TryCreateConnection(StrArrayOutPin, InputStrArray);
	K2Schema->TryCreateConnection(NumArrayOutPin, InputNumArray);
	K2Schema->TryCreateConnection(BoolArrayOutPin, InputBoolArray);

	//CompilerContext.MovePinLinksToIntermediate(*ArrayOutPin, *InputStrArray);
	//CompilerContext.MovePinLinksToIntermediate(*ArrayOutPin, *InputNumArray);
	//CompilerContext.MovePinLinksToIntermediate(*ArrayOutPin, *InputBoolArray);


	switch (dataType)
	{
	case EStateKeyDataType::String:
		TargetInputPin = "InString";
		break;
	case EStateKeyDataType::StringArray:
		TargetInputPin = "InStringArray";
		break;
	case EStateKeyDataType::Number:
		TargetInputPin = "InNumber";
		break;
	case EStateKeyDataType::NumberArray:
		TargetInputPin = "InNumberArray";
		break;
	case EStateKeyDataType::Bool:
		TargetInputPin = "InBool";
		break;
	case EStateKeyDataType::BoolArray:
		TargetInputPin = "InBoolArray";
		break;
	case EStateKeyDataType::Invalid:
	default:
		CompilerContext.MessageLog.Error(TEXT("Key on node @@ must be set to a valid key."), this);
		return;
	}

	UEdGraphPin* execTopPin = GetExecPin();
	UEdGraphPin* execInternalPin = CallNode->GetExecPin();

	UEdGraphPin* thenTopPin = GetThenPin();
	UEdGraphPin* thenInternalPin = CallNode->GetThenPin();

	UEdGraphPin* ValuePin = FindPin(ReturnPinName);
	UEdGraphPin* InputPin = CallNode->FindPin(*TargetInputPin);

	if (InputPin && ValuePin)
	{
		CompilerContext.MovePinLinksToIntermediate(*FindPin(ReturnPinName), *CallNode->FindPin(*TargetInputPin));
	}

	CompilerContext.MovePinLinksToIntermediate(*execTopPin, *execInternalPin);
	CompilerContext.MovePinLinksToIntermediate(*thenTopPin, *thenInternalPin);

	BreakAllNodeLinks();
}

void UZLK2Node_SetCurrentStateValue::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UZLK2Node_SetCurrentStateValue::GetMenuCategory() const
{
	return LOCTEXT("NodeCategory", "Zerolight Omnistream State");
}

FName UZLK2Node_SetCurrentStateValue::GetFunctionName() const
{
	return FName(TEXT("SetCurrentSchemaValue"));
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "UZLK2Node_PerformStateRequest"

bool UZLK2Node_PerformStateRequest::ParseSlotIndexFromPinName(const FString& PinNameStr, const TCHAR* Prefix, int32& OutSlot)
{
	if (!PinNameStr.StartsWith(Prefix))
	{
		return false;
	}
	const FString Rest = PinNameStr.RightChop(FCString::Strlen(Prefix));
	if (Rest.IsEmpty() || !Rest.IsNumeric())
	{
		return false;
	}
	OutSlot = FCString::Atoi(*Rest);
	return true;
}

static FString ZL_ExtractObjectPathFromPinDefault(const FString& RawDefault)
{
	if (RawDefault.IsEmpty())
	{
		return FString();
	}

	// K2 object default format can be either:
	//   ClassName'/Game/Path/Asset.Asset'
	// or already a plain object path.
	const int32 FirstQuote = RawDefault.Find(TEXT("'"));
	if (FirstQuote != INDEX_NONE)
	{
		const int32 SecondQuote = RawDefault.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FirstQuote + 1);
		if (SecondQuote != INDEX_NONE)
		{
			return RawDefault.Mid(FirstQuote + 1, SecondQuote - FirstQuote - 1);
		}
	}
	return RawDefault;
}

EStateKeyDataType UZLK2Node_PerformStateRequest::PinTypeToValueDataType(const UEdGraphPin* ValuePin)
{
	if (!ValuePin)
	{
		return EStateKeyDataType::Invalid;
	}
	if (ValuePin->PinType.ContainerType == EPinContainerType::Array)
	{
		if (ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_String)
		{
			return EStateKeyDataType::StringArray;
		}
		if (ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real && ValuePin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float)
		{
			return EStateKeyDataType::NumberArray;
		}
		if (ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return EStateKeyDataType::BoolArray;
		}
	}
	else
	{
		if (ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_String)
		{
			return EStateKeyDataType::String;
		}
		if (ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real && ValuePin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float)
		{
			return EStateKeyDataType::Number;
		}
		if (ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return EStateKeyDataType::Bool;
		}
	}
	return EStateKeyDataType::Invalid;
}

EStateKeyDataType UZLK2Node_PerformStateRequest::GetDataTypeForSlot(int32 SlotIndex) const
{
	UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
	const FName KeyPinName(*FString::Printf(TEXT("Key_%d"), SlotIndex));
	UEdGraphPin* KeyPin = FindPin(KeyPinName);
	if (AssetPin && KeyPin && AssetPin->DefaultObject)
	{
		const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
		if (!Asset)
		{
			return EStateKeyDataType::Invalid;
		}
		if (Asset->KeyInfos.Contains(KeyPin->DefaultValue))
		{
			return Asset->KeyInfos[KeyPin->DefaultValue].GetDataTypeEnum();
		}
	}
	return EStateKeyDataType::Invalid;
}

FText UZLK2Node_PerformStateRequest::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("PerformStateRequestTitle", "Perform State Request (Schema Asset)");
}

FText UZLK2Node_PerformStateRequest::GetTooltipText() const
{
	return LOCTEXT(
		"PerformStateRequestTooltip",
		"Build a JSON state object from multiple schema keys and broadcast it on OnRecieveData (same path as remote state). "
		"Right-click the node (not a pin) and use the State Request menu to add/remove slots, or edit Num Key Slots in the Details panel if your editor shows graph node properties.");
}

void UZLK2Node_PerformStateRequest::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if (!Context || Context->bIsDebugging || Context->Pin != nullptr)
	{
		return;
	}

	FToolMenuSection& Section = Menu->AddSection(
		"ZLPerformStateRequest",
		LOCTEXT("ZLPerformStateRequestSection", "State Request"));

	UZLK2Node_PerformStateRequest* MutableThis = const_cast<UZLK2Node_PerformStateRequest*>(this);

	Section.AddMenuEntry(
		"ZLAddKeyToRequestCtx",
		LOCTEXT("ZLAddKeyToRequestCtx", "Add Key to request"),
		LOCTEXT("ZLAddKeyToRequestCtxTooltip", "Adds another schema key/value slot (Key_N / Value_N)."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateUObject(MutableThis, &UZLK2Node_PerformStateRequest::AddKeyToRequest)));

	Section.AddMenuEntry(
		"ZLRemoveLastKeyCtx",
		LOCTEXT("ZLRemoveLastKeyCtx", "Remove Key from request"),
		LOCTEXT("ZLRemoveLastKeyCtxTooltip", "Removes the highest-index key/value slot. At least one slot remains."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateUObject(MutableThis, &UZLK2Node_PerformStateRequest::RemoveLastKeyFromRequest),
			FCanExecuteAction::CreateUObject(MutableThis, &UZLK2Node_PerformStateRequest::CanRemoveLastKeySlot)));
}

void UZLK2Node_PerformStateRequest::ScheduleVisualRefreshAfterSlotChange()
{
#if WITH_EDITOR
	if (GEditor)
	{
		TWeakObjectPtr<UZLK2Node_PerformStateRequest> WeakThis(this);
		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis]()
		{
			if (UZLK2Node_PerformStateRequest* Node = WeakThis.Get())
			{
				if (UEdGraph* Graph = Node->GetGraph())
				{
#if UNREAL_5_3_OR_NEWER
					Graph->NotifyNodeChanged(Node);
#else
					Graph->NotifyGraphChanged();
#endif	
				}
			}
		}));
	}
#endif
}

void UZLK2Node_PerformStateRequest::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UZLK2Node_PerformStateRequest, NumKeySlots))
	{
		NumKeySlots = FMath::Max(1, NumKeySlots);
		ReconstructNode();
		ScheduleVisualRefreshAfterSlotChange();
		if (UBlueprint* BP = GetBlueprint())
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		}
	}
}

void UZLK2Node_PerformStateRequest::AddKeyToRequest()
{
	NumKeySlots++;
	Modify();
	ReconstructNode();
	ScheduleVisualRefreshAfterSlotChange();
	if (UBlueprint* BP = GetBlueprint())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}
}

void UZLK2Node_PerformStateRequest::RemoveLastKeyFromRequest()
{
	if (NumKeySlots <= 1)
	{
		return;
	}
	OrphanedPinSaveMode = ESaveOrphanPinMode::SaveNone;
	NumKeySlots--;
	Modify();
	ReconstructNode();
	ScheduleVisualRefreshAfterSlotChange();
	if (UBlueprint* BP = GetBlueprint())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}
}

void UZLK2Node_PerformStateRequest::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	if (GIsReconstructingBlueprintInstances || GIsDuplicatingClassForReinstancing)
	{
		return;
	}

	Super::PinDefaultValueChanged(ChangedPin);

	if (ChangedPin && ChangedPin->PinName == TEXT("Asset"))
	{
		UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(ChangedPin->DefaultObject);
		if (!Asset || Asset->KeyInfos.IsEmpty())
		{
			return;
		}

		if (!HasValidBlueprint())
		{
			return;
		}

		SavedKeyPerSlot.Empty();
		SavedDataTypePerSlot.Empty();
		SavedValuePerSlot.Empty();
		ReconstructNode();

		for (int32 Slot = 0; Slot < NumKeySlots; ++Slot)
		{
			if (UEdGraphPin* KeyPin = FindPin(*FString::Printf(TEXT("Key_%d"), Slot), EGPD_Input))
			{
				KeyPin->DefaultValue = TEXT("Select Key");
			}
		}
	}

	const FString ChangedName = ChangedPin->PinName.ToString();
	if (ChangedPin && ChangedName.StartsWith(TEXT("Key_")))
	{
		UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
		if (!AssetPin || !AssetPin->DefaultObject)
		{
			return;
		}
		const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
		if (!Asset || Asset->KeyInfos.IsEmpty())
		{
			return;
		}
		if (Asset->KeyInfos.Contains(ChangedPin->DefaultValue))
		{
			if (!HasValidBlueprint())
			{
				return;
			}

			ReconstructNode();
		}
	}
}

void UZLK2Node_PerformStateRequest::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);

	UEdGraphPin* NewAssetPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Object,
		NAME_None,
		TEXT("Asset")
	);
	NewAssetPin->PinType.PinSubCategoryObject = UStateKeyInfoAsset::StaticClass();

	if (SavedAssetObject != nullptr)
	{
		NewAssetPin->DefaultObject = SavedAssetObject;
		SavedAssetObject = nullptr;
	}

	UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(NewAssetPin->DefaultObject);
	if (Asset && Asset->KeyInfos.IsEmpty())
	{
		FSoftObjectPath Path(Asset);
		FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
		Streamable.RequestAsyncLoad(Path, FStreamableDelegate::CreateUObject(this, &UZLK2Node_PerformStateRequest::OnAssetLoaded, Path));
	}

	const FName KeySubCat(TEXT("OmniStreamSchemaKey"));

	for (int32 Slot = 0; Slot < NumKeySlots; ++Slot)
	{
		UEdGraphPin* KeyPin = CreatePin(
			EGPD_Input,
			UEdGraphSchema_K2::PC_String,
			KeySubCat,
			*FString::Printf(TEXT("Key_%d"), Slot)
		);
		if (SavedKeyPerSlot.IsValidIndex(Slot) && !SavedKeyPerSlot[Slot].IsEmpty())
		{
			KeyPin->DefaultValue = SavedKeyPerSlot[Slot];
		}

		EStateKeyDataType dataType = GetDataTypeForSlot(Slot);
		if (dataType == EStateKeyDataType::Invalid)
		{
			dataType = SavedDataTypePerSlot.IsValidIndex(Slot) ? SavedDataTypePerSlot[Slot] : EStateKeyDataType::Invalid;
		}
		if (dataType != EStateKeyDataType::Invalid)
		{
			if (SavedDataTypePerSlot.Num() <= Slot)
			{
				SavedDataTypePerSlot.SetNum(Slot + 1);
			}
			SavedDataTypePerSlot[Slot] = dataType;
		}

		const auto SetValuePinLabel = [&](UEdGraphPin* ValuePin)
		{
			if (!ValuePin)
			{
				return;
			}
			const FString K = KeyPin->DefaultValue;
			if (K.IsEmpty() || K == TEXT("Select Key"))
			{
				ValuePin->PinFriendlyName = FText::Format(LOCTEXT("ValueSlotFallback", "Value {0}"), FText::AsNumber(Slot));
			}
			else
			{
				ValuePin->PinFriendlyName = FText::FromString(FString::Printf(TEXT("%s Value"), *K));
			}
		};

		const auto RestoreValuePinDefault = [&](UEdGraphPin* ValuePin)
		{
			if (!ValuePin || !SavedValuePerSlot.IsValidIndex(Slot))
			{
				return;
			}
			const FString& SavedDefaultValue = SavedValuePerSlot[Slot];
			if (SavedDefaultValue.IsEmpty())
			{
				return;
			}

			if (const UEdGraphSchema* PinSchema = ValuePin->GetSchema())
			{
				PinSchema->TrySetDefaultValue(*ValuePin, SavedDefaultValue);
			}
			else
			{
				ValuePin->DefaultValue = SavedDefaultValue;
			}
		};

		const FName ValueName(*FString::Printf(TEXT("Value_%d"), Slot));
		switch (dataType)
		{
		case EStateKeyDataType::String:
			{
				UEdGraphPin* P = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, ValueName);
				P->PinType.PinCategory = UEdGraphSchema_K2::PC_String;
				SetValuePinLabel(P);
				RestoreValuePinDefault(P);
				break;
			}
		case EStateKeyDataType::StringArray:
			{
				UEdGraphPin* P = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, ValueName);
				P->PinType.ContainerType = EPinContainerType::Array;
				SetValuePinLabel(P);
				RestoreValuePinDefault(P);
				break;
			}
		case EStateKeyDataType::Number:
			{
				UEdGraphPin* P = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Real, ValueName);
				P->PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
				P->PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
				SetValuePinLabel(P);
				RestoreValuePinDefault(P);
				break;
			}
		case EStateKeyDataType::NumberArray:
			{
				UEdGraphPin* P = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Real, ValueName);
				P->PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
				P->PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
				P->PinType.ContainerType = EPinContainerType::Array;
				SetValuePinLabel(P);
				RestoreValuePinDefault(P);
				break;
			}
		case EStateKeyDataType::Bool:
			{
				UEdGraphPin* P = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, ValueName);
				SetValuePinLabel(P);
				RestoreValuePinDefault(P);
				break;
			}
		case EStateKeyDataType::BoolArray:
			{
				UEdGraphPin* P = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, ValueName);
				P->PinType.ContainerType = EPinContainerType::Array;
				SetValuePinLabel(P);
				RestoreValuePinDefault(P);
				break;
			}
		case EStateKeyDataType::Invalid:
		default:
			break;
		}
	}

	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("doCurrentStateCompare"));

	UEdGraphPin* SuccessPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, TEXT("Success"));
	SuccessPin->PinType.bIsReference = true;

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
}

void UZLK2Node_PerformStateRequest::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	SavedKeyPerSlot.Empty();
	SavedDataTypePerSlot.Empty();
	SavedValuePerSlot.Empty();

	for (UEdGraphPin* OldPin : OldPins)
	{
		if (OldPin->PinName == TEXT("Asset") && OldPin->Direction == EGPD_Input)
		{
			SavedAssetObject = Cast<UStateKeyInfoAsset>(OldPin->DefaultObject);
			if (!SavedAssetObject)
			{
				FString AssetPathRaw = OldPin->DefaultValue;
				if (AssetPathRaw.IsEmpty())
				{
					AssetPathRaw = OldPin->GetDefaultAsString();
				}
				const FString AssetPathName = ZL_ExtractObjectPathFromPinDefault(AssetPathRaw);
				if (!AssetPathName.IsEmpty())
				{
					SavedAssetObject = Cast<UStateKeyInfoAsset>(
						StaticLoadObject(UStateKeyInfoAsset::StaticClass(), nullptr, *AssetPathName, nullptr, LOAD_None, nullptr, false));
				}
			}
			if (SavedAssetObject && SavedAssetObject->KeyInfos.IsEmpty())
			{
				FString AssetPathName = SavedAssetObject->GetPathName();
				UStateKeyInfoAsset* LoadedAssetAlt = Cast<UStateKeyInfoAsset>(
					StaticLoadObject(UStateKeyInfoAsset::StaticClass(), nullptr, *AssetPathName, nullptr, LOAD_None, nullptr, false));
				AssetPathStr = FSoftObjectPath(LoadedAssetAlt);
				FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
				Streamable.RequestAsyncLoad(AssetPathStr, FStreamableDelegate::CreateUObject(this, &UZLK2Node_PerformStateRequest::OnAssetLoaded, AssetPathStr));
				SavedAssetObject = LoadedAssetAlt;
			}
		}

		const FString PinStr = OldPin->PinName.ToString();
		int32 SlotIdx = INDEX_NONE;
		if (ParseSlotIndexFromPinName(PinStr, TEXT("Key_"), SlotIdx))
		{
			if (SlotIdx < 0 || SlotIdx >= NumKeySlots)
			{
				continue;
			}
			if (!SavedKeyPerSlot.IsValidIndex(SlotIdx) || SavedKeyPerSlot.Num() <= SlotIdx)
			{
				const int32 NewNum = FMath::Max(SavedKeyPerSlot.Num(), SlotIdx + 1);
				SavedKeyPerSlot.SetNum(NewNum);
			}
			SavedKeyPerSlot[SlotIdx] = OldPin->DefaultValue;
		}
		else if (ParseSlotIndexFromPinName(PinStr, TEXT("Value_"), SlotIdx))
		{
			if (SlotIdx < 0 || SlotIdx >= NumKeySlots)
			{
				continue;
			}
			const EStateKeyDataType T = PinTypeToValueDataType(OldPin);
			if (!SavedDataTypePerSlot.IsValidIndex(SlotIdx) || SavedDataTypePerSlot.Num() <= SlotIdx)
			{
				const int32 NewNum = FMath::Max(SavedDataTypePerSlot.Num(), SlotIdx + 1);
				SavedDataTypePerSlot.SetNum(NewNum);
			}
			SavedDataTypePerSlot[SlotIdx] = T;

			if (!SavedValuePerSlot.IsValidIndex(SlotIdx) || SavedValuePerSlot.Num() <= SlotIdx)
			{
				const int32 NewNum = FMath::Max(SavedValuePerSlot.Num(), SlotIdx + 1);
				SavedValuePerSlot.SetNum(NewNum);
			}
			SavedValuePerSlot[SlotIdx] = OldPin->DefaultValue;
		}
	}

	NumKeySlots = FMath::Max(1, NumKeySlots);
	if (SavedKeyPerSlot.Num() > NumKeySlots)
	{
		SavedKeyPerSlot.SetNum(NumKeySlots);
	}
	if (SavedDataTypePerSlot.Num() > NumKeySlots)
	{
		SavedDataTypePerSlot.SetNum(NumKeySlots);
	}
	if (SavedValuePerSlot.Num() > NumKeySlots)
	{
		SavedValuePerSlot.SetNum(NumKeySlots);
	}

	UK2Node::ReallocatePinsDuringReconstruction(OldPins);
}

void UZLK2Node_PerformStateRequest::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	UK2Node::ExpandNode(CompilerContext, SourceGraph);

	static FName MergeFnName = GET_FUNCTION_NAME_CHECKED(UZLCloudPluginStateManagerBlueprints, MergeSchemaKeyIntoStateRequestJson);
	static FName BroadcastFnName = GET_FUNCTION_NAME_CHECKED(UZLCloudPluginStateManagerBlueprints, BroadcastStateRequestJsonAsIncomingData);

	UFunction* MergeUFunction = UZLCloudPluginStateManagerBlueprints::StaticClass()->FindFunctionByName(MergeFnName);
	UFunction* BroadcastUFunction = UZLCloudPluginStateManagerBlueprints::StaticClass()->FindFunctionByName(BroadcastFnName);
	if (!MergeUFunction || !BroadcastUFunction)
	{
		CompilerContext.MessageLog.Error(TEXT("Perform State Request: required Blueprint library function not found."));
		return;
	}

	static FName MakeLiteralName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralString);
	UFunction* LiteralFn = UKismetSystemLibrary::StaticClass()->FindFunctionByName(MakeLiteralName);
	if (!LiteralFn)
	{
		CompilerContext.MessageLog.Error(TEXT("Perform State Request: MakeLiteralString not found."));
		return;
	}

	UK2Node_CallFunction* LiteralNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	LiteralNode->SetFromFunction(LiteralFn);
	LiteralNode->AllocateDefaultPins();
	if (UEdGraphPin* ValPin = LiteralNode->FindPin(TEXT("Value")))
	{
		ValPin->DefaultValue = TEXT("{}");
	}
	UEdGraphPin* JsonAccumulator = LiteralNode->GetReturnValuePin();

	UEdGraphPin* UserAssetPin = FindPin(TEXT("Asset"));
	TArray<UEdGraphPin*> AssetSourcePins;
	UObject* AssetDefaultObj = nullptr;
	if (UserAssetPin)
	{
		AssetSourcePins = UserAssetPin->LinkedTo;
		AssetDefaultObj = UserAssetPin->DefaultObject;
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	TArray<UK2Node_CallFunction*> MergeNodes;
	MergeNodes.Reserve(NumKeySlots);

	for (int32 Slot = 0; Slot < NumKeySlots; ++Slot)
	{
		UK2Node_CallFunction* MergeCall = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		MergeCall->SetFromFunction(MergeUFunction);
		MergeCall->AllocateDefaultPins();
		MergeNodes.Add(MergeCall);

		UEdGraphPin* ExistingIn = MergeCall->FindPin(TEXT("ExistingJson"));
		if (!JsonAccumulator || !ExistingIn || !K2Schema->TryCreateConnection(JsonAccumulator, ExistingIn))
		{
			CompilerContext.MessageLog.Error(TEXT("Perform State Request: failed to wire ExistingJson chain."), this);
			return;
		}
		JsonAccumulator = MergeCall->GetReturnValuePin();

		UK2Node_MakeArray* MakeStrArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
		MakeStrArrayNode->AllocateDefaultPins();
		UK2Node_MakeArray* MakeNumArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
		MakeNumArrayNode->AllocateDefaultPins();
		UK2Node_MakeArray* MakeBoolArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
		MakeBoolArrayNode->AllocateDefaultPins();

		UEdGraphPin* InputStrArray = MergeCall->FindPin(TEXT("InStringArray"));
		UEdGraphPin* InputNumArray = MergeCall->FindPin(TEXT("InNumberArray"));
		UEdGraphPin* InputBoolArray = MergeCall->FindPin(TEXT("InBoolArray"));
		K2Schema->TryCreateConnection(MakeStrArrayNode->GetOutputPin(), InputStrArray);
		K2Schema->TryCreateConnection(MakeNumArrayNode->GetOutputPin(), InputNumArray);
		K2Schema->TryCreateConnection(MakeBoolArrayNode->GetOutputPin(), InputBoolArray);

		EStateKeyDataType dataType = EStateKeyDataType::Invalid;
		if (UserAssetPin && UserAssetPin->DefaultObject)
		{
			const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(UserAssetPin->DefaultObject);
			if (Asset && Asset->KeyInfos.IsEmpty())
			{
				dataType = SavedDataTypePerSlot.IsValidIndex(Slot) ? SavedDataTypePerSlot[Slot] : EStateKeyDataType::Invalid;
			}
		}
		if (dataType == EStateKeyDataType::Invalid)
		{
			dataType = GetDataTypeForSlot(Slot);
		}
		if (dataType == EStateKeyDataType::Invalid)
		{
			dataType = SavedDataTypePerSlot.IsValidIndex(Slot) ? SavedDataTypePerSlot[Slot] : EStateKeyDataType::Invalid;
		}

		FString TargetInputPin;
		switch (dataType)
		{
		case EStateKeyDataType::String:
			TargetInputPin = TEXT("InString");
			break;
		case EStateKeyDataType::StringArray:
			TargetInputPin = TEXT("InStringArray");
			break;
		case EStateKeyDataType::Number:
			TargetInputPin = TEXT("InNumber");
			break;
		case EStateKeyDataType::NumberArray:
			TargetInputPin = TEXT("InNumberArray");
			break;
		case EStateKeyDataType::Bool:
			TargetInputPin = TEXT("InBool");
			break;
		case EStateKeyDataType::BoolArray:
			TargetInputPin = TEXT("InBoolArray");
			break;
		case EStateKeyDataType::Invalid:
		default:
			CompilerContext.MessageLog.Error(TEXT("Key on node @@ must be set to a valid key (or slot type unknown)."), this);
			return;
		}

		const FName KeyPinName(*FString::Printf(TEXT("Key_%d"), Slot));
		const FName ValuePinName(*FString::Printf(TEXT("Value_%d"), Slot));
		UEdGraphPin* UserKeyPin = FindPin(KeyPinName, EGPD_Input);
		UEdGraphPin* UserValuePin = FindPin(ValuePinName, EGPD_Input);
		UEdGraphPin* MergeKeyPin = MergeCall->FindPin(TEXT("KeyName"));
		UEdGraphPin* MergeValueIn = MergeCall->FindPin(*TargetInputPin);

		if (!UserKeyPin || !MergeKeyPin)
		{
			CompilerContext.MessageLog.Error(TEXT("Perform State Request: missing or invalid key pins."), this);
			return;
		}

		CompilerContext.MovePinLinksToIntermediate(*UserKeyPin, *MergeKeyPin);

		if (TargetInputPin != TEXT("InStringArray") && TargetInputPin != TEXT("InNumberArray") && TargetInputPin != TEXT("InBoolArray"))
		{
			if (!UserValuePin || !MergeValueIn)
			{
				CompilerContext.MessageLog.Error(TEXT("Perform State Request: missing value pin."), this);
				return;
			}
			CompilerContext.MovePinLinksToIntermediate(*UserValuePin, *MergeValueIn);
		}
		else
		{
			if (!UserValuePin || !MergeValueIn)
			{
				CompilerContext.MessageLog.Error(TEXT("Perform State Request: missing array value pin."), this);
				return;
			}
			CompilerContext.MovePinLinksToIntermediate(*UserValuePin, *MergeValueIn);
		}
	}

	for (UK2Node_CallFunction* MergeCall : MergeNodes)
	{
		UEdGraphPin* MergeAsset = MergeCall->FindPin(TEXT("Asset"));
		if (!MergeAsset)
		{
			continue;
		}
		for (UEdGraphPin* Src : AssetSourcePins)
		{
			K2Schema->TryCreateConnection(Src, MergeAsset);
		}
		if (AssetSourcePins.Num() == 0 && AssetDefaultObj)
		{
			MergeAsset->DefaultObject = AssetDefaultObj;
		}
	}

	UK2Node_CallFunction* BroadcastCall = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	BroadcastCall->SetFromFunction(BroadcastUFunction);
	BroadcastCall->AllocateDefaultPins();

	if (!JsonAccumulator)
	{
		CompilerContext.MessageLog.Error(TEXT("Perform State Request: invalid JSON accumulator."), this);
		return;
	}

	UEdGraphPin* BroadcastJsonPin = BroadcastCall->FindPin(TEXT("jsonString"));
	if (!BroadcastJsonPin || !K2Schema->TryCreateConnection(JsonAccumulator, BroadcastJsonPin))
	{
		CompilerContext.MessageLog.Error(TEXT("Perform State Request: failed to wire jsonString to BroadcastStateRequestJsonAsIncomingData."), this);
		return;
	}

	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *BroadcastCall->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(*GetThenPin(), *BroadcastCall->GetThenPin());

	if (UEdGraphPin* SuccUser = FindPin(TEXT("Success"), EGPD_Output))
	{
		if (UEdGraphPin* SuccFn = BroadcastCall->FindPin(TEXT("Success")))
		{
			CompilerContext.MovePinLinksToIntermediate(*SuccUser, *SuccFn);
		}
	}

	BreakAllNodeLinks();
}

void UZLK2Node_PerformStateRequest::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UZLK2Node_PerformStateRequest::GetMenuCategory() const
{
	return LOCTEXT("NodeCategoryPerform", "Zerolight Omnistream State");
}

void UZLK2Node_PerformStateRequest::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	const UEdGraphPin* SchemaPin = FindPin(TEXT("Asset"));
	const UStateKeyInfoAsset* SchemaAsset = SchemaPin ? Cast<UStateKeyInfoAsset>(SchemaPin->DefaultObject) : nullptr;
	if (SchemaPin && !SchemaPin->DefaultObject)
	{
		MessageLog.Error(TEXT("Schema must be assigned on node @@"), this);
	}

	TSet<FString> UsedKeys;
	for (int32 Slot = 0; Slot < NumKeySlots; ++Slot)
	{
		const FName KeyPinName(*FString::Printf(TEXT("Key_%d"), Slot));
		const UEdGraphPin* KeyPin = FindPin(KeyPinName);
		if (!KeyPin || KeyPin->DefaultValue == TEXT("Select Key") || KeyPin->DefaultValue.IsEmpty())
		{
			MessageLog.Error(*FString::Printf(TEXT("Key slot %d on node @@ must be set to a valid key."), Slot), this);
			continue;
		}

		const FString KeyStr = KeyPin->DefaultValue;
		if (UsedKeys.Contains(KeyStr))
		{
			MessageLog.Error(
				*FString::Printf(
					TEXT("State request on node @@ lists key \"%s\" more than once; each slot must use a unique key."),
					*KeyStr),
				this);
		}
		else
		{
			UsedKeys.Add(KeyStr);
		}

		const FName ValuePinName(*FString::Printf(TEXT("Value_%d"), Slot));
		const UEdGraphPin* ValuePin = FindPin(ValuePinName);
		if (!ValuePin)
		{
			if (SavedValuePerSlot.IsValidIndex(Slot) && !SavedValuePerSlot[Slot].IsEmpty())
			{
				MessageLog.Warning(
					*FString::Printf(
						TEXT("Key \"%s\" (slot %d) on node @@ lost a previously saved value default during reconstruction."),
						*KeyStr,
						Slot),
					this);
			}
			MessageLog.Error(*FString::Printf(TEXT("Key \"%s\" (slot %d) on node @@ has no value pin."), *KeyStr, Slot), this);
			continue;
		}

		if (ValuePin->PinType.ContainerType != EPinContainerType::None)
		{
			continue;
		}

		if (ValuePin->LinkedTo.Num() > 0)
		{
			continue;
		}

		const FString ValStr = ValuePin->DefaultValue;
		const FStateKeyInfo* Info = SchemaAsset ? SchemaAsset->KeyInfos.Find(KeyStr) : nullptr;

		// Only enforce non-empty / no "Select value" when the pin uses the schema dropdown (same rules as ZL_ValuePinNeedsConstrainedWidget).
		// Free-typed string/number pins may legitimately use empty string, 0.0, etc.
		const bool bUsesAcceptedValueList =
			Info && Info->bLimitValues && ValuePin->PinType.ContainerType == EPinContainerType::None
			&& ((ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_String && Info->AcceptedStringValues.Num() > 0)
				|| (ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real
					&& ValuePin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float
					&& Info->AcceptedNumberValues.Num() > 0));

		if (bUsesAcceptedValueList && (ValStr.IsEmpty() || ValStr.Equals(TEXT("Select value"), ESearchCase::IgnoreCase)))
		{
			MessageLog.Error(
				*FString::Printf(
					TEXT("Key \"%s\" (slot %d) on node @@ must pick a value from the schema list when the pin is not wired (\"Select value\" and empty are invalid)."),
					*KeyStr,
					Slot),
				this);
			continue;
		}

		if (!SchemaAsset)
		{
			continue;
		}
		if (!Info || !Info->bLimitValues)
		{
			continue;
		}

		const EStateKeyDataType DataType = Info->GetDataTypeEnum();
		if (DataType == EStateKeyDataType::String && Info->AcceptedStringValues.Num() > 0)
		{
			if (!Info->AcceptedStringValues.Contains(ValStr))
			{
				MessageLog.Error(
					*FString::Printf(
						TEXT("Key \"%s\" (slot %d) on node @@ has value \"%s\", which is not in the schema allowed list for that key."),
						*KeyStr,
						Slot,
						*ValStr),
					this);
			}
		}
		else if (DataType == EStateKeyDataType::Number && Info->AcceptedNumberValues.Num() > 0)
		{
			const float PinVal = FCString::Atof(*ValStr);
			bool bMatched = false;
			for (double Accepted : Info->AcceptedNumberValues)
			{
				if (FMath::IsNearlyEqual(PinVal, static_cast<float>(Accepted), KINDA_SMALL_NUMBER))
				{
					bMatched = true;
					break;
				}
			}
			if (!bMatched)
			{
				MessageLog.Error(
					*FString::Printf(
						TEXT("Key \"%s\" (slot %d) on node @@ has numeric value \"%s\", which does not match any allowed value for that key."),
						*KeyStr,
						Slot,
						*ValStr),
					this);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

void UZLK2Node_GetRequestedStateValue::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	ZL_TryRemapStateSchemaOnZLK2Node(this);
#endif
}

void UZLK2Node_GetRequestedStateValueSubKeys::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	ZL_TryRemapStateSchemaOnZLK2Node(this);
#endif
}

void UZLK2Node_GetRequestedStateValueObjectString::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	ZL_TryRemapStateSchemaOnZLK2Node(this);
#endif
}

void UZLK2Node_SetCurrentStateValue::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	ZL_TryRemapStateSchemaOnZLK2Node(this);
#endif
}

void UZLK2Node_PerformStateRequest::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	ZL_TryRemapStateSchemaOnZLK2Node(this);
#endif
}

TSharedPtr<SGraphPin> FSchemaKeyPinFactory::CreatePin(UEdGraphPin* InPin) const
{
	if (!InPin)
	{
		return nullptr;
	}
	if (InPin->PinType.PinSubCategory == FName(TEXT("OmniStreamSchemaKey")) && InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_String)
	{
		return SNew(SGraphPin_KeySelector, InPin);
	}
	if (ZL_ValuePinNeedsConstrainedWidget(InPin))
	{
		return SNew(SGraphPin_SchemaConstrainedValue, InPin);
	}
	return nullptr;
}