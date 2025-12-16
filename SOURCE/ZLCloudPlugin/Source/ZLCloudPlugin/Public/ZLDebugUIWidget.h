#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetTree.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateColorBrush.h"
#include "ZLStateKeyInfo.h"
#include "ZLDebugUIWidget.generated.h"

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

	const FString& FinalKey = Keys.Last();

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
			JsonArray.Add(MakeShared<FJsonValueString>(Str));

		Current->SetArrayField(FinalKey, JsonArray);
	}
	else if constexpr (std::is_same_v<T, TArray<double>>)
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		for (const double& Num : Value)
			JsonArray.Add(MakeShared<FJsonValueNumber>(Num));

		Current->SetArrayField(FinalKey, JsonArray);
	}
	else if constexpr (std::is_same_v<T, TArray<bool>>)
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		for (bool b : Value)
			JsonArray.Add(MakeShared<FJsonValueBoolean>(b));

		Current->SetArrayField(FinalKey, JsonArray);
	}
	else
	{
		static_assert(sizeof(T) == 0, "Unsupported type in CreateStateChangeJsonStr.");
	}

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

template <typename T>
static inline void UpdateJsonObjectKey(const FString& Key, const T& Value, TSharedPtr<FJsonObject>& JsonObject)
{
	TArray<FString> Keys;
	Key.ParseIntoArray(Keys, TEXT("."), true);

	TSharedPtr<FJsonObject> Current = JsonObject;
	for (int32 i = 0; i < Keys.Num() - 1; ++i)
	{
		TSharedPtr<FJsonObject> Child;
		if (Current->HasField(Keys[i]))
			Child = Current->GetObjectField(Keys[i]);
		else
		{
			Child = MakeShared<FJsonObject>();
			Current->SetObjectField(Keys[i], Child);
		}
		Current = Child;
	}

	const FString& FinalKey = Keys.Last();

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
			JsonArray.Add(MakeShared<FJsonValueString>(Str));

		Current->SetArrayField(FinalKey, JsonArray);
	}
	else if constexpr (std::is_same_v<T, TArray<double>>)
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		for (const double& Num : Value)
			JsonArray.Add(MakeShared<FJsonValueNumber>(Num));

		Current->SetArrayField(FinalKey, JsonArray);
	}
	else if constexpr (std::is_same_v<T, TArray<bool>>)
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		for (bool b : Value)
			JsonArray.Add(MakeShared<FJsonValueBoolean>(b));

		Current->SetArrayField(FinalKey, JsonArray);
	}
	else
	{
		static_assert(sizeof(T) == 0, "Unsupported type in UpdateJsonObjectKey.");
	}
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

	UFUNCTION()
	void OnComboBoxChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
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
};

UCLASS()
class UStateKeyInputTextBox : public UEditableTextBox
{
	GENERATED_BODY()
public:
	FString KeyName;
	EStateKeyDataType DataType;

	UZLDebugUIWidget* ParentDebugUI;
	bool InstantBroadcastChange = true;

	UFUNCTION()
	void OnTextValueCommitted(const FText& ComittedText, ETextCommit::Type CommitMethod);
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

	inline void TriggerRefreshUI()
	{
		RebuildDebugUI();
	}

	UPROPERTY(meta = (BindWidget))
	UCheckBox* InstantChangeToggle;

	UPROPERTY(meta = (BindWidget))
	UButton* SubmitStateBtn;

	TSharedPtr<FJsonObject> ModifiedStateObject = MakeShared<FJsonObject>();

	TSet<FString> ExpandedFoldouts;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void OnSubmitStateInstantBoxChanged(bool bIsChecked);

	UFUNCTION()
	void OnSubmitState();

	UFUNCTION()
	void OnRemoveArrayEntry(UWidget* EntryToRemove);

	void RebuildDebugUI();
	void RebuildDebugUIWithNesting();

	UPROPERTY()
	TArray<TObjectPtr<UObject>> FoldoutHelpers;

	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UTextBlock> SchemaTitle;

	UPROPERTY(BlueprintReadOnly, Category = "Schema", meta = (BindWidget))
	TObjectPtr<UVerticalBox> SchemaOptionsVBox;

	UPROPERTY()
	UStateKeyInfoAsset* TargetSchema = nullptr;

	bool instantProcess = true;
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
	void ToggleVisibility()
	{
		if (SectionContent && ArrowText && ParentWidget)
		{
			const bool bIsVisible = SectionContent->GetVisibility() == ESlateVisibility::Visible;
			SectionContent->SetVisibility(bIsVisible ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
			ArrowText->SetText(FText::FromString(bIsVisible ? 
				TEXT(" >																																") : 
				TEXT(" v																																")));

			if (bIsVisible)
			{
				ParentWidget->ExpandedFoldouts.Remove(FoldoutPath);
			}
			else
			{
				ParentWidget->ExpandedFoldouts.Add(FoldoutPath);
			}
		}
	}
};
