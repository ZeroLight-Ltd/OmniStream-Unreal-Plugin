// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#if WITH_EDITOR
#include "K2Node.h"
#include "KismetCompiler.h"
#include "SGraphPin.h"
#include "SGraphPanel.h"
#include "EdGraph/EdGraphPin.h"
#include "Widgets/Input/SComboBox.h"
#include "EdGraphUtilities.h"
#include "EdGraphSchema_K2.h"
#include "ZeroLightEditorStyle.h"
#endif

#include "ZLStateKeyInfo.h"
#include "ZLCloudPluginVersion.h"

#if !WITH_EDITOR
#include "UObject/Object.h"
class UK2Node : public UObject
{
	GENERATED_BODY()
};
#endif

#include "ZLK2Nodes.generated.h"

class UEdGraphPin;

UCLASS()
class ZLCLOUDPLUGINEDITOR_API UZLK2Node_GetRequestedStateValue : public UK2Node
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* ChangedPin) override;
	virtual void PostLoad() override;

	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;

	virtual FName GetFunctionName() const;
	inline virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override
	{
		return FSlateIcon("ZeroLightEditorStyle", "ZeroLight.Logo");
	}

	inline virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override
	{
		Super::ValidateNodeDuringCompilation(MessageLog);

		const UEdGraphPin* SchemaPin = FindPin(TEXT("Asset"));
		if (SchemaPin && !SchemaPin->DefaultObject)
		{
			MessageLog.Error(TEXT("Schema must be assigned on node @@"), this);
		}

		const UEdGraphPin* KeyNamePin = FindPin(TEXT("Key"));
		if (KeyNamePin && (KeyNamePin->DefaultValue == TEXT("Select Key") || KeyNamePin->DefaultValue.IsEmpty()))
		{
			MessageLog.Error(TEXT("Key on node @@ must be set to a valid key."), this);
		}
	}

	inline void OnAssetLoaded(FSoftObjectPath AssetPath)
	{
		UStateKeyInfoAsset* LoadedAsset = Cast<UStateKeyInfoAsset>(AssetPath.ResolveObject());

		if (!LoadedAsset)
		{
			LoadedAsset = Cast<UStateKeyInfoAsset>(AssetPath.TryLoad());
		}

		if (LoadedAsset)
		{
			LoadedAsset->ConditionalPostLoad();

			SavedAssetObject = LoadedAsset;

			//ReconstructNode();
		}
	}

	UPROPERTY()
	EStateKeyDataType SavedDataType = EStateKeyDataType::Invalid; //To prevent errors on project reload deserialization before schema asset is fully loaded

private:
	inline EStateKeyDataType GetDataType() 
	{
		UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
		UEdGraphPin* KeyPin = FindPin(TEXT("Key"));
		if (AssetPin && KeyPin && AssetPin->DefaultObject)
		{
			const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
			if (!Asset)
				return EStateKeyDataType::Invalid;;

			if (Asset->KeyInfos.Contains(KeyPin->DefaultValue))
			{
				return Asset->KeyInfos[KeyPin->DefaultValue].GetDataTypeEnum();
			}
		}

		return EStateKeyDataType::Invalid;
	}

	FSoftObjectPath AssetPathStr; // Asset is a pointer to your UStateKeyInfoAsset
	UStateKeyInfoAsset* SavedAssetObject = nullptr;
	FString SavedKeyStr;

};

UCLASS()
class ZLCLOUDPLUGINEDITOR_API UZLK2Node_GetRequestedStateValueSubKeys : public UK2Node
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* ChangedPin) override;
	virtual void PostLoad() override;

	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;

	virtual FName GetFunctionName() const;
	inline virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override
	{
		return FSlateIcon("ZeroLightEditorStyle", "ZeroLight.Logo");
	}

	inline virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override
	{
		Super::ValidateNodeDuringCompilation(MessageLog);

		const UEdGraphPin* SchemaPin = FindPin(TEXT("Asset"));
		if (SchemaPin && !SchemaPin->DefaultObject)
		{
			MessageLog.Error(TEXT("Schema must be assigned on node @@"), this);
		}

		const UEdGraphPin* KeyNamePin = FindPin(TEXT("Key"));
		if (KeyNamePin && (KeyNamePin->DefaultValue == TEXT("Select Key") || KeyNamePin->DefaultValue.IsEmpty()))
		{
			MessageLog.Error(TEXT("Key on node @@ must be set to a valid key."), this);
		}
	}

	inline void OnAssetLoaded(FSoftObjectPath AssetPath)
	{
		UStateKeyInfoAsset* LoadedAsset = Cast<UStateKeyInfoAsset>(AssetPath.ResolveObject());

		if (!LoadedAsset)
		{
			LoadedAsset = Cast<UStateKeyInfoAsset>(AssetPath.TryLoad());
		}

		if (LoadedAsset)
		{
			LoadedAsset->ConditionalPostLoad();

			SavedAssetObject = LoadedAsset;

			//ReconstructNode();
		}
	}

private:
	inline EStateKeyDataType GetDataType()
	{
		UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
		UEdGraphPin* KeyPin = FindPin(TEXT("Key"));
		if (AssetPin && KeyPin && AssetPin->DefaultObject)
		{
			const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
			if (!Asset)
				return EStateKeyDataType::Invalid;;

			if (Asset->KeyInfos.Contains(KeyPin->DefaultValue))
			{
				return Asset->KeyInfos[KeyPin->DefaultValue].GetDataTypeEnum();
			}
		}

		return EStateKeyDataType::Invalid;
	}

	FSoftObjectPath AssetPathStr; // Asset is a pointer to your UStateKeyInfoAsset
	UStateKeyInfoAsset* SavedAssetObject = nullptr;
	FString SavedKeyStr;

};

UCLASS()
class ZLCLOUDPLUGINEDITOR_API UZLK2Node_GetRequestedStateValueObjectString : public UK2Node
{
	GENERATED_BODY()

public:
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* ChangedPin) override;
	virtual void PostLoad() override;

	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;

	virtual FName GetFunctionName() const;
	inline virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override
	{
		return FSlateIcon("ZeroLightEditorStyle", "ZeroLight.Logo");
	}

	inline virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override
	{
		Super::ValidateNodeDuringCompilation(MessageLog);

		const UEdGraphPin* SchemaPin = FindPin(TEXT("Asset"));
		if (SchemaPin && !SchemaPin->DefaultObject)
		{
			MessageLog.Error(TEXT("Schema must be assigned on node @@"), this);
		}

		const UEdGraphPin* KeyNamePin = FindPin(TEXT("Key"));
		if (KeyNamePin && (KeyNamePin->DefaultValue == TEXT("Select Key") || KeyNamePin->DefaultValue.IsEmpty()))
		{
			MessageLog.Error(TEXT("Key on node @@ must be set to a valid key."), this);
		}
	}

	inline void OnAssetLoaded(FSoftObjectPath AssetPath)
	{
		UStateKeyInfoAsset* LoadedAsset = Cast<UStateKeyInfoAsset>(AssetPath.ResolveObject());

		if (!LoadedAsset)
		{
			LoadedAsset = Cast<UStateKeyInfoAsset>(AssetPath.TryLoad());
		}

		if (LoadedAsset)
		{
			LoadedAsset->ConditionalPostLoad();

			SavedAssetObject = LoadedAsset;

			//ReconstructNode();
		}
	}

private:
	inline EStateKeyDataType GetDataType()
	{
		UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
		UEdGraphPin* KeyPin = FindPin(TEXT("Key"));
		if (AssetPin && KeyPin && AssetPin->DefaultObject)
		{
			const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
			if (!Asset)
				return EStateKeyDataType::Invalid;;

			if (Asset->KeyInfos.Contains(KeyPin->DefaultValue))
			{
				return Asset->KeyInfos[KeyPin->DefaultValue].GetDataTypeEnum();
			}
		}

		return EStateKeyDataType::Invalid;
	}

	FSoftObjectPath AssetPathStr; // Asset is a pointer to your UStateKeyInfoAsset
	UStateKeyInfoAsset* SavedAssetObject = nullptr;
	FString SavedKeyStr;

};

UCLASS()
class ZLCLOUDPLUGINEDITOR_API UZLK2Node_SetCurrentStateValue : public UK2Node
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* ChangedPin) override;
	virtual void PostLoad() override;

	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;

	virtual FName GetFunctionName() const;
	inline virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override
	{
		return FSlateIcon("ZeroLightEditorStyle", "ZeroLight.Logo");
	}

	inline virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override
	{
		Super::ValidateNodeDuringCompilation(MessageLog);

		const UEdGraphPin* SchemaPin = FindPin(TEXT("Asset"));
		if (SchemaPin && !SchemaPin->DefaultObject)
		{
			MessageLog.Error(TEXT("Schema must be assigned on node @@"), this);
		}

		const UEdGraphPin* KeyNamePin = FindPin(TEXT("Key"));
		if (KeyNamePin && (KeyNamePin->DefaultValue == TEXT("Select Key") || KeyNamePin->DefaultValue.IsEmpty()))
		{
			MessageLog.Error(TEXT("Key on node @@ must be set to a valid key."), this);
		}
	}

	inline void OnAssetLoaded(FSoftObjectPath AssetPath)
	{
		UStateKeyInfoAsset* LoadedAsset = Cast<UStateKeyInfoAsset>(AssetPath.ResolveObject());

		if (!LoadedAsset)
		{
			LoadedAsset = Cast<UStateKeyInfoAsset>(AssetPath.TryLoad());
		}

		if (LoadedAsset)
		{
			LoadedAsset->ConditionalPostLoad();

			SavedAssetObject = LoadedAsset;

			//ReconstructNode();
		}
	}

	UPROPERTY()
	EStateKeyDataType SavedDataType = EStateKeyDataType::Invalid; //To prevent errors on project reload deserialization before schema asset is fully loaded

private:
	inline EStateKeyDataType GetDataType()
	{
		UEdGraphPin* AssetPin = FindPin(TEXT("Asset"));
		UEdGraphPin* KeyPin = FindPin(TEXT("Key"));
		if (AssetPin && KeyPin && AssetPin->DefaultObject)
		{
			const UStateKeyInfoAsset* Asset = Cast<UStateKeyInfoAsset>(AssetPin->DefaultObject);
			if (!Asset)
				return EStateKeyDataType::Invalid;;

			if (Asset->KeyInfos.Contains(KeyPin->DefaultValue))
			{
				return Asset->KeyInfos[KeyPin->DefaultValue].GetDataTypeEnum();
			}
		}

		return EStateKeyDataType::Invalid;
	}

	FSoftObjectPath AssetPathStr; // Asset is a pointer to your UStateKeyInfoAsset
	UStateKeyInfoAsset* SavedAssetObject = nullptr;
	FString SavedKeyStr;

};

UCLASS()
class ZLCLOUDPLUGINEDITOR_API UZLK2Node_PerformStateRequest : public UK2Node
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* ChangedPin) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;

	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;

	inline virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override
	{
		return FSlateIcon("ZeroLightEditorStyle", "ZeroLight.Logo");
	}

	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;

	/** Adds another Key_N / Value_N pair (also: right-click node → State Request). */
	void AddKeyToRequest();

	/** Removes the highest-index slot (also: right-click node → State Request). */
	void RemoveLastKeyFromRequest();

	inline void OnAssetLoaded(FSoftObjectPath Path)
	{
		UStateKeyInfoAsset* LoadedAsset = Cast<UStateKeyInfoAsset>(Path.ResolveObject());
		if (!LoadedAsset)
		{
			LoadedAsset = Cast<UStateKeyInfoAsset>(Path.TryLoad());
		}
		if (LoadedAsset)
		{
			LoadedAsset->ConditionalPostLoad();
			SavedAssetObject = LoadedAsset;
		}
	}

	UPROPERTY(EditAnywhere, Category = "State Request", meta = (ClampMin = "1"))
	int32 NumKeySlots = 1;

private:
	bool CanRemoveLastKeySlot() const { return NumKeySlots > 1; }

	void ScheduleVisualRefreshAfterSlotChange();

	static bool ParseSlotIndexFromPinName(const FString& PinNameStr, const TCHAR* Prefix, int32& OutSlot);
	static EStateKeyDataType PinTypeToValueDataType(const UEdGraphPin* ValuePin);
	EStateKeyDataType GetDataTypeForSlot(int32 SlotIndex) const;

	FSoftObjectPath AssetPathStr;
	UStateKeyInfoAsset* SavedAssetObject = nullptr;

	UPROPERTY()
	TArray<FString> SavedKeyPerSlot;

	UPROPERTY()
	TArray<EStateKeyDataType> SavedDataTypePerSlot;

	UPROPERTY()
	TArray<FString> SavedValuePerSlot;
};

class SGraphPin_KeySelector : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPin_KeySelector) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	TArray<TSharedPtr<FString>> KeyOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ComboBox;

	TArray<TSharedPtr<FString>> GatherKeyOptions() const;
	void OnSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
};

/** Schema value pin with combo from AcceptedStringValues / AcceptedNumberValues when bLimitValues is set (Perform State Request node). */
class SGraphPin_SchemaConstrainedValue : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPin_SchemaConstrainedValue) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	TArray<TSharedPtr<FString>> ValueOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ValueCombo;

	TArray<TSharedPtr<FString>> GatherValueOptions() const;
	void OnSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
};

class FSchemaKeyPinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<SGraphPin> CreatePin(class UEdGraphPin* InPin) const override;
};



