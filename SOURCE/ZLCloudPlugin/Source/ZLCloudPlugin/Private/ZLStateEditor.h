// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IZLCloudPluginModule.h"

#if WITH_EDITOR

#include "ZLStateKeyInfo.h"
#include "Containers/Map.h"
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
#include "PropertyCustomizationHelpers.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "AssetRegistry/AssetRegistryModule.h"

DECLARE_LOG_CATEGORY_EXTERN(LogZLStateEditor, Log, All);

class StateKeyInfo
{
public:
    FString dataType = "Select Type";
    TSharedPtr<FJsonValue> defaultValue = nullptr;
    bool limitValues = false;
    TArray<TSharedPtr<FJsonValue>> acceptedValues = TArray<TSharedPtr<FJsonValue>>();

    TArray<TSharedPtr<FJsonValue>> defaultValueArray = TArray<TSharedPtr<FJsonValue>>();

    inline void ResetDefaultValue()
    {
        if(dataType == "String")
            defaultValue = MakeShared<FJsonValueString>("");
        else if(dataType == "Number")
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
            defaultValue = defaultValue = MakeShared<FJsonValueString>("");
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

class FZLStateEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(FZLStateEditor) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SaveAssetFromMap();
    void LoadFromUAsset();

protected:
	TSharedPtr<SMultiLineEditableTextBox> JsonTextBox;
    TSharedPtr<SVerticalBox> ButtonsContainer;
    TSharedPtr<SListView<TSharedPtr<FString>>> KeyListView;
    TArray<TSharedPtr<FString>> JsonKeys;
    TMap<FString, TPair<bool,bool>> AreaExpansionStates;

    void OnJsonTextChanged(const FText& NewText);
    void RemoveInvalidKeyInfoEntries();
    void UpdateJsonData(const FString& JsonString);
    void UpdateJsonStr();
    void GenerateUIFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& Prefix, const bool SetActiveObject = true);
    TMap<FString, FStateKeyInfo> ConvertToSerializableMap(const TMap<FString, StateKeyInfo>& StateKeyInfoMap);
    TMap<FString, StateKeyInfo> ConvertToEditorMap(const TMap<FString, FStateKeyInfo>& SavedAsset, TSharedPtr<FJsonObject>& OutJsonObject);
    inline static TArray<TSharedPtr<FString>> s_DataTypes = { MakeShared<FString>("String"), MakeShared<FString>("Number"), MakeShared<FString>("Bool"), MakeShared<FString>("StringArray"), MakeShared<FString>("NumberArray"), MakeShared<FString>("BoolArray") };

    TSharedPtr<FJsonObject> ActiveJsonObject = MakeShared<FJsonObject>();

    TMap<FString, StateKeyInfo> keyInfos;
    FString newKeyStr = "";
    FString lastOpenSchemaAssetPath = "";

    inline static FString s_currentJsonStr = "{\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n}";
};

#endif
