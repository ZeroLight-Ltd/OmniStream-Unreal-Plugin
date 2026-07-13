// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IZLCloudPluginModule.h"

#if WITH_EDITOR

#include "IZLOmniStream_SchemaAutoPopulate.h"
#include "ZLStateKeyInfo.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "SlateBasics.h"
#include "SlateExtras.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "PropertyCustomizationHelpers.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "AssetRegistry/AssetRegistryModule.h"

DECLARE_LOG_CATEGORY_EXTERN(LogZLStateEditorV2, Log, All);

class StateKeyInfoV2
{
public:
    FString dataType = "Select Type";
    TSharedPtr<FJsonValue> defaultValue = nullptr;
    bool limitValues = false;
    TArray<TSharedPtr<FJsonValue>> acceptedValues = TArray<TSharedPtr<FJsonValue>>();

    TArray<TSharedPtr<FJsonValue>> defaultValueArray = TArray<TSharedPtr<FJsonValue>>();

    bool ignoredInDataHash = false;
    bool useMinMax = false;
    double minValue = 0.0;
    double maxValue = 0.0;
    bool allowDynamicArraySize = false;
    bool allowNullValue = false;
    bool defaultValueIsNull = false;
    bool displayDescriptionAsOptions = false;

    inline void ResetDefaultValue()
    {
        if (dataType == "String")
            defaultValue = MakeShared<FJsonValueString>("");
        else if (dataType == "Number")
            defaultValue = MakeShared<FJsonValueNumber>(0.0f);
        else if (dataType == "Bool")
            defaultValue = MakeShared<FJsonValueBoolean>(false);
        else if (dataType == "StringArray")
        {
            TArray<TSharedPtr<FJsonValue>> StructJsonArray;
            StructJsonArray.Add(MakeShared<FJsonValueString>(""));
            defaultValue = MakeShared<FJsonValueArray>(StructJsonArray);
        }
        else if (dataType == "NumberArray")
        {
            TArray<TSharedPtr<FJsonValue>> StructJsonArray;
            StructJsonArray.Add(MakeShared<FJsonValueNumber>(0.0f));
            defaultValue = MakeShared<FJsonValueArray>(StructJsonArray);
        }
        else if (dataType == "BoolArray")
        {
            TArray<TSharedPtr<FJsonValue>> StructJsonArray;
            StructJsonArray.Add(MakeShared<FJsonValueBoolean>(false));
            defaultValue = MakeShared<FJsonValueArray>(StructJsonArray);
        }
        else
            defaultValue = MakeShared<FJsonValueString>("");
    }

    inline bool IsArray()
    {
        return dataType == "StringArray" || dataType == "NumberArray" || dataType == "BoolArray";
    }

    inline bool AllowsLimitValues()
    {
        return dataType == "String" || dataType == "Number" || dataType == "StringArray" || dataType == "NumberArray";
    }
};

class FZLStateEditorV2 : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(FZLStateEditorV2) {}
    SLATE_END_ARGS()

    FZLStateEditorV2();
    virtual ~FZLStateEditorV2();

    void Construct(const FArguments& InArgs);
    bool SaveAssetFromMap();
    void LoadFromUAsset();
    void LoadFromJsonSchema(TSharedPtr<FJsonObject> Schema);
    bool LoadSchemaFromFilePath(const FString& FilePath, bool bPersistPath = true);

    /** Runs auto-populate against the given selections and levels, merging contributions
     * from each registered IZLOmniStream_SchemaAutoPopulate. When bAdditive is false,
     * the current schema is cleared before merging the new contributions. */
    void AutoPopulateSchema(
        const TArray<FString>& SelectedOptions,
        const TArray<UWorld*>& Levels,
        bool bAdditive = true,
        bool bDiscardPendingPreview = true,
        const FZLAutoPopulateAdvancedSettingsMap& AdvancedSettingsByOption = FZLAutoPopulateAdvancedSettingsMap());

    /** Returns the auto-populate option strings persisted for the advanced auto-populate window. */
    const TSet<FString>& GetSelectedAutoPopulateOptions() const { return SelectedAutoPopulateOptions; }

    /** Updates and persists the auto-populate option selection for the advanced window. */
    void SetSelectedAutoPopulateOptions(const TArray<FString>& Options);

    /** Weak pointer to the currently constructed editor instance (if any). */
    static TWeakPtr<FZLStateEditorV2> GetLiveInstance() { return s_LiveInstance; }

    /** Opens the python-driven advanced auto-populate window. */
    void OpenAdvancedAutoPopulateWindow();
    void BeginOpenAdvancedAutoPopulateWindow();
    EActiveTimerReturnType HandleDeferredOpenAdvancedAutoPopulate(double InCurrentTime, float InDeltaTime);

    /** Returns the currently loaded schema path (if any). */
    const FString& GetLoadedSchemaPath() const { return lastOpenSchemaAssetPath; }

    /** Builds a non-mutating auto-populate preview and returns JSON-Schema text. */
    bool BuildAutoPopulatePreview(
        const TArray<FString>& SelectedOptions,
        const TArray<UWorld*>& Levels,
        bool bAdditive,
        FString& OutProposedSchemaText,
        const FZLAutoPopulateAdvancedSettingsMap& AdvancedSettingsByOption = FZLAutoPopulateAdvancedSettingsMap());

    /** Applies the last previewed state to the live editor. */
    bool ApplyAutoPopulatePreview();

    /** Clears any pending preview state. */
    void DiscardAutoPopulatePreview();

    /** Returns the current staged preview schema text (if any). */
    const FString& GetPendingAutoPopulatePreviewSchemaText() const { return PendingAutoPopulatePreviewSchemaText; }

    /** True when the editor content differs from the last saved or loaded snapshot. */
    bool HasUnsavedChanges() const;

    /** Saves via the schema save dialog when possible; returns whether the window may close. */
    bool PromptSaveAndClose();

protected:
    TSharedPtr<SMultiLineEditableTextBox> JsonTextBox;
    TArray<TSharedPtr<FString>> TextEditorViewOptions;
    TSharedPtr<FString> SelectedTextEditorViewOption;
    bool bSuppressJsonTextChanged = false;
    TSharedPtr<SVerticalBox> ButtonsContainer;
    TSharedPtr<SListView<TSharedPtr<FString>>> KeyListView;
    TArray<TSharedPtr<FString>> JsonKeys;
    TMap<FString, TPair<bool, bool>> AreaExpansionStates;

    void OnJsonTextChanged(const FText& NewText);
    bool IsFullJsonSchemaAndMetadataView() const;
    FString BuildFullJsonSchemaAndMetadataString() const;
    void RefreshJsonTextEditorDisplay();
    void RemoveInvalidKeyInfoEntries();
    void UpdateJsonData(const FString& JsonString);
    void UpdateJsonStr();
    void GenerateUIFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& Prefix, const bool SetActiveObject = true);
    void RefreshEditorFromState();
    void MarkDocumentClean();
    FString BuildDocumentSnapshot() const;
    bool SaveAssetToPath(const FString& SaveFileName);
    bool SaveSchemaAfterAutoPopulate();
    void SaveLastOpenedSchemaPathToConfig() const;
    void LoadLastOpenedSchemaPathFromConfig();
    void SaveSelectedAutoPopulateOptionsToConfig() const;
    void LoadSelectedAutoPopulateOptionsFromConfig();
    void ApplyDefaultAutoPopulateOptionSelections();
    void BeginRenameKey(const FString& Key);
    void CommitRenameKey(const FString& OriginalKey, const FString& ProposedKey);
    void DuplicateKey(const FString& SourceKey);
    void DeleteKey(const FString& Key);
    FString MakeUniqueDuplicateKey(const FString& SourceKey) const;

    void RefreshAutoPopulateOptions();

    /** Returns true if the key has at least one accepted value (or DIME group code)
     * that resolves to a non-empty description via the loaded DIME model metadata. */
    bool KeyHasDescribedAcceptedValues(const FString& Key) const;

    /** Returns true when the schema editor should show the "Display Description as
     * options" toggle for this key. */
    bool ShouldShowDisplayDescriptionAsOptionsCheckbox(const FString& Key) const;

    TMap<FString, FStateKeyInfo> ConvertToSerializableMap(const TMap<FString, StateKeyInfoV2>& StateKeyInfoMap);
    TMap<FString, StateKeyInfoV2> ConvertToEditorMap(const TMap<FString, FStateKeyInfo>& SavedAsset, TSharedPtr<FJsonObject>& OutJsonObject);
    inline static TArray<TSharedPtr<FString>> s_DataTypes = { MakeShared<FString>("String"), MakeShared<FString>("Number"), MakeShared<FString>("Bool"), MakeShared<FString>("StringArray"), MakeShared<FString>("NumberArray"), MakeShared<FString>("BoolArray") };

    TSharedPtr<FJsonObject> ActiveJsonObject = MakeShared<FJsonObject>();

    inline static TMap<FString, StateKeyInfoV2> keyInfos;
    FString newKeyStr = "";
    FString lastOpenSchemaAssetPath = "";
    FString keyBeingRenamed = "";
    FString pendingRenameText = "";

    TArray<TSharedPtr<FString>> AutoPopulateOptionLabels;
    TSet<FString> SelectedAutoPopulateOptions;
    bool bIsOpeningAdvancedAutoPopulateWindow = false;

    bool bHasPendingAutoPopulatePreview = false;
    TSharedPtr<FJsonObject> PendingAutoPopulatePreviewJsonObject;
    TMap<FString, StateKeyInfoV2> PendingAutoPopulatePreviewKeyInfos;
    TArray<FDIMEModelMetadata> PendingAutoPopulatePreviewDimeModelData;
    FString PendingAutoPopulatePreviewSchemaText;

    FString SavedDocumentSnapshot;

    TArray<FDIMEModelMetadata> DimeModelData;

    static TWeakPtr<FZLStateEditorV2> s_LiveInstance;

    inline static FString s_currentJsonStr = "{\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n}";
};

#endif
