//Copyright ZeroLight ltd.All Rights Reserved.

#include "ZLK2Nodes.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "K2Node_CallFunction.h"
#include "BlueprintNodeSpawner.h"
#include "ZLCloudPluginStateManager.h"
#include "K2Node.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Engine/AssetManager.h"

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

	for (const auto& Pair : Asset->KeyInfos)
	{
		Keys.Add(MakeShared<FString>(Pair.Key));
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

FText UZLK2Node_GetRequestedStateValue::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return LOCTEXT("SelectKeyNodeTitle", "Get Requested State Value (Schema Asset)");
}

void UZLK2Node_GetRequestedStateValue::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	Super::PinDefaultValueChanged(ChangedPin);

	if (ChangedPin && ChangedPin->PinName == TEXT("Asset"))
	{
		ReconstructNode();

		UEdGraphPin* KeyNamePin = FindPin(TEXT("Key"));
		if (KeyNamePin)
		{
			KeyNamePin->DefaultValue = TEXT("Select Key");
		}
	}

	if (ChangedPin && ChangedPin->PinName == "Key")
	{
		UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
		if (AssetPin || AssetPin->DefaultObject)
		{
			const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
			if (!Asset)
				return;
	
			if (Asset->KeyInfos.Contains(ChangedPin->DefaultValue))
			{
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
	if(dataType == EStateKeyDataType::Invalid)
		dataType = GetDataType();

	if (dataType != EStateKeyDataType::Invalid)
		SavedDataType = dataType;

	switch (dataType)
	{
	case EStateKeyDataType::String:
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_String, TEXT("Value"));
		break;
	case EStateKeyDataType::StringArray:
		ArrayPin = CreatePin( EGPD_Output, UEdGraphSchema_K2::PC_String, TEXT("Value"));
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
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Key")), *CallNode->FindPin(TEXT("KeyName")));
	CompilerContext.MovePinLinksToIntermediate(*FindPin(TEXT("Instant Confirm")), *CallNode->FindPin(TEXT("InstantConfirm")));

	// Output selection logic
	FString ReturnPinName = TEXT("Value");
	FString TargetOutputPin;
	if(dataType == EStateKeyDataType::Invalid)
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



#undef LOCTEXT_NAMESPACE