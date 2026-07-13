// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLSchemaK2NodeRemap.h"
#include "ZLK2Nodes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogZLSchemaK2Remap, Log, All);

namespace ZLSchemaRemap_Private
{
/** Reconstructs pins after schema remap; safe no-op when the node has no owning blueprint yet. */
static bool TryReconstructRemappedNode(UK2Node* Node)
{
	if (!Node || !Node->HasValidBlueprint())
	{
		return false;
	}
	Node->ReconstructNode();
	if (UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(Node))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}
	return true;
}

static FString ExtractPathFromK2ObjectPinDefault(const FString& RawDefault)
{
	if (RawDefault.IsEmpty())
	{
		return FString();
	}
	// K2 default: ClassName'/Game/Path/Asset.Asset'
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

static FString GetObjectPathFromAssetPin(const UEdGraphPin* AssetPin)
{
	if (!AssetPin)
	{
		return FString();
	}

	// For object pins, serialized fallback may not always be in DefaultValue;
	// GetDefaultAsString is the canonical engine representation.
	FString RawDefault = AssetPin->DefaultValue;
	if (RawDefault.IsEmpty())
	{
		RawDefault = AssetPin->GetDefaultAsString();
	}
	if (RawDefault.IsEmpty() && !AssetPin->DefaultTextValue.IsEmpty())
	{
		RawDefault = AssetPin->DefaultTextValue.ToString();
	}
	return ExtractPathFromK2ObjectPinDefault(RawDefault);
}

static FString GetHintAssetNameFromPin(const UEdGraphPin* AssetPin)
{
	if (!AssetPin)
	{
		return FString();
	}
	const FString PathStr = GetObjectPathFromAssetPin(AssetPin);
	if (PathStr.IsEmpty())
	{
		return FString();
	}
	return FPaths::GetBaseFilename(PathStr);
}

static UStateKeyInfoAsset* TryResolveSchemaFromPin(const UEdGraphPin* AssetPin)
{
	if (!AssetPin)
	{
		return nullptr;
	}
	if (UStateKeyInfoAsset* Direct = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject))
	{
		Direct->ConditionalPostLoad();
		return Direct;
	}
	const FString PathStr = GetObjectPathFromAssetPin(AssetPin);
	if (PathStr.IsEmpty())
	{
		return nullptr;
	}
	const FSoftObjectPath SoftPath(PathStr);
	if (UObject* Obj = SoftPath.ResolveObject())
	{
		if (UStateKeyInfoAsset* A = Cast<UStateKeyInfoAsset>(Obj))
		{
			A->ConditionalPostLoad();
			return A;
		}
	}
	if (UObject* Loaded = SoftPath.TryLoad())
	{
		if (UStateKeyInfoAsset* A = Cast<UStateKeyInfoAsset>(Loaded))
		{
			A->ConditionalPostLoad();
			return A;
		}
	}
	if (UObject* Loaded = StaticLoadObject(UStateKeyInfoAsset::StaticClass(), nullptr, *PathStr, nullptr, LOAD_None, nullptr))
	{
		if (UStateKeyInfoAsset* A = Cast<UStateKeyInfoAsset>(Loaded))
		{
			A->ConditionalPostLoad();
			return A;
		}
	}
	return nullptr;
}

static bool IsSchemaRegisteredInProject(const UStateKeyInfoAsset* Schema)
{
	if (!Schema)
	{
		return false;
	}

	FAssetRegistryModule& RegModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegModule.Get();
	const FAssetData AssetData = Registry.GetAssetByObjectPath(FSoftObjectPath(Schema));
	return AssetData.IsValid() && AssetData.AssetClassPath == UStateKeyInfoAsset::StaticClass()->GetClassPathName();
}

static bool IsAssignedSchemaPresentInProject(const UEdGraphPin* AssetPin, const UStateKeyInfoAsset* ResolvedSchema)
{
	if (!AssetPin)
	{
		return false;
	}

	if (IsSchemaRegisteredInProject(ResolvedSchema))
	{
		return true;
	}

	const FString PathStr = GetObjectPathFromAssetPin(AssetPin);
	if (PathStr.IsEmpty())
	{
		return false;
	}

	FAssetRegistryModule& RegModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegModule.Get();
	const FAssetData AssetData = Registry.GetAssetByObjectPath(FSoftObjectPath(PathStr));
	return AssetData.IsValid() && AssetData.AssetClassPath == UStateKeyInfoAsset::StaticClass()->GetClassPathName();
}

static bool SchemaHasAllKeys(const UStateKeyInfoAsset* Schema, const TArray<FString>& Keys)
{
	if (!Schema)
	{
		return false;
	}
	for (const FString& K : Keys)
	{
		if (!Schema->KeyInfos.Contains(K))
		{
			return false;
		}
	}
	return true;
}

static EStateKeyDataType ValuePinTypeToDataType(const UEdGraphPin* ValuePin)
{
	if (!ValuePin)
	{
		return EStateKeyDataType::Invalid;
	}

	const bool bArray = ValuePin->PinType.ContainerType == EPinContainerType::Array;
	if (ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_String)
	{
		return bArray ? EStateKeyDataType::StringArray : EStateKeyDataType::String;
	}
	if (ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real && ValuePin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float)
	{
		return bArray ? EStateKeyDataType::NumberArray : EStateKeyDataType::Number;
	}
	if (ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
	{
		return bArray ? EStateKeyDataType::BoolArray : EStateKeyDataType::Bool;
	}
	return EStateKeyDataType::Invalid;
}

static void GatherRequiredKeys(UK2Node* Node, TArray<FString>& OutKeys)
{
	OutKeys.Reset();
	if (UZLK2Node_PerformStateRequest* Multi = Cast<UZLK2Node_PerformStateRequest>(Node))
	{
		for (int32 Slot = 0; Slot < Multi->NumKeySlots; ++Slot)
		{
			const FName PinName(*FString::Printf(TEXT("Key_%d"), Slot));
			if (const UEdGraphPin* KeyPin = Multi->FindPin(PinName, EGPD_Input))
			{
				const FString& V = KeyPin->DefaultValue;
				if (!V.IsEmpty() && V != TEXT("Select Key"))
				{
					OutKeys.AddUnique(V);
				}
			}
		}
		return;
	}

	if (const UEdGraphPin* KeyPin = Node->FindPin(TEXT("Key"), EGPD_Input))
	{
		const FString& V = KeyPin->DefaultValue;
		if (!V.IsEmpty() && V != TEXT("Select Key"))
		{
			OutKeys.Add(V);
		}
	}
}

static void GatherRequiredKeyTypes(UK2Node* Node, TMap<FString, EStateKeyDataType>& OutExpectedTypes)
{
	OutExpectedTypes.Reset();
	if (UZLK2Node_PerformStateRequest* Multi = Cast<UZLK2Node_PerformStateRequest>(Node))
	{
		for (int32 Slot = 0; Slot < Multi->NumKeySlots; ++Slot)
		{
			const UEdGraphPin* KeyPin = Multi->FindPin(*FString::Printf(TEXT("Key_%d"), Slot), EGPD_Input);
			const UEdGraphPin* ValuePin = Multi->FindPin(*FString::Printf(TEXT("Value_%d"), Slot), EGPD_Input);
			if (!KeyPin || !ValuePin)
			{
				continue;
			}

			const FString& KeyName = KeyPin->DefaultValue;
			if (KeyName.IsEmpty() || KeyName == TEXT("Select Key"))
			{
				continue;
			}

			const EStateKeyDataType ExpectedType = ValuePinTypeToDataType(ValuePin);
			if (ExpectedType != EStateKeyDataType::Invalid)
			{
				OutExpectedTypes.Add(KeyName, ExpectedType);
			}
		}
		return;
	}

	const UEdGraphPin* KeyPin = Node ? Node->FindPin(TEXT("Key"), EGPD_Input) : nullptr;
	const UEdGraphPin* ValuePin = Node ? Node->FindPin(TEXT("Value"), EGPD_Input) : nullptr;
	if (!KeyPin || !ValuePin)
	{
		return;
	}

	const FString& KeyName = KeyPin->DefaultValue;
	if (KeyName.IsEmpty() || KeyName == TEXT("Select Key"))
	{
		return;
	}

	const EStateKeyDataType ExpectedType = ValuePinTypeToDataType(ValuePin);
	if (ExpectedType != EStateKeyDataType::Invalid)
	{
		OutExpectedTypes.Add(KeyName, ExpectedType);
	}
}

static int32 CountTypeMatches(const UStateKeyInfoAsset* Schema, const TMap<FString, EStateKeyDataType>& ExpectedKeyTypes, int32& OutCompared)
{
	OutCompared = 0;
	if (!Schema)
	{
		return 0;
	}

	int32 Matches = 0;
	for (const TPair<FString, EStateKeyDataType>& Pair : ExpectedKeyTypes)
	{
		const FStateKeyInfo* Info = Schema->KeyInfos.Find(Pair.Key);
		if (!Info)
		{
			continue;
		}
		++OutCompared;
		if (Info->GetDataTypeEnum() == Pair.Value)
		{
			++Matches;
		}
	}
	return Matches;
}

static UStateKeyInfoAsset* FindBestSchemaForKeys(
	const TArray<FString>& RequiredKeys,
	const FString& HintAssetName,
	const TMap<FString, EStateKeyDataType>& ExpectedKeyTypes)
{
	if (RequiredKeys.Num() == 0)
	{
		return nullptr;
	}

	FAssetRegistryModule& RegModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UStateKeyInfoAsset::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);

	TArray<UStateKeyInfoAsset*> Matches;
	Matches.Reserve(Assets.Num());
	for (const FAssetData& AD : Assets)
	{
		UStateKeyInfoAsset* Schema = Cast<UStateKeyInfoAsset>(AD.GetAsset());
		if (!Schema)
		{
			continue;
		}
		Schema->ConditionalPostLoad();
		if (SchemaHasAllKeys(Schema, RequiredKeys))
		{
			Matches.Add(Schema);
		}
	}

	if (Matches.Num() == 0)
	{
		return nullptr;
	}
	if (Matches.Num() == 1)
	{
		return Matches[0];
	}

	if (ExpectedKeyTypes.Num() > 0)
	{
		int32 BestMatches = -1;
		int32 BestCompared = -1;
		UStateKeyInfoAsset* BestSchema = nullptr;
		for (UStateKeyInfoAsset* Candidate : Matches)
		{
			int32 Compared = 0;
			const int32 MatchCount = CountTypeMatches(Candidate, ExpectedKeyTypes, Compared);
			if (!BestSchema || MatchCount > BestMatches || (MatchCount == BestMatches && Compared > BestCompared))
			{
				BestSchema = Candidate;
				BestMatches = MatchCount;
				BestCompared = Compared;
			}
		}

		if (BestSchema)
		{
			if (BestCompared > 0 && BestMatches < BestCompared)
			{
				UE_LOG(
					LogZLSchemaK2Remap,
					Warning,
					TEXT("Schema remap selected '%s' with partial key type match (%d/%d)."),
					*BestSchema->GetPathName(),
					BestMatches,
					BestCompared);
			}
			return BestSchema;
		}
	}

	if (!HintAssetName.IsEmpty())
	{
		for (UStateKeyInfoAsset* S : Matches)
		{
			if (S && S->GetName() == HintAssetName)
			{
				return S;
			}
		}
		for (UStateKeyInfoAsset* S : Matches)
		{
			if (S && S->GetOutermost() && S->GetOutermost()->GetName() == HintAssetName)
			{
				return S;
			}
		}
	}

	// TArray::Sort dereferences raw pointers before calling the predicate (see TDereferenceWrapper in Sorting.h).
	Matches.Sort([](const UStateKeyInfoAsset& A, const UStateKeyInfoAsset& B)
	{
		return A.GetPathName() < B.GetPathName();
	});
	UE_LOG(
		LogZLSchemaK2Remap,
		Warning,
		TEXT("Multiple UStateKeyInfoAsset candidates contain the same keys; using first stable path: %s"),
		*Matches[0]->GetPathName());
	return Matches[0];
}

static void ApplySchemaToAssetPin(UK2Node* Node, UEdGraphPin* AssetPin, UStateKeyInfoAsset* NewSchema)
{
	if (!Node || !AssetPin || !NewSchema)
	{
		return;
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	FText UseDefaultText;
	if (!K2Schema->IsPinDefaultValid(AssetPin, FString(), NewSchema, UseDefaultText).IsEmpty())
	{
		return;
	}

	Node->Modify();
	AssetPin->Modify();

	// Avoid UEdGraphSchema_K2::TrySetDefaultObject: it always calls PinDefaultValueChanged and, when
	// bMarkAsModified is true, FindBlueprintForNodeChecked — both assert during blueprint import when
	// the graph is still transient (no owning UBlueprint yet).
	AssetPin->DefaultObject = NewSchema;
	// Keep a path fallback so later load/reconstruct passes can still resolve the schema
	// if DefaultObject is temporarily null.
	AssetPin->DefaultValue = NewSchema->GetPathName();
	AssetPin->DefaultTextValue = UseDefaultText;

	if (!TryReconstructRemappedNode(Node))
	{
		TWeakObjectPtr<UK2Node> WeakNode(Node);
		TSharedRef<int32> RemainingAttempts = MakeShared<int32>(120);
		FTSTicker::GetCoreTicker().AddTicker(
			TEXT("ZLSchemaK2RemapReconstruct"),
			0.0f,
			[WeakNode, RemainingAttempts](float)
			{
				UK2Node* N = WeakNode.Get();
				if (!N)
				{
					return false;
				}
				if (TryReconstructRemappedNode(N))
				{
					return false;
				}
				return --(*RemainingAttempts) > 0;
			});
	}

	UE_LOG(
		LogZLSchemaK2Remap,
		Log,
		TEXT("Remapped state schema on node '%s' (%s) to '%s'"),
		*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
		*Node->GetName(),
		*NewSchema->GetPathName());
}
} // namespace ZLSchemaRemap_Private

void ZL_TryRemapStateSchemaOnZLK2Node(UK2Node* Node)
{
#if !WITH_EDITOR
	return;
#else
	using namespace ZLSchemaRemap_Private;

	if (!Node || !GIsEditor || Node->HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	if (!Node->IsA(UZLK2Node_GetRequestedStateValue::StaticClass())
		&& !Node->IsA(UZLK2Node_GetRequestedStateValueSubKeys::StaticClass())
		&& !Node->IsA(UZLK2Node_GetRequestedStateValueObjectString::StaticClass())
		&& !Node->IsA(UZLK2Node_SetCurrentStateValue::StaticClass())
		&& !Node->IsA(UZLK2Node_PerformStateRequest::StaticClass()))
	{
		return;
	}

	UEdGraphPin* AssetPin = Node->FindPin(TEXT("Asset"), EGPD_Input);
	if (!AssetPin)
	{
		return;
	}

	const UStateKeyInfoAsset* Current = TryResolveSchemaFromPin(AssetPin);
	if (IsAssignedSchemaPresentInProject(AssetPin, Current))
	{
		return;
	}

	TArray<FString> RequiredKeys;
	GatherRequiredKeys(Node, RequiredKeys);
	if (RequiredKeys.Num() == 0)
	{
		return;
	}

	const FString HintName = GetHintAssetNameFromPin(AssetPin);
	TMap<FString, EStateKeyDataType> ExpectedKeyTypes;
	GatherRequiredKeyTypes(Node, ExpectedKeyTypes);
	UStateKeyInfoAsset* Replacement = FindBestSchemaForKeys(RequiredKeys, HintName, ExpectedKeyTypes);
	if (!Replacement)
	{
		UE_LOG(
			LogZLSchemaK2Remap,
			Verbose,
			TEXT("No UStateKeyInfoAsset in project contains keys required by node '%s'"),
			*Node->GetName());
		return;
	}

	if (Replacement == Current)
	{
		return;
	}

	ApplySchemaToAssetPin(Node, AssetPin, Replacement);
#endif
}
