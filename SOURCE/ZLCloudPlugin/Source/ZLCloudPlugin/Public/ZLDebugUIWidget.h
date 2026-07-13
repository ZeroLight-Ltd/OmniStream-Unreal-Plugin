#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Engine/DeveloperSettings.h"

struct FKeyEvent;
#include "Blueprint/UserWidget.h"
#include "Layout/WidgetPath.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Dom/JsonObject.h"
#include "ZLStateKeyInfo.h"
#include "ZLCloudPluginVersion.h"

#if UNREAL_5_2_OR_NEWER
#include "Engine/TimerHandle.h"
#endif

#include "ZLDebugUIWidget.generated.h"

namespace ZLDebugUIJson_Private
{
template<typename T>
static inline void WriteLeafValue(FJsonObject* Current, const FString& FinalKey, const T& Value)
{
	if constexpr (std::is_same_v<T, FString>)
	{
		Current->SetStringField(FinalKey, Value);
	}
	else if constexpr (std::is_same_v<T, double>)
	{
		Current->SetNumberField(FinalKey, Value);
	}
	else if constexpr (std::is_same_v<T, bool>)
	{
		Current->SetBoolField(FinalKey, Value);
	}
	else if constexpr (std::is_same_v<T, TArray<FString>>)
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		for (const FString& Str : Value)
		{
			JsonArray.Add(MakeShared<FJsonValueString>(Str));
		}
		Current->SetArrayField(FinalKey, JsonArray);
	}
	else if constexpr (std::is_same_v<T, TArray<double>>)
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		for (const double& Num : Value)
		{
			JsonArray.Add(MakeShared<FJsonValueNumber>(Num));
		}
		Current->SetArrayField(FinalKey, JsonArray);
	}
	else if constexpr (std::is_same_v<T, TArray<bool>>)
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		for (bool b : Value)
		{
			JsonArray.Add(MakeShared<FJsonValueBoolean>(b));
		}
		Current->SetArrayField(FinalKey, JsonArray);
	}
	else
	{
		static_assert(sizeof(T) == 0, "Unsupported JSON leaf type.");
	}
}
}

template<typename T>
static inline FString CreateStateChangeJsonStr(const FString& Key, const T& Value)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<FString> Keys;
	Key.ParseIntoArray(Keys, TEXT("."), true);
	TSharedPtr<FJsonObject> Current = Root;
	for (int32 i = 0; i < Keys.Num() - 1; ++i)
	{
		TSharedPtr<FJsonObject> Child = MakeShared<FJsonObject>();
		Current->SetObjectField(Keys[i], Child);
		Current = Child;
	}
	ZLDebugUIJson_Private::WriteLeafValue(Current.Get(), Keys.Last(), Value);
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

template<typename T>
static inline void UpdateJsonObjectKey(const FString& Key, const T& Value, TSharedPtr<FJsonObject>& JsonObject)
{
	TArray<FString> Keys;
	Key.ParseIntoArray(Keys, TEXT("."), true);
	TSharedPtr<FJsonObject> Current = JsonObject;
	for (int32 i = 0; i < Keys.Num() - 1; ++i)
	{
		TSharedPtr<FJsonObject> Child;
		if (Current->HasField(Keys[i]))
		{
			Child = Current->GetObjectField(Keys[i]);
		}
		else
		{
			Child = MakeShared<FJsonObject>();
			Current->SetObjectField(Keys[i], Child);
		}
		Current = Child;
	}
	ZLDebugUIJson_Private::WriteLeafValue(Current.Get(), Keys.Last(), Value);
}

static inline bool RemoveJsonObjectKey(const FString& Key, TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return false;
	}

	TArray<FString> Keys;
	Key.ParseIntoArray(Keys, TEXT("."), true);
	if (Keys.Num() == 0)
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Parents;
	TArray<FString> ParentKeys;
	TSharedPtr<FJsonObject> Current = JsonObject;
	for (int32 Idx = 0; Idx < Keys.Num() - 1; ++Idx)
	{
		const FString& Segment = Keys[Idx];
		const TSharedPtr<FJsonValue> Existing = Current->TryGetField(Segment);
		if (!Existing.IsValid() || Existing->Type != EJson::Object)
		{
			return false;
		}
		Parents.Add(Current);
		ParentKeys.Add(Segment);
		Current = Existing->AsObject();
		if (!Current.IsValid())
		{
			return false;
		}
	}

	const FString& Leaf = Keys.Last();
	if (!Current->HasField(Leaf))
	{
		return false;
	}
	Current->RemoveField(Leaf);

	for (int32 ParentIdx = Parents.Num() - 1; ParentIdx >= 0; --ParentIdx)
	{
		if (Current.IsValid() && Current->Values.Num() == 0)
		{
			Parents[ParentIdx]->RemoveField(ParentKeys[ParentIdx]);
			Current = Parents[ParentIdx];
		}
		else
		{
			break;
		}
	}

	return true;
}

UCLASS()
class UStateKeyInputComboBox : public UComboBoxString
{
	GENERATED_BODY()
public:
	FString KeyName;
	EStateKeyDataType DataType;
	FStateKeyInfo StateKeyInfo;
	UZLDebugUIWidget* ParentDebugUI;
	bool InstantBroadcastChange = true;
	// Maps a true value to its resolved DIME description (used for tooltips / option labels).
	TMap<FString, FString> OptionDescriptions;
	// When true the dropdown displays the description for each value as the option label,
	// while still broadcasting the true value. Mapping tables below bridge the two.
	bool bDisplayDescriptionAsOptions = false;
	TMap<FString, FString> DisplayLabelToValue;
	TMap<FString, FString> ValueToDisplayLabel;
	UFUNCTION()
	void OnComboBoxChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void TriggerResend();
	UFUNCTION()
	UWidget* GenerateComboOptionWidget(FString Item);
	void RefreshSelectedOptionTooltip();

	/** Adds an option for the given true value, displaying its description as the label
	 * when bDisplayDescriptionAsOptions is set. Records the label<->value mapping. */
	void AddValueOptionWithLabel(const FString& TrueValue);
	/** Selects the option corresponding to the given true value. */
	void SelectByTrueValue(const FString& TrueValue);
	/** Returns the true value for the currently selected option (maps label back to value). */
	FString GetSelectedTrueValue() const;
	/** Returns the display label that represents the given true value. */
	FString GetDisplayLabelForValue(const FString& TrueValue) const;
	/** Returns the true value represented by the given display label. */
	FString GetTrueValueForDisplayLabel(const FString& InDisplayLabel) const;
};

UCLASS()
class UStateKeyInputCheckBox : public UCheckBox
{
	GENERATED_BODY()
public:
	FString KeyName;
	UZLDebugUIWidget* ParentDebugUI;
	bool InstantBroadcastChange = true;
	UFUNCTION()
	void OnCheckBoxChanged(bool bIsChecked);
	UFUNCTION()
	void TriggerResend();
};

UCLASS()
class UStateKeyInputTextBox : public UEditableTextBox
{
	GENERATED_BODY()
public:
	FString KeyName;
	EStateKeyDataType DataType;
	FStateKeyInfo StateKeyInfo;
	UZLDebugUIWidget* ParentDebugUI;
	bool InstantBroadcastChange = true;
	UFUNCTION()
	virtual void OnTextValueCommitted(const FText& ComittedText, ETextCommit::Type CommitMethod);
	UFUNCTION()
	virtual void TriggerResend();
};

USTRUCT()
struct FAcceptedValueSuggestion
{
	GENERATED_BODY()
	UPROPERTY()
	FString Value;
	UPROPERTY()
	FString Description;
	UPROPERTY()
	FString DisplayLabel;
};

UCLASS()
class UStateKeyInputAcceptedValuesTextBox : public UStateKeyInputTextBox
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<FString> AcceptedValues;
	UPROPERTY()
	TMap<FString, FString> OptionDescriptions;
	UPROPERTY()
	bool bDisplayDescriptionAsOptions = false;
	UFUNCTION()
	void HandleAcceptedValuesTextChanged(const FText& InText);
	virtual void OnTextValueCommitted(const FText& ComittedText, ETextCommit::Type CommitMethod) override;

	void BuildDisplayLabels();
	FString GetDisplayLabelForValue(const FString& TrueValue) const;
	FString GetTrueValueForDisplayLabel(const FString& InDisplayLabel) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	void BuildLabelForValue(const FString& TrueValue);
	void HandleInputFocused();
	void RefreshSuggestions(bool bShowAllOptions = false);
	TSharedRef<class ITableRow> GenerateSuggestionRow(TSharedPtr<FAcceptedValueSuggestion> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	void HandleSuggestionSelected(TSharedPtr<FAcceptedValueSuggestion> Item, ESelectInfo::Type SelectInfo);

	TArray<TSharedPtr<FAcceptedValueSuggestion>> SuggestionItems;
	TSharedPtr<class SMenuAnchor> SuggestionAnchor;
	TSharedPtr<class SListView<TSharedPtr<FAcceptedValueSuggestion>>> SuggestionList;
	bool bSuppressTextChangedHandler = false;

	TMap<FString, FString> DisplayLabelToValue;
	TMap<FString, FString> ValueToDisplayLabel;
};

/**
 * Thin wrapper around UStateKeyInputCheckBox used only by model-configuration
 * BoolArray rows. Broadcasts the full BoolArray under ParentKey instead of the
 * per-index suffixed key so the runtime can reconcile it with the comma
 * delimited `configuration` string.
 */
UCLASS()
class UStateKeyInputConfigArrayCheckBox : public UStateKeyInputCheckBox
{
	GENERATED_BODY()
public:
	FString ParentKey;
	int32 Index = 0;
	int32 ExpectedArrayLength = 0;
	TArray<bool> DefaultArrayTemplate;
	FString TooltipDescription;
	UFUNCTION()
	void OnConfigArrayCheckBoxChanged(bool bIsChecked);
	UFUNCTION()
	void TriggerResendConfigArray();
};

struct FConfigurationAutofillSuggestion
{
	FString Code;
	FString Model;
	FString Group;
	FString Description;
};

/**
 * Drop-in replacement for UStateKeyInputTextBox used only for the `configuration`
 * field in the Debug UI when the ZLVE plugin is present. Adds an autofill popup
 * with PR-code suggestions sourced from all model/group schema branches.
 */
UCLASS()
class UStateKeyInputConfigurationTextBox : public UStateKeyInputTextBox
{
	GENERATED_BODY()
public:
	UFUNCTION()
	void HandleAutofillTextChanged(const FText& InText);
	virtual void OnTextValueCommitted(const FText& ComittedText, ETextCommit::Type CommitMethod) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	void RefreshSuggestions();
	void GetTokenContext(const FString& CurrentText, FString& OutBeforeToken, FString& OutToken) const;
	TSharedRef<class ITableRow> GenerateSuggestionRow(TSharedPtr<FConfigurationAutofillSuggestion> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	void HandleSuggestionSelected(TSharedPtr<FConfigurationAutofillSuggestion> Item, ESelectInfo::Type SelectInfo);

	TArray<TSharedPtr<FConfigurationAutofillSuggestion>> SuggestionItems;
	TSharedPtr<class SMenuAnchor> SuggestionAnchor;
	TSharedPtr<class SListView<TSharedPtr<FConfigurationAutofillSuggestion>>> SuggestionList;
	bool bSuppressTextChangedHandler = false;
};

class UFoldoutHelper;

UENUM()
enum class EDebugSchemaNavEntryKind : uint8
{
	Foldout,
	Value,
};

USTRUCT()
struct FDebugSchemaNavEntry
{
	GENERATED_BODY()
	UPROPERTY()
	EDebugSchemaNavEntryKind Kind = EDebugSchemaNavEntryKind::Value;
	UPROPERTY()
	TObjectPtr<UFoldoutHelper> FoldoutHelper = nullptr;
	UPROPERTY()
	TObjectPtr<UWidget> ValueWidget = nullptr;
	UPROPERTY()
	TObjectPtr<UBorder> RowBorder = nullptr;
};

UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "ZL Debug UI"))
class ZLCLOUDPLUGIN_API UZLDebugUIProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(config, EditAnywhere, Category = "Keyboard")
	bool enableDebugUIKeyboardControl = false;
	UPROPERTY(config, EditAnywhere, Category = "Editor Tab", meta = (ClampMin = "0.1", ClampMax = "3.0", UIMin = "0.1", UIMax = "3.0"))
	float debugUITabScale = 0.75f;

	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif
};

UCLASS()
class ZLCLOUDPLUGIN_API UZLDebugUIWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	inline void SetTargetSchema(UStateKeyInfoAsset* Schema)
	{
		TargetSchema = Schema;
		RebuildDebugUI();
	}
	inline void TriggerRefreshUI() { RebuildDebugUI(); }
	FORCEINLINE bool IsSchemaValueRebuildInProgress() const { return bSuppressSchemaValueChangeBroadcast; }
	/** True when the debug UI root is shown (not Hidden/Collapsed). Used to skip rebuild work and preset broadcasts while hidden. */
	bool IsDebugUIPresented() const;
	inline void StagePendingNullStateKey(const FString& Key) { PendingNullStateKeys.Add(Key); }
	inline void ClearPendingNullStateKey(const FString& Key) { PendingNullStateKeys.Remove(Key); }
	inline const TSet<FString>& GetPendingNullStateKeys() const { return PendingNullStateKeys; }
	inline void ClearPendingNullStateKeys() { PendingNullStateKeys.Empty(); }
	bool TryHandleSchemaNavKeys(const FKeyEvent& InKeyEvent);
	UFUNCTION(BlueprintCallable, Category = "ZL|DebugUI")
	void SetEnableDebugUIKeyboardControl(bool bEnable);
	UFUNCTION(BlueprintPure, Category = "ZL|DebugUI")
	bool IsDebugUIKeyboardControlEnabled() const { return bEnableDebugUIKeyboardControl; }

	UPROPERTY(meta = (BindWidget))
	UCheckBox* InstantChangeToggle;
	UPROPERTY(meta = (BindWidget))
	UButton* SubmitStateBtn;
	TSharedPtr<FJsonObject> ModifiedStateObject = MakeShared<FJsonObject>();
	TSet<FString> ExpandedFoldouts;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent) override;

	UFUNCTION()
	void OnSubmitStateInstantBoxChanged(bool bIsChecked);
	UFUNCTION()
	void OnSubmitState();
	UFUNCTION()
	void OnToggleAllowResendCurrent();
	UFUNCTION()
	void OnToggleAddNewPreset();
	UFUNCTION()
	void OnPresetDeleteClicked();
	UFUNCTION()
	void OnRemoveArrayEntry(UWidget* EntryToRemove);

	void RebuildDebugUI();
	void RebuildDebugUIWithNesting();
	FString GetPresetsFilePathForSchema() const;
	bool ShouldPersistSchemaPresetsToDisk() const;
	void EnsurePresetsDirectoryExists() const;
	void SyncPresetsCacheFromDisk();
	void SavePresetsCacheToDisk();
	void PopulatePresetOptionsDropdown(const FString& SelectName = FString());
	void BuildPresetPayloadFromCheckedKeys(TSharedPtr<FJsonObject>& OutPayload);
	void BuildDefaultSchemaValuesPayload(TSharedPtr<FJsonObject>& OutPayload);
	void BroadcastPresetJsonForName(const FString& PresetName);
	void UpdatePresetSaveButtonEnabledState();
	void UpdatePresetNameFieldPollTimer();
	void PollPresetNameFieldForSaveButton();
	void UpdatePresetNewUIPanelVisibility();
	FString GetEnteredPresetName() const;
	void ResetPresetNewNameFieldToDefault();
	void ResetSchemaNavOrder();
	void CaptureSchemaNavSelectionToken();
	void RestoreSchemaNavSelectionAfterRebuild();
	void RegisterSchemaNavFoldout(UBorder* RowBorder, UFoldoutHelper* FoldoutHelper);
	void RegisterSchemaNavValue(UBorder* RowBorder, UWidget* ValueWidget);
	void SyncSchemaNavSelectionFromUMGWidget(UWidget* LeafUMG);
	void SchemaNavSetSelectedIndexIfChanged(int32 NewIdx);
	void ApplySchemaNavSelectionVisual();
	void SchemaNavMoveSelectionVertical(int32 Delta);
	void HandleSchemaNavLeftRight(bool bRight);
	void FocusWidgetForSchemaNavIndex(int32 Idx);
	bool IsSchemaNavEntrySelectable(int32 Idx) const;
	int32 FindFirstSelectableSchemaNavIndex() const;
	bool SchemaNavEntryMatchesToken(int32 Idx, const FString& Token) const;
	static FString GetKeyTokenFromSchemaValueWidget(const UWidget* Widget);
	static void ApplySchemaRowBorderFocusState(UBorder* Border, bool bFocused);
	static void ApplyFoldoutExpandCollapse(UFoldoutHelper* FoldoutHelper, bool bExpand);
	bool IsWidgetUnderSchemaOptionsBox(const UWidget* Widget) const;
	static void CycleSchemaCombo(UStateKeyInputComboBox* Combo, bool bForward);
	static void ToggleSchemaCheckBox(UStateKeyInputCheckBox* CheckBox);
	static bool IsNavWidgetHierarchyVisible(const UWidget* Widget);
	static UWidget* GetFocusedUMGWidgetFromSlateForUser(uint32 UserIndex);
	static UWidget* GetUMGWidgetFromSlateWidget(const SWidget* StartWidget);
	static UEditableTextBox* FindFirstEditableTextBoxRecursive(UWidget* Root);
	static FString ExtractEditableStringFromWidgetTree(UWidget* Root, bool& bOutFoundField);
	static void ApplyPresetNameDefaultToWidgetTree(UWidget* Root, const FText& DefaultText);
	void LoadDebugUIUserSettings();
	void SaveDebugUIUserSettings() const;

	UFUNCTION()
	void OnPresetDropdownSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void OnSaveNewPresetClicked();
	UFUNCTION()
	void OnPresetIncludeCheckboxStateChanged(bool bIsChecked);

	TSharedPtr<FJsonObject> CachedPresetsRoot = MakeShared<FJsonObject>();
	TMap<FString, UCheckBox*> PresetIncludeBySchemaKey;
	TMap<FString, TArray<UWidget*>> PresetValueWidgetsBySchemaKey;
	TSet<FString> PendingNullStateKeys;
	bool bPresetComboProgrammaticSelection = false;
	bool bPresetPickKeysMode = false;
	bool bNeedsInitialDefaultPresetApply = true;
#if UNREAL_5_2_OR_NEWER
	FTimerHandle PresetNameFieldPollTimerHandle;
#endif
	FString PresetNameFieldLastPolledText;
	bool bPresetsStagedHydratedOnce = false;
	TWeakObjectPtr<UStateKeyInfoAsset> PresetsHydrationSchema;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> FoldoutHelpers;
	UPROPERTY()
	TArray<TObjectPtr<UObject>> ArrayResizeHelpers;
	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UTextBlock> SchemaTitle;
	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UButton> ZLLogoButton;
	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UButton> PresetAddNewButton;
	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UImage> PresetAddNewImage;
	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UButton> PresetDeleteButton;
	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UImage> PresetDeleteImage;
	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UBorder> PresetNewNameTextArea;
	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UEditableTextBox> PresetNewNameEntry;
	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UComboBoxString> PresetOptionsDropdown;
	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UButton> PresetSaveNewButton;
	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UVerticalBox> SchemaOptionsVBox;
	UPROPERTY()
	TArray<FDebugSchemaNavEntry> SchemaNavEntries;
	UPROPERTY()
	UStateKeyInfoAsset* TargetSchema = nullptr;

	bool instantProcess = true;
	bool allowResendCurrentValues = false;
	bool addNewPresetUIVisible = false;
	bool bSuppressSchemaValueChangeBroadcast = false;
	int32 SchemaNavSelectedIndex = 0;
	FString SchemaNavPersistSelectionToken;
	bool bRegisteredForSchemaNavGlobalInput = false;
	bool bEnableDebugUIKeyboardControl = false;
};

UCLASS()
class UFoldoutHelper : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TObjectPtr<UVerticalBox> SectionContent;
	UPROPERTY()
	TObjectPtr<UTextBlock> ArrowText;
	UPROPERTY()
	TObjectPtr<UZLDebugUIWidget> ParentWidget;
	FString FoldoutPath;
	UFUNCTION()
	void ToggleVisibility();
};

UCLASS()
class UStringArrayResizeHelper : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TObjectPtr<UZLDebugUIWidget> ParentWidget;
	FString KeyName;
	FString FillValue;
	int32 Delta = 0;
	bool bInstantBroadcast = false;
	UFUNCTION()
	void ApplyResize();
};
