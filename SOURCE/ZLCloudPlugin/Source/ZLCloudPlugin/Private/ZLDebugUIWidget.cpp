#include "ZLDebugUIWidget.h"
#include "ZLCloudPluginStateManager.h"
#include "ZLCloudPluginVersion.h"
#include "ZLCloudPluginPrivate.h"
#include "ZLCloudPluginDelegates.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Types/ReflectionMetadata.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Styling/SlateBrush.h"
#include "Templates/UnrealTemplate.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformProperties.h"
#include "HAL/PlatformFilemanager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/MultiLineEditableText.h"
#include "Components/PanelWidget.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ButtonSlot.h"
#include "Styling/AppStyle.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SToolTip.h"
#include "Interfaces/IPluginManager.h"

namespace
{
static const TCHAR* GPresetNewNameDefaultString = TEXT("Enter Preset Name...");
static const TCHAR* GDefaultSchemaPresetTitle = TEXT("Default Schema Values");
static constexpr float GPresetNameFieldPollIntervalSec = 0.05f;
static constexpr int32 GAcceptedValuesAutofillThreshold = 75;
// Max characters shown for a description label in dropdown lists when
// "Display Description as options" is enabled. Longer descriptions are truncated
// with "..."; tooltips still show the full text.
static constexpr int32 GDescriptionDropdownDisplayMaxChars = 50;
static const FString GDebugNullToken = TEXT("null");

static FString TruncateDescriptionForDropdownDisplay(const FString& InText)
{
	if (GDescriptionDropdownDisplayMaxChars <= 0 || InText.Len() <= GDescriptionDropdownDisplayMaxChars)
	{
		return InText;
	}
	return InText.Left(GDescriptionDropdownDisplayMaxChars) + TEXT("...");
}

static bool IsCameraFieldKey(const FString& InKeyName)
{
	FString LeafKey = InKeyName;
	int32 DotIndex = INDEX_NONE;
	if (InKeyName.FindLastChar(TEXT('.'), DotIndex))
	{
		LeafKey = InKeyName.Mid(DotIndex + 1);
	}

	return LeafKey.Equals(TEXT("camera"), ESearchCase::IgnoreCase);
}

static double ClampNumberValueForSchema(const FStateKeyInfo& SchemaInfo, double InValue)
{
	if (!SchemaInfo.bUseMinMax)
	{
		return InValue;
	}

	const double MinVal = FMath::Min(SchemaInfo.MinValue, SchemaInfo.MaxValue);
	const double MaxVal = FMath::Max(SchemaInfo.MinValue, SchemaInfo.MaxValue);
	return FMath::Clamp(InValue, MinVal, MaxVal);
}

static bool ZL_IsKeyboardFocusInsideEditableTextField(const UWidget* FocusedLeaf)
{
	for (const UWidget* Cur = FocusedLeaf; Cur; Cur = Cur->GetParent())
	{
		if (Cast<UEditableTextBox>(Cur) || Cast<UEditableText>(Cur) || Cast<UMultiLineEditableText>(Cur))
		{
			return true;
		}
	}
	return false;
}

template<typename TItem>
static TSharedRef<SWidget> CreateStandardSuggestionMenuContent(const TSharedRef<SListView<TSharedPtr<TItem>>>& InListView)
{
	return SNew(SBox)
		.MinDesiredWidth(260.f)
		.MaxDesiredHeight(260.f)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor::White)
				.Padding(FMargin(2.0f))
				[
					InListView
				]
		];
}

class SClickDetector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SClickDetector) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnClickedDelegate = InArgs._OnClicked;
		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			OnClickedDelegate.ExecuteIfBound();
		}
		return FReply::Unhandled();
	}

private:
	FSimpleDelegate OnClickedDelegate;
};

static bool SupportsNullableDebugInput(const FStateKeyInfo& SchemaInfo, EStateKeyDataType DataType)
{
	if (!SchemaInfo.bAllowNullValue)
	{
		return false;
	}

	return DataType == EStateKeyDataType::String
		|| DataType == EStateKeyDataType::StringArray
		|| DataType == EStateKeyDataType::Number
		|| DataType == EStateKeyDataType::NumberArray;
}

static bool IsNullTokenInput(const FString& Value)
{
	return Value.TrimStartAndEnd().Equals(GDebugNullToken, ESearchCase::IgnoreCase);
}

static bool AreJsonValuesEqual(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
{
	if (!Left.IsValid() || !Right.IsValid() || Left->Type != Right->Type)
	{
		return false;
	}

	switch (Left->Type)
	{
	case EJson::String:
		return Left->AsString().Equals(Right->AsString(), ESearchCase::CaseSensitive);
	case EJson::Number:
		return Left->AsNumber() == Right->AsNumber();
	case EJson::Boolean:
		return Left->AsBool() == Right->AsBool();
	case EJson::Array:
	{
		const TArray<TSharedPtr<FJsonValue>>& LeftArray = Left->AsArray();
		const TArray<TSharedPtr<FJsonValue>>& RightArray = Right->AsArray();
		if (LeftArray.Num() != RightArray.Num())
		{
			return false;
		}

		for (int32 Idx = 0; Idx < LeftArray.Num(); ++Idx)
		{
			if (!AreJsonValuesEqual(LeftArray[Idx], RightArray[Idx]))
			{
				return false;
			}
		}
		return true;
	}
	case EJson::Object:
	{
		const TSharedPtr<FJsonObject> LeftObj = Left->AsObject();
		const TSharedPtr<FJsonObject> RightObj = Right->AsObject();
		if (!LeftObj.IsValid() || !RightObj.IsValid() || LeftObj->Values.Num() != RightObj->Values.Num())
		{
			return false;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : LeftObj->Values)
		{
			const TSharedPtr<FJsonValue>* FoundRight = RightObj->Values.Find(Pair.Key);
			if (!FoundRight || !AreJsonValuesEqual(Pair.Value, *FoundRight))
			{
				return false;
			}
		}
		return true;
	}
	case EJson::Null:
		return true;
	default:
		return false;
	}
}

static TSharedPtr<FJsonObject> BuildStateDiffObject(
	const TSharedPtr<FJsonObject>& CurrentState,
	const TSharedPtr<FJsonObject>& CandidateState)
{
	if (!CurrentState.IsValid() || !CandidateState.IsValid())
	{
		return MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> DiffObject = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : CandidateState->Values)
	{
		const FString& Key = Pair.Key;
		const TSharedPtr<FJsonValue>& CandidateValue = Pair.Value;
		const TSharedPtr<FJsonValue>* CurrentValue = CurrentState->Values.Find(Key);

		if (!CurrentValue)
		{
			DiffObject->SetField(Key, CandidateValue);
			continue;
		}

		if (!CurrentValue->IsValid() || !CandidateValue.IsValid())
		{
			DiffObject->SetField(Key, CandidateValue);
			continue;
		}

		if (CandidateValue->Type == EJson::Object && (*CurrentValue)->Type == EJson::Object)
		{
			TSharedPtr<FJsonObject> NestedDiff = BuildStateDiffObject((*CurrentValue)->AsObject(), CandidateValue->AsObject());
			if (NestedDiff.IsValid() && NestedDiff->Values.Num() > 0)
			{
				DiffObject->SetObjectField(Key, NestedDiff);
			}
			continue;
		}

		if (!AreJsonValuesEqual(*CurrentValue, CandidateValue))
		{
			DiffObject->SetField(Key, CandidateValue);
		}
	}

	return DiffObject;
}
}

namespace ZLDebugUISchemaNavGlobals
{
static TArray<TWeakObjectPtr<UZLDebugUIWidget>> GWidgets;
static TSharedPtr<IInputProcessor> GProcessor;

class FSchemaNavInputPreprocessor final : public IInputProcessor
{
public:
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		for (int32 i = GWidgets.Num() - 1; i >= 0; --i)
		{
			UZLDebugUIWidget* W = GWidgets[i].Get();
			if (!IsValid(W))
			{
				GWidgets.RemoveAtSwap(i);
				continue;
			}
			if (W->TryHandleSchemaNavKeys(InKeyEvent))
			{
				return true;
			}
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("ZLDebugUISchemaNav"); }
};

void EnsureProcessorRegistered()
{
	if (GProcessor.IsValid() || GWidgets.Num() == 0 || !FSlateApplication::IsInitialized())
		return;
	GProcessor = MakeShared<FSchemaNavInputPreprocessor>();
#if UNREAL_5_5_OR_NEWER
	FSlateApplication::Get().RegisterInputPreProcessor(GProcessor, EInputPreProcessorType::PreGame);
#endif
}

namespace ZLDebugUIDimeDescriptions
{
static constexpr float GTooltipBackgroundAlpha = 0.35f * 1.3f;
static constexpr int32 GTooltipFontSize = 18;

static UWidget* CreateStyledTooltipWidget(UObject* Outer, const FString& DescriptionText)
{
	const FString TrimmedDescription = DescriptionText.TrimStartAndEnd();
	if (TrimmedDescription.IsEmpty())
	{
		return nullptr;
	}

	UBorder* TooltipBorder = NewObject<UBorder>(Outer ? Outer : GetTransientPackage());
	if (!TooltipBorder)
	{
		return nullptr;
	}

	TooltipBorder->SetBrushColor(FLinearColor(0.5f, 0.5f, 0.5f, GTooltipBackgroundAlpha));
	TooltipBorder->SetPadding(FMargin(6.f, 4.f));

	UTextBlock* TooltipText = NewObject<UTextBlock>(TooltipBorder);
	if (!TooltipText)
	{
		return nullptr;
	}

	TooltipText->SetText(FText::FromString(TrimmedDescription));
	FSlateFontInfo TooltipFontInfo = TooltipText->GetFont();
	TooltipFontInfo.Size = GTooltipFontSize;
	TooltipText->SetFont(TooltipFontInfo);
	TooltipText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	TooltipBorder->SetContent(TooltipText);

	return TooltipBorder;
}

static TSharedPtr<IToolTip> CreateStyledTooltip(const FString& DescriptionText)
{
	const FString TrimmedDescription = DescriptionText.TrimStartAndEnd();
	if (TrimmedDescription.IsEmpty())
	{
		return TSharedPtr<IToolTip>();
	}

	const FSlateFontInfo TooltipFont = FCoreStyle::GetDefaultFontStyle("Regular", GTooltipFontSize);
	return SNew(SToolTip)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.5f, 0.5f, 0.5f, GTooltipBackgroundAlpha))
				.Padding(FMargin(6.0f, 4.0f))
				[
					SNew(STextBlock)
						.Text(FText::FromString(TrimmedDescription))
						.Font(TooltipFont)
						.ColorAndOpacity(FSlateColor(FLinearColor::Black))
				]
		];
}

static bool SplitModelGroupKey(const FString& FullKey, FString& OutModelName, FString& OutGroupName)
{
	FString CleanKey = FullKey;
	const FString IndexToken = TEXT("_INDEX_");
	if (CleanKey.Contains(IndexToken))
	{
		int32 IndexStart = CleanKey.Find(IndexToken, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (IndexStart != INDEX_NONE)
		{
			CleanKey = CleanKey.Left(IndexStart);
		}
	}

	if (!CleanKey.Split(TEXT("."), &OutModelName, &OutGroupName, ESearchCase::CaseSensitive))
	{
		return false;
	}

	OutModelName = OutModelName.TrimStartAndEnd();
	OutGroupName = OutGroupName.TrimStartAndEnd();
	return !OutModelName.IsEmpty() && !OutGroupName.IsEmpty() && !OutGroupName.Contains(TEXT("."));
}

static FString ResolveCodeDescription(const UStateKeyInfoAsset* Schema, const FString& ModelName, const FString& GroupName, const FString& Code)
{
	if (!IsValid(Schema))
	{
		return FString();
	}

	for (const FDIMEModelMetadata& ModelMetadata : Schema->DimeModelData)
	{
		if (!ModelMetadata.ModelName.Equals(ModelName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		for (const FDIMEModelCodeMetadata& CodeMetadata : ModelMetadata.Codes)
		{
			const bool bGroupMatches = GroupName.TrimStartAndEnd().IsEmpty()
				? CodeMetadata.Group.TrimStartAndEnd().IsEmpty()
				: CodeMetadata.Group.Equals(GroupName, ESearchCase::IgnoreCase);
			if (!bGroupMatches ||
				!CodeMetadata.Code.Equals(Code, ESearchCase::IgnoreCase) ||
				CodeMetadata.DescriptionId == INDEX_NONE)
			{
				continue;
			}

			if (const FString* Description = ModelMetadata.DescriptionLookupById.Find(CodeMetadata.DescriptionId))
			{
				return Description->TrimStartAndEnd();
			}
		}
	}

	return FString();
}
}

static void MaybeUnregisterProcessor()
{
	if (GWidgets.Num() > 0 || !GProcessor.IsValid())
		return;
#if UNREAL_5_5_OR_NEWER
	if (FSlateApplication::IsInitialized())
		FSlateApplication::Get().UnregisterInputPreProcessor(GProcessor);
#endif
	GProcessor.Reset();
}

void RegisterDebugWidget(UZLDebugUIWidget* Widget)
{
	if (!IsValid(Widget))
		return;
	for (const TWeakObjectPtr<UZLDebugUIWidget>& Ptr : GWidgets)
		if (Ptr.Get() == Widget)
			return;
	GWidgets.Add(Widget);
	EnsureProcessorRegistered();
}

void UnregisterDebugWidget(UZLDebugUIWidget* Widget)
{
	if (!Widget)
		return;
	GWidgets.RemoveAll([Widget](const TWeakObjectPtr<UZLDebugUIWidget>& Ptr) {
		return !Ptr.IsValid() || Ptr.Get() == Widget;
	});
	MaybeUnregisterProcessor();
}
}

void UFoldoutHelper::ToggleVisibility()
{
	if (!SectionContent || !ArrowText || !ParentWidget)
	{
		return;
	}
	const bool bIsVisible = SectionContent->GetVisibility() == ESlateVisibility::Visible;
	SectionContent->SetVisibility(bIsVisible ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	ArrowText->SetText(FText::FromString(bIsVisible
		? TEXT(" >																																")
		: TEXT(" v																																")));
	if (bIsVisible)
	{
		ParentWidget->ExpandedFoldouts.Remove(FoldoutPath);
	}
	else
	{
		ParentWidget->ExpandedFoldouts.Add(FoldoutPath);
	}
}

void UStringArrayResizeHelper::ApplyResize()
{
	if (!IsValid(ParentWidget) || KeyName.IsEmpty())
	{
		return;
	}

	auto TryGetPendingStringArray = [this](const FString& InKey, TArray<FString>& OutArray) -> bool
	{
		if (!ParentWidget->ModifiedStateObject.IsValid())
		{
			return false;
		}

		TArray<FString> KeyParts;
		InKey.ParseIntoArray(KeyParts, TEXT("."), true);
		if (KeyParts.Num() == 0)
		{
			return false;
		}

		TSharedPtr<FJsonObject> CurrentObject = ParentWidget->ModifiedStateObject;
		for (int32 PartIdx = 0; PartIdx < KeyParts.Num() - 1; ++PartIdx)
		{
			if (!CurrentObject.IsValid() || !CurrentObject->HasTypedField<EJson::Object>(KeyParts[PartIdx]))
			{
				return false;
			}
			CurrentObject = CurrentObject->GetObjectField(KeyParts[PartIdx]);
		}

		if (!CurrentObject.IsValid() || !CurrentObject->HasTypedField<EJson::Array>(KeyParts.Last()))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>& JsonArray = CurrentObject->GetArrayField(KeyParts.Last());
		OutArray.Reset(JsonArray.Num());
		for (const TSharedPtr<FJsonValue>& JsonValue : JsonArray)
		{
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::String)
			{
				return false;
			}
			OutArray.Add(JsonValue->AsString());
		}

		return true;
	};

	TArray<FString> WorkingArray;
	bool bFound = false;

	UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
	if (StateManager)
	{
		StateManager->GetCurrentStateValue<TArray<FString>>(KeyName, WorkingArray, bFound);
	}

	if (!bFound)
	{
		bFound = TryGetPendingStringArray(KeyName, WorkingArray);
	}

	if (Delta > 0)
	{
		WorkingArray.Add(FillValue);
	}
	else if (Delta < 0 && WorkingArray.Num() > 0)
	{
		WorkingArray.SetNum(WorkingArray.Num() - 1);
	}

	const FStateKeyInfo* SchemaKeyInfo = nullptr;
	if (StateManager)
	{
		if (UStateKeyInfoAsset* SchemaAsset = StateManager->GetCurrentSchemaAsset())
		{
			SchemaKeyInfo = SchemaAsset->KeyInfos.Find(KeyName);
		}
	}

	const bool bOmitNullableEmptyFromPending = SchemaKeyInfo
		&& SchemaKeyInfo->bAllowNullValue
		&& WorkingArray.Num() == 0
		&& (SchemaKeyInfo->bDefaultValueIsNull || SchemaKeyInfo->DefaultStringArray.Num() == 0);

	if (bInstantBroadcast)
	{
		if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
		{
			Delegates->OnRecieveData.Broadcast(CreateStateChangeJsonStr<TArray<FString>>(KeyName, WorkingArray));
		}
	}

	if (bOmitNullableEmptyFromPending)
	{
		RemoveJsonObjectKey(KeyName, ParentWidget->ModifiedStateObject);
	}
	else
	{
		UpdateJsonObjectKey<TArray<FString>>(KeyName, WorkingArray, ParentWidget->ModifiedStateObject);
	}

	if (!bInstantBroadcast)
	{
		ParentWidget->TriggerRefreshUI();
	}
}

void UZLDebugUIWidget::NativeConstruct()
{
    Super::NativeConstruct();
	LoadDebugUIUserSettings();

    if (TargetSchema)
    {
        if (SchemaTitle)
        {
            SchemaTitle->SetText(FText::FromString("Schema: " + TargetSchema->GetName()));
        }
    }

    if (InstantChangeToggle)
        InstantChangeToggle->OnCheckStateChanged.AddDynamic(this, &UZLDebugUIWidget::OnSubmitStateInstantBoxChanged);

    if (SubmitStateBtn)
        SubmitStateBtn->OnClicked.AddDynamic(this, &UZLDebugUIWidget::OnSubmitState);

	if (ZLLogoButton)
		ZLLogoButton->OnClicked.AddDynamic(this, &UZLDebugUIWidget::OnToggleAllowResendCurrent);

	const bool bPresetMutateUiEnabled = ShouldPersistSchemaPresetsToDisk();
	if (bPresetMutateUiEnabled)
	{
		if (PresetAddNewButton)
			PresetAddNewButton->OnClicked.AddDynamic(this, &UZLDebugUIWidget::OnToggleAddNewPreset);

		if (PresetDeleteButton)
			PresetDeleteButton->OnClicked.AddDynamic(this, &UZLDebugUIWidget::OnPresetDeleteClicked);

		if (const FSlateBrush* CreateTabBrush = FAppStyle::GetBrush(TEXT("CurveEditor.CreateTab")))
		{
			if (PresetAddNewImage)
				PresetAddNewImage->SetBrush(*CreateTabBrush);
		}
		if (const FSlateBrush* DeleteTabBrush = FAppStyle::GetBrush(TEXT("CurveEditor.DeleteTab")))
		{
			if (PresetDeleteImage)
				PresetDeleteImage->SetBrush(*DeleteTabBrush);
		}
	}
	else
	{
		if (PresetAddNewButton)
			PresetAddNewButton->SetVisibility(ESlateVisibility::Collapsed);
		if (PresetDeleteButton)
			PresetDeleteButton->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (PresetSaveNewButton)
		PresetSaveNewButton->OnClicked.AddDynamic(this, &UZLDebugUIWidget::OnSaveNewPresetClicked);

	if (PresetOptionsDropdown)
		PresetOptionsDropdown->OnSelectionChanged.AddDynamic(this, &UZLDebugUIWidget::OnPresetDropdownSelectionChanged);

	UpdatePresetNewUIPanelVisibility();
	UpdatePresetSaveButtonEnabledState();

	if (bEnableDebugUIKeyboardControl && !bRegisteredForSchemaNavGlobalInput)
	{
		ZLDebugUISchemaNavGlobals::RegisterDebugWidget(this);
		bRegisteredForSchemaNavGlobalInput = true;
	}
}

FName UZLDebugUIProjectSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UZLDebugUIProjectSettings::GetContainerName() const
{
	return TEXT("Project");
}

#if WITH_EDITOR
FText UZLDebugUIProjectSettings::GetSectionText() const
{
	return NSLOCTEXT("ZLDebugUIProjectSettings", "ZLDebugUIProjectSettingsSection", "ZL Debug UI");
}
#endif

void UZLDebugUIWidget::NativeDestruct()
{
#if UNREAL_5_2_OR_NEWER
	if (PresetNameFieldPollTimerHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PresetNameFieldPollTimerHandle);
		}
		PresetNameFieldPollTimerHandle.Invalidate();
	}
#endif

	if (bRegisteredForSchemaNavGlobalInput)
	{
		ZLDebugUISchemaNavGlobals::UnregisterDebugWidget(this);
		bRegisteredForSchemaNavGlobalInput = false;
	}

    Super::NativeDestruct();

    if (InstantChangeToggle)
    {
        InstantChangeToggle->OnCheckStateChanged.RemoveDynamic(this, &UZLDebugUIWidget::OnSubmitStateInstantBoxChanged);
    }

    if (SubmitStateBtn)
    {
        SubmitStateBtn->OnClicked.RemoveDynamic(this, &UZLDebugUIWidget::OnSubmitState);
    }

	if (ZLLogoButton)
		ZLLogoButton->OnClicked.RemoveDynamic(this, &UZLDebugUIWidget::OnToggleAllowResendCurrent);

	if (PresetAddNewButton && ShouldPersistSchemaPresetsToDisk())
		PresetAddNewButton->OnClicked.RemoveDynamic(this, &UZLDebugUIWidget::OnToggleAddNewPreset);

	if (PresetDeleteButton && ShouldPersistSchemaPresetsToDisk())
		PresetDeleteButton->OnClicked.RemoveDynamic(this, &UZLDebugUIWidget::OnPresetDeleteClicked);

	if (PresetSaveNewButton)
		PresetSaveNewButton->OnClicked.RemoveDynamic(this, &UZLDebugUIWidget::OnSaveNewPresetClicked);

	if (PresetOptionsDropdown)
		PresetOptionsDropdown->OnSelectionChanged.RemoveDynamic(this, &UZLDebugUIWidget::OnPresetDropdownSelectionChanged);
}

void UZLDebugUIWidget::OnSubmitStateInstantBoxChanged(bool bIsChecked)
{
    instantProcess = bIsChecked;
    if (SubmitStateBtn)
    {
        SubmitStateBtn->SetVisibility(instantProcess ? ESlateVisibility::Hidden : ESlateVisibility::Visible);
        RebuildDebugUI();
    }
}

void UZLDebugUIWidget::OnSubmitState()
{
	TSharedPtr<FJsonObject> PayloadObject = ModifiedStateObject;
	if (UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager())
	{
		PayloadObject = BuildStateDiffObject(StateManager->GetCurrentAppState(), ModifiedStateObject);
	}

	if (PayloadObject.IsValid() && PayloadObject->Values.Num() > 0)
	{
		if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
		{
			FString JsonString;
			TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString, 1);
			FJsonSerializer::Serialize(PayloadObject.ToSharedRef(), JsonWriter);
			JsonWriter->Close();

			Delegates->OnRecieveData.Broadcast(JsonString);
		}
	}

	if (GetPendingNullStateKeys().Num() > 0)
	{
		if (UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager())
		{
			for (const FString& KeyToClear : GetPendingNullStateKeys())
			{
				StateManager->RemoveCurrentStateValue(KeyToClear);
			}
		}
		ClearPendingNullStateKeys();
		TriggerRefreshUI();
	}
}

void UZLDebugUIWidget::OnToggleAllowResendCurrent()
{
	UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();

	StateManager->s_defaultDoCurrentStateCompare = !StateManager->s_defaultDoCurrentStateCompare;
	allowResendCurrentValues = !allowResendCurrentValues;

	RebuildDebugUI();
}

void UZLDebugUIWidget::OnToggleAddNewPreset()
{
	if (addNewPresetUIVisible)
		return;

	addNewPresetUIVisible = true;
	bPresetPickKeysMode = true;
	UpdatePresetNewUIPanelVisibility();
	RebuildDebugUI();
	UpdatePresetSaveButtonEnabledState();
}

void UZLDebugUIWidget::OnPresetDeleteClicked()
{
	if (addNewPresetUIVisible)
	{
		addNewPresetUIVisible = false;
		bPresetPickKeysMode = false;
		UpdatePresetNewUIPanelVisibility();
		RebuildDebugUI();
		UpdatePresetSaveButtonEnabledState();
		return;
	}

	if (!PresetOptionsDropdown || !IsValid(TargetSchema))
		return;

	FString Selected = PresetOptionsDropdown->GetSelectedOption();
	Selected.TrimStartAndEndInline();
	if (Selected.IsEmpty())
		return;

	const FString DefaultTitle(GDefaultSchemaPresetTitle);
	if (Selected.Equals(DefaultTitle, ESearchCase::IgnoreCase))
		return;

	if (!CachedPresetsRoot.IsValid())
		CachedPresetsRoot = MakeShared<FJsonObject>();

	CachedPresetsRoot->RemoveField(Selected);

	if (ShouldPersistSchemaPresetsToDisk())
		SavePresetsCacheToDisk();

	PopulatePresetOptionsDropdown(DefaultTitle);
	BroadcastPresetJsonForName(DefaultTitle);
}

void UZLDebugUIWidget::UpdatePresetNewUIPanelVisibility()
{
	if (addNewPresetUIVisible)
	{
		if (PresetNewNameTextArea)
			PresetNewNameTextArea->SetVisibility(ESlateVisibility::Visible);

		if (PresetSaveNewButton)
			PresetSaveNewButton->SetVisibility(ESlateVisibility::Visible);

		if (PresetOptionsDropdown)
			PresetOptionsDropdown->SetVisibility(ESlateVisibility::Hidden);
	}
	else
	{
		if (PresetNewNameTextArea)
			PresetNewNameTextArea->SetVisibility(ESlateVisibility::Hidden);

		if (PresetSaveNewButton)
			PresetSaveNewButton->SetVisibility(ESlateVisibility::Hidden);

		if (PresetOptionsDropdown)
			PresetOptionsDropdown->SetVisibility(ESlateVisibility::Visible);
	}

	if (PresetDeleteButton)
	{
		if (ShouldPersistSchemaPresetsToDisk())
			PresetDeleteButton->SetVisibility(ESlateVisibility::Visible);
		else
			PresetDeleteButton->SetVisibility(ESlateVisibility::Collapsed);
	}

	UpdatePresetSaveButtonEnabledState();
	UpdatePresetNameFieldPollTimer();
}

void UZLDebugUIWidget::OnRemoveArrayEntry(UWidget* EntryToRemove)
{
    if (EntryToRemove && EntryToRemove->GetParent())
    {
        if (UPanelWidget* Parent = Cast<UPanelWidget>(EntryToRemove->GetParent()))
        {
            Parent->RemoveChild(EntryToRemove);
        }
    }
}

FString UZLDebugUIWidget::GetEnteredPresetName() const
{
	if (IsValid(PresetNewNameEntry))
		return PresetNewNameEntry->GetText().ToString();
	return FString();
}

void UZLDebugUIWidget::ResetPresetNewNameFieldToDefault()
{
	const FText DefaultTxt = FText::FromString(GPresetNewNameDefaultString);
	if (IsValid(PresetNewNameEntry))
	{
		PresetNewNameEntry->SetText(DefaultTxt);
		PresetNameFieldLastPolledText = FString(GPresetNewNameDefaultString);
		return;
	}
}

FString UZLDebugUIWidget::GetPresetsFilePathForSchema() const
{
	if (!IsValid(TargetSchema))
		return FString();

	FString SafeName = TargetSchema->GetName();
	for (TCHAR& Ch : SafeName)
	{
		if (Ch == TEXT('/') || Ch == TEXT('\\') || Ch == TEXT(':') || Ch == TEXT('*') || Ch == TEXT('?') || Ch == TEXT('"') || Ch == TEXT('<') || Ch == TEXT('>') || Ch == TEXT('|'))
			Ch = TEXT('_');
	}
	return FPaths::ProjectContentDir() / TEXT("ZLSchemaPresets") / (SafeName + TEXT(".zlschemapresets"));
}

bool UZLDebugUIWidget::ShouldPersistSchemaPresetsToDisk() const
{
	return !FPlatformProperties::RequiresCookedData();
}

void UZLDebugUIWidget::EnsurePresetsDirectoryExists() const
{
	const FString Path = GetPresetsFilePathForSchema();
	if (Path.IsEmpty())
		return;
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Path));
}

void UZLDebugUIWidget::SyncPresetsCacheFromDisk()
{
	if (!IsValid(TargetSchema))
		return;

	if (PresetsHydrationSchema.Get() != TargetSchema)
	{
		bPresetsStagedHydratedOnce = false;
		PresetsHydrationSchema = TargetSchema;
	}

	const FString FilePath = GetPresetsFilePathForSchema();
	if (FilePath.IsEmpty())
		return;

	const bool bPersist = ShouldPersistSchemaPresetsToDisk();
	if (!bPersist && bPresetsStagedHydratedOnce)
		return;

	CachedPresetsRoot = MakeShared<FJsonObject>();

	if (!FPaths::FileExists(FilePath))
	{
		if (!bPersist)
			bPresetsStagedHydratedOnce = true;
		return;
	}

	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *FilePath))
	{
		if (bPersist)
		{
			UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLDebugUIWidget: failed to read schema presets %s"), *FilePath);
		}
		else
		{
			UE_LOG(LogZLCloudPlugin, Log, TEXT("ZLDebugUIWidget: schema presets not readable (skipping) %s"), *FilePath);
		}
		if (!bPersist)
			bPresetsStagedHydratedOnce = true;
		return;
	}

	TSharedPtr<FJsonObject> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLDebugUIWidget: invalid JSON in schema presets %s"), *FilePath);
		if (!bPersist)
			bPresetsStagedHydratedOnce = true;
		return;
	}
	CachedPresetsRoot = Parsed;
	if (!bPersist)
	{
		// Keep editor-only carry-over presets hidden from cooked/packaged builds.
		CachedPresetsRoot->RemoveField(TEXT("Previous PIE Session"));
	}
	if (!bPersist)
		bPresetsStagedHydratedOnce = true;
}

void UZLDebugUIWidget::SavePresetsCacheToDisk()
{
	if (!ShouldPersistSchemaPresetsToDisk())
		return;

	const FString FilePath = GetPresetsFilePathForSchema();
	if (FilePath.IsEmpty())
		return;

	if (!CachedPresetsRoot.IsValid())
		return;

	EnsurePresetsDirectoryExists();
	FString Out;
	TSharedRef<TJsonWriter<TCHAR>> Writer = TJsonWriterFactory<TCHAR>::Create(&Out, 1);
	FJsonSerializer::Serialize(CachedPresetsRoot.ToSharedRef(), Writer);
	Writer->Close();
	if (!FFileHelper::SaveStringToFile(Out, *FilePath))
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLDebugUIWidget: failed to write schema presets %s"), *FilePath);
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Log, TEXT("ZLDebugUIWidget: wrote schema presets %s"), *FilePath);
	}
}

void UZLDebugUIWidget::PopulatePresetOptionsDropdown(const FString& SelectName)
{
	if (!PresetOptionsDropdown || addNewPresetUIVisible)
		return;

	if (!IsDebugUIPresented())
		return;

	const FString DefaultTitle(GDefaultSchemaPresetTitle);

	const FString SelectionToApply = [&]() -> FString
	{
		if (!SelectName.IsEmpty())
			return SelectName;
		if (bNeedsInitialDefaultPresetApply)
			return DefaultTitle;
		return PresetOptionsDropdown->GetSelectedOption();
	}();

	bPresetComboProgrammaticSelection = true;
	PresetOptionsDropdown->ClearOptions();

	PresetOptionsDropdown->AddOption(DefaultTitle);

	TArray<FString> Names;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : CachedPresetsRoot->Values)
	{
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
			continue;
		if (Pair.Key.Equals(DefaultTitle, ESearchCase::IgnoreCase))
			continue;
		Names.Add(Pair.Key);
	}
	Names.Sort();
	for (const FString& Name : Names)
		PresetOptionsDropdown->AddOption(Name);

	FString ToSelect = SelectionToApply;
	if (PresetOptionsDropdown->FindOptionIndex(ToSelect) == INDEX_NONE)
		ToSelect = DefaultTitle;
	PresetOptionsDropdown->SetSelectedOption(ToSelect);

	bPresetComboProgrammaticSelection = false;

	if (bNeedsInitialDefaultPresetApply && SelectName.IsEmpty())
	{
		bNeedsInitialDefaultPresetApply = false;
		BroadcastPresetJsonForName(DefaultTitle);
		// Rebuild so rows reflect state after OnRecieveData applies defaults (first Populate built with prior state).
		RebuildDebugUIWithNesting();
	}
}

void UZLDebugUIWidget::OnPresetDropdownSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bPresetComboProgrammaticSelection || SelectedItem.IsEmpty())
		return;

	BroadcastPresetJsonForName(SelectedItem);
}

void UZLDebugUIWidget::BuildPresetPayloadFromCheckedKeys(TSharedPtr<FJsonObject>& OutPayload)
{
	OutPayload = MakeShared<FJsonObject>();
	if (!IsValid(TargetSchema))
		return;

	TArray<FString> SortedKeysBuild;
	TargetSchema->KeyInfos.GenerateKeyArray(SortedKeysBuild);
	SortedKeysBuild.Sort();

	for (const FString& KeyName : SortedKeysBuild)
	{
		UCheckBox* const* IncludePtr = PresetIncludeBySchemaKey.Find(KeyName);
		if (!IncludePtr || !(*IncludePtr) || !(*IncludePtr)->IsChecked())
			continue;

		const FStateKeyInfo* SchemaKey = TargetSchema->KeyInfos.Find(KeyName);
		if (!SchemaKey)
			continue;

		const TArray<UWidget*>* WidgetsPtr = PresetValueWidgetsBySchemaKey.Find(KeyName);
		if (!WidgetsPtr || WidgetsPtr->Num() == 0)
			continue;

		const TArray<UWidget*>& Ws = *WidgetsPtr;
		switch (SchemaKey->GetDataTypeEnum())
		{
		case EStateKeyDataType::String:
			if (UStateKeyInputComboBox* C = Cast<UStateKeyInputComboBox>(Ws[0]))
			{
				const FString Selected = C->GetSelectedTrueValue();
				if (!(SchemaKey->bAllowNullValue && IsNullTokenInput(Selected)))
				{
					UpdateJsonObjectKey<FString>(KeyName, Selected, OutPayload);
				}
			}
			else if (UStateKeyInputTextBox* T = Cast<UStateKeyInputTextBox>(Ws[0]))
			{
				const FString TextValue = T->GetText().ToString();
				if (!(SchemaKey->bAllowNullValue && IsNullTokenInput(TextValue)))
				{
					UpdateJsonObjectKey<FString>(KeyName, TextValue, OutPayload);
				}
			}
			break;
		case EStateKeyDataType::Number:
			if (UStateKeyInputComboBox* C = Cast<UStateKeyInputComboBox>(Ws[0]))
			{
				const FString Selected = C->GetSelectedTrueValue();
				if (!(SchemaKey->bAllowNullValue && IsNullTokenInput(Selected)))
				{
					UpdateJsonObjectKey<double>(KeyName, ClampNumberValueForSchema(*SchemaKey, FCString::Atod(*Selected)), OutPayload);
				}
			}
			else if (UStateKeyInputTextBox* T = Cast<UStateKeyInputTextBox>(Ws[0]))
			{
				const FString TextValue = T->GetText().ToString();
				if (!(SchemaKey->bAllowNullValue && IsNullTokenInput(TextValue)))
				{
					UpdateJsonObjectKey<double>(KeyName, ClampNumberValueForSchema(*SchemaKey, FCString::Atod(*TextValue)), OutPayload);
				}
			}
			break;
		case EStateKeyDataType::Bool:
			if (UStateKeyInputCheckBox* C = Cast<UStateKeyInputCheckBox>(Ws[0]))
				UpdateJsonObjectKey<bool>(KeyName, C->IsChecked(), OutPayload);
			break;
		case EStateKeyDataType::StringArray:
		{
			TArray<FString> Arr;
			Arr.Reserve(Ws.Num());
			for (UWidget* W : Ws)
			{
				if (UStateKeyInputComboBox* C = Cast<UStateKeyInputComboBox>(W))
				{
					const FString Selected = C->GetSelectedTrueValue();
					if (SchemaKey->bAllowNullValue && IsNullTokenInput(Selected))
					{
						Arr.Empty();
						break;
					}
					Arr.Add(Selected);
				}
				else if (UStateKeyInputTextBox* T = Cast<UStateKeyInputTextBox>(W))
				{
					const FString TextValue = T->GetText().ToString();
					if (SchemaKey->bAllowNullValue && IsNullTokenInput(TextValue))
					{
						Arr.Empty();
						break;
					}
					Arr.Add(TextValue);
				}
			}
			if (Arr.Num() == Ws.Num())
				UpdateJsonObjectKey<TArray<FString>>(KeyName, Arr, OutPayload);
			break;
		}
		case EStateKeyDataType::NumberArray:
		{
			TArray<double> Arr;
			Arr.Reserve(Ws.Num());
			for (UWidget* W : Ws)
			{
				if (UStateKeyInputComboBox* C = Cast<UStateKeyInputComboBox>(W))
				{
					const FString Selected = C->GetSelectedTrueValue();
					if (SchemaKey->bAllowNullValue && IsNullTokenInput(Selected))
					{
						Arr.Empty();
						break;
					}
					Arr.Add(ClampNumberValueForSchema(*SchemaKey, FCString::Atod(*Selected)));
				}
				else if (UStateKeyInputTextBox* T = Cast<UStateKeyInputTextBox>(W))
				{
					const FString TextValue = T->GetText().ToString();
					if (SchemaKey->bAllowNullValue && IsNullTokenInput(TextValue))
					{
						Arr.Empty();
						break;
					}
					Arr.Add(ClampNumberValueForSchema(*SchemaKey, FCString::Atod(*TextValue)));
				}
			}
			if (Arr.Num() == Ws.Num())
				UpdateJsonObjectKey<TArray<double>>(KeyName, Arr, OutPayload);
			break;
		}
		case EStateKeyDataType::BoolArray:
		{
			TArray<bool> Arr;
			Arr.Reserve(Ws.Num());
			for (UWidget* W : Ws)
			{
				if (UStateKeyInputCheckBox* C = Cast<UStateKeyInputCheckBox>(W))
					Arr.Add(C->IsChecked());
			}
			if (Arr.Num() == Ws.Num())
				UpdateJsonObjectKey<TArray<bool>>(KeyName, Arr, OutPayload);
			break;
		}
		default:
			break;
		}
	}
}

void UZLDebugUIWidget::BuildDefaultSchemaValuesPayload(TSharedPtr<FJsonObject>& OutPayload)
{
	OutPayload = MakeShared<FJsonObject>();
	if (!IsValid(TargetSchema))
		return;

	TArray<FString> SortedKeys;
	TargetSchema->KeyInfos.GenerateKeyArray(SortedKeys);
	SortedKeys.Sort();

	for (const FString& KeyName : SortedKeys)
	{
		const FStateKeyInfo* SchemaKey = TargetSchema->KeyInfos.Find(KeyName);
		if (!SchemaKey)
			continue;

		if (SchemaKey->bAllowNullValue)
		{
			const bool bSkipNullableDefault = SchemaKey->bDefaultValueIsNull
				|| (SchemaKey->GetDataTypeEnum() == EStateKeyDataType::String && SchemaKey->DefaultStringValue.TrimStartAndEnd().IsEmpty())
				|| (SchemaKey->GetDataTypeEnum() == EStateKeyDataType::StringArray && SchemaKey->DefaultStringArray.Num() == 0)
				|| (SchemaKey->GetDataTypeEnum() == EStateKeyDataType::NumberArray && SchemaKey->DefaultNumberArray.Num() == 0);
			if (bSkipNullableDefault)
			{
				continue;
			}
		}

		switch (SchemaKey->GetDataTypeEnum())
		{
		case EStateKeyDataType::String:
			UpdateJsonObjectKey<FString>(KeyName, SchemaKey->DefaultStringValue, OutPayload);
			break;
		case EStateKeyDataType::Number:
			UpdateJsonObjectKey<double>(KeyName, SchemaKey->DefaultNumberValue, OutPayload);
			break;
		case EStateKeyDataType::Bool:
			UpdateJsonObjectKey<bool>(KeyName, SchemaKey->DefaultBoolValue, OutPayload);
			break;
		case EStateKeyDataType::StringArray:
			UpdateJsonObjectKey<TArray<FString>>(KeyName, SchemaKey->DefaultStringArray, OutPayload);
			break;
		case EStateKeyDataType::NumberArray:
			UpdateJsonObjectKey<TArray<double>>(KeyName, SchemaKey->DefaultNumberArray, OutPayload);
			break;
		case EStateKeyDataType::BoolArray:
			UpdateJsonObjectKey<TArray<bool>>(KeyName, SchemaKey->DefaultBoolArray, OutPayload);
			break;
		default:
			break;
		}
	}
}

void UZLDebugUIWidget::BroadcastPresetJsonForName(const FString& PresetName)
{
	if (PresetName.IsEmpty())
		return;

	if (!IsDebugUIPresented())
		return;

	if (UZLCloudPluginStateManagerBlueprints::IsInitialStateRequest())
		return;

	UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates();
	if (!Delegates)
		return;

	TSharedPtr<FJsonObject> PresetObj;
	if (PresetName.Equals(GDefaultSchemaPresetTitle, ESearchCase::IgnoreCase))
	{
		BuildDefaultSchemaValuesPayload(PresetObj);
	}
	else
	{
		const TSharedPtr<FJsonObject>* Ptr = nullptr;
		if (!CachedPresetsRoot->TryGetObjectField(PresetName, Ptr) || !Ptr || !Ptr->IsValid())
			return;
		PresetObj = *Ptr;
	}

	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString, 1);
	FJsonSerializer::Serialize(PresetObj.ToSharedRef(), JsonWriter);
	JsonWriter->Close();
	Delegates->OnRecieveData.Broadcast(JsonString);
}

void UZLDebugUIWidget::UpdatePresetSaveButtonEnabledState()
{
	if (!PresetSaveNewButton)
		return;

	if (!addNewPresetUIVisible)
	{
		PresetSaveNewButton->SetIsEnabled(true);
		return;
	}

	FString Name = GetEnteredPresetName();
	Name.TrimStartAndEndInline();
	const bool bNameOk = !Name.IsEmpty()
		&& !Name.Equals(GPresetNewNameDefaultString, ESearchCase::IgnoreCase)
		&& !Name.Equals(GDefaultSchemaPresetTitle, ESearchCase::IgnoreCase);

	bool bAnyInclude = false;
	for (const TPair<FString, UCheckBox*>& Pair : PresetIncludeBySchemaKey)
	{
		if (Pair.Value && Pair.Value->IsChecked())
		{
			bAnyInclude = true;
			break;
		}
	}

	PresetSaveNewButton->SetIsEnabled(bNameOk && bAnyInclude);
}

void UZLDebugUIWidget::UpdatePresetNameFieldPollTimer()
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerManager& TM = World->GetTimerManager();
	if (addNewPresetUIVisible && IsValid(PresetNewNameEntry))
	{
		PresetNameFieldLastPolledText = PresetNewNameEntry->GetText().ToString();
#if UNREAL_5_2_OR_NEWER
		TM.SetTimer(PresetNameFieldPollTimerHandle, this, &UZLDebugUIWidget::PollPresetNameFieldForSaveButton,
			GPresetNameFieldPollIntervalSec, true);
#endif
	}
#if UNREAL_5_2_OR_NEWER
	else if (PresetNameFieldPollTimerHandle.IsValid())
	{
		TM.ClearTimer(PresetNameFieldPollTimerHandle);
		PresetNameFieldPollTimerHandle.Invalidate();
	}
#endif
}

void UZLDebugUIWidget::PollPresetNameFieldForSaveButton()
{
	if (!addNewPresetUIVisible || !IsValid(PresetNewNameEntry))
	{
		return;
	}

	const FString Current = PresetNewNameEntry->GetText().ToString();
	if (Current.Equals(PresetNameFieldLastPolledText))
	{
		return;
	}

	PresetNameFieldLastPolledText = Current;
	UpdatePresetSaveButtonEnabledState();
}

void UZLDebugUIWidget::OnPresetIncludeCheckboxStateChanged(bool bIsChecked)
{
	UpdatePresetSaveButtonEnabledState();
}

void UZLDebugUIWidget::OnSaveNewPresetClicked()
{
	if (!IsValid(TargetSchema))
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLDebugUIWidget: save preset ignored (no TargetSchema)."));
		return;
	}

	FString PresetName = GetEnteredPresetName();
	PresetName.TrimStartAndEndInline();
	if (PresetName.IsEmpty() || PresetName.Equals(GPresetNewNameDefaultString, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLDebugUIWidget: save preset ignored (empty or placeholder name). Enter a unique preset name."));
		return;
	}

	if (PresetName.Equals(GDefaultSchemaPresetTitle, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLDebugUIWidget: save preset ignored (reserved preset name)."));
		return;
	}

	if (!CachedPresetsRoot.IsValid())
		CachedPresetsRoot = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> Payload;
	BuildPresetPayloadFromCheckedKeys(Payload);
	if (!Payload.IsValid() || Payload->Values.Num() == 0)
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLDebugUIWidget: save preset ignored (no schema keys selected for preset)."));
		return;
	}
	CachedPresetsRoot->SetObjectField(PresetName, Payload);
	SavePresetsCacheToDisk();

	addNewPresetUIVisible = false;
	bPresetPickKeysMode = false;
	UpdatePresetNewUIPanelVisibility();
	RebuildDebugUI();
	PopulatePresetOptionsDropdown(PresetName);
	ResetPresetNewNameFieldToDefault();
}

UWidget* UZLDebugUIWidget::GetUMGWidgetFromSlateWidget(const SWidget* StartWidget)
{
	for (const SWidget* CurrentWidget = StartWidget; CurrentWidget != nullptr; CurrentWidget = CurrentWidget->GetParentWidget().Get())
	{
		if (TSharedPtr<FReflectionMetaData> Meta = CurrentWidget->GetMetaData<FReflectionMetaData>())
		{
			if (UWidget* W = Cast<UWidget>(Meta->SourceObject.Get()))
			{
				return W;
			}
		}
	}
	return nullptr;
}

UWidget* UZLDebugUIWidget::GetFocusedUMGWidgetFromSlateForUser(uint32 UserIndex)
{
	TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetUserFocusedWidget(UserIndex);
	if (!FocusedWidget.IsValid() && FSlateApplication::Get().GetUserIndexForKeyboard() == UserIndex)
	{
		FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
	}
	if (!FocusedWidget.IsValid())
	{
		return nullptr;
	}
	return GetUMGWidgetFromSlateWidget(FocusedWidget.Get());
}

bool UZLDebugUIWidget::IsNavWidgetHierarchyVisible(const UWidget* Widget)
{
	for (const UWidget* Cur = Widget; Cur; Cur = Cur->GetParent())
	{
		const ESlateVisibility Vis = Cur->GetVisibility();
		// Match UMG "shown" widgets: Visible / HitTestInvisible / SelfHitTestInvisible are all drawn.
		// Only Collapsed and Hidden remove the widget from effective display (foldouts use Collapsed).
		if (Vis == ESlateVisibility::Collapsed || Vis == ESlateVisibility::Hidden)
		{
			return false;
		}
	}
	return true;
}

void UZLDebugUIWidget::ResetSchemaNavOrder()
{
	SchemaNavEntries.Reset();
}

void UZLDebugUIWidget::CaptureSchemaNavSelectionToken()
{
	SchemaNavPersistSelectionToken.Reset();
	if (!SchemaNavEntries.IsValidIndex(SchemaNavSelectedIndex))
	{
		return;
	}
	const FDebugSchemaNavEntry& E = SchemaNavEntries[SchemaNavSelectedIndex];
	if (E.Kind == EDebugSchemaNavEntryKind::Foldout && E.FoldoutHelper)
	{
		SchemaNavPersistSelectionToken = FString::Printf(TEXT("$FOLDOUT:%s"), *E.FoldoutHelper->FoldoutPath);
	}
	else if (E.Kind == EDebugSchemaNavEntryKind::Value && E.ValueWidget)
	{
		const FString K = GetKeyTokenFromSchemaValueWidget(E.ValueWidget.Get());
		if (!K.IsEmpty())
		{
			SchemaNavPersistSelectionToken = FString::Printf(TEXT("$KEY:%s"), *K);
		}
	}
}

void UZLDebugUIWidget::RestoreSchemaNavSelectionAfterRebuild()
{
	const int32 N = SchemaNavEntries.Num();
	if (N == 0)
	{
		SchemaNavSelectedIndex = 0;
		return;
	}

	if (!SchemaNavPersistSelectionToken.IsEmpty())
	{
		for (int32 i = 0; i < N; ++i)
		{
			if (SchemaNavEntryMatchesToken(i, SchemaNavPersistSelectionToken))
			{
				SchemaNavSelectedIndex = i;
				SchemaNavPersistSelectionToken.Reset();
				if (!IsSchemaNavEntrySelectable(SchemaNavSelectedIndex))
				{
					SchemaNavSelectedIndex = FindFirstSelectableSchemaNavIndex();
				}
				return;
			}
		}
		SchemaNavPersistSelectionToken.Reset();
	}

	SchemaNavSelectedIndex = FMath::Clamp(SchemaNavSelectedIndex, 0, N - 1);
	if (!IsSchemaNavEntrySelectable(SchemaNavSelectedIndex))
	{
		SchemaNavSelectedIndex = FindFirstSelectableSchemaNavIndex();
	}
}

void UZLDebugUIWidget::RegisterSchemaNavFoldout(UBorder* RowBorder, UFoldoutHelper* FoldoutHelper)
{
	if (!RowBorder || !FoldoutHelper)
		return;
	FDebugSchemaNavEntry Entry;
	Entry.Kind = EDebugSchemaNavEntryKind::Foldout;
	Entry.FoldoutHelper = FoldoutHelper;
	Entry.RowBorder = RowBorder;
	SchemaNavEntries.Add(Entry);
}

void UZLDebugUIWidget::RegisterSchemaNavValue(UBorder* RowBorder, UWidget* ValueWidget)
{
	if (!RowBorder || !ValueWidget)
		return;
	FDebugSchemaNavEntry Entry;
	Entry.Kind = EDebugSchemaNavEntryKind::Value;
	Entry.ValueWidget = ValueWidget;
	Entry.RowBorder = RowBorder;
	SchemaNavEntries.Add(Entry);
}

FString UZLDebugUIWidget::GetKeyTokenFromSchemaValueWidget(const UWidget* Widget)
{
	if (const UStateKeyInputComboBox* C = Cast<UStateKeyInputComboBox>(Widget))
		return C->KeyName;
	if (const UStateKeyInputTextBox* T = Cast<UStateKeyInputTextBox>(Widget))
		return T->KeyName;
	if (const UStateKeyInputCheckBox* CB = Cast<UStateKeyInputCheckBox>(Widget))
		return CB->KeyName;
	return FString();
}

bool UZLDebugUIWidget::SchemaNavEntryMatchesToken(int32 Idx, const FString& Token) const
{
	if (!SchemaNavEntries.IsValidIndex(Idx))
		return false;
	const FDebugSchemaNavEntry& E = SchemaNavEntries[Idx];
	if (Token.StartsWith(TEXT("$FOLDOUT:")))
	{
		const FString Path = Token.Mid(9);
		return E.Kind == EDebugSchemaNavEntryKind::Foldout && E.FoldoutHelper && E.FoldoutHelper->FoldoutPath == Path;
	}
	if (Token.StartsWith(TEXT("$KEY:")))
	{
		const FString Key = Token.Mid(5);
		return E.Kind == EDebugSchemaNavEntryKind::Value && E.ValueWidget
			&& GetKeyTokenFromSchemaValueWidget(E.ValueWidget.Get()) == Key;
	}
	return false;
}

bool UZLDebugUIWidget::IsSchemaNavEntrySelectable(int32 Idx) const
{
	if (!SchemaNavEntries.IsValidIndex(Idx))
		return false;
	const FDebugSchemaNavEntry& E = SchemaNavEntries[Idx];
	if (!IsValid(E.RowBorder) || !IsNavWidgetHierarchyVisible(E.RowBorder))
		return false;
	if (E.Kind == EDebugSchemaNavEntryKind::Foldout)
		return true;
	if (!IsValid(E.ValueWidget))
		return false;
	return IsNavWidgetHierarchyVisible(E.ValueWidget.Get());
}

int32 UZLDebugUIWidget::FindFirstSelectableSchemaNavIndex() const
{
	for (int32 i = 0; i < SchemaNavEntries.Num(); ++i)
		if (IsSchemaNavEntrySelectable(i))
			return i;
	return 0;
}

void UZLDebugUIWidget::ApplySchemaNavSelectionVisual()
{
	TSet<UBorder*> Borders;
	for (const FDebugSchemaNavEntry& E : SchemaNavEntries)
	{
		if (UBorder* B = E.RowBorder.Get())
		{
			Borders.Add(B);
		}
	}
	for (UBorder* B : Borders)
	{
		ApplySchemaRowBorderFocusState(B, false);
	}
	if (SchemaNavEntries.IsValidIndex(SchemaNavSelectedIndex))
	{
		if (UBorder* Sel = SchemaNavEntries[SchemaNavSelectedIndex].RowBorder.Get())
		{
			ApplySchemaRowBorderFocusState(Sel, true);
		}
	}
}

void UZLDebugUIWidget::FocusWidgetForSchemaNavIndex(int32 Idx)
{
	if (!SchemaNavEntries.IsValidIndex(Idx))
		return;
	const FDebugSchemaNavEntry& E = SchemaNavEntries[Idx];
	if (E.Kind == EDebugSchemaNavEntryKind::Foldout)
	{
		if (IsValid(E.RowBorder))
			if (UWidget* Inner = E.RowBorder->GetContent())
				Inner->SetKeyboardFocus();
	}
	else if (UWidget* V = E.ValueWidget.Get())
		V->SetKeyboardFocus();
}

void UZLDebugUIWidget::SchemaNavMoveSelectionVertical(int32 Delta)
{
	const int32 N = SchemaNavEntries.Num();
	if (N == 0 || Delta == 0)
		return;
	int32 Idx = SchemaNavSelectedIndex;
	if (!SchemaNavEntries.IsValidIndex(Idx))
		Idx = FindFirstSelectableSchemaNavIndex();
	for (int32 Tries = 0; Tries < N; ++Tries)
	{
		Idx = (Idx + Delta + N) % N;
		if (IsSchemaNavEntrySelectable(Idx))
		{
			SchemaNavSelectedIndex = Idx;
			ApplySchemaNavSelectionVisual();
			FocusWidgetForSchemaNavIndex(Idx);
			return;
		}
	}
}

void UZLDebugUIWidget::ApplyFoldoutExpandCollapse(UFoldoutHelper* FoldoutHelper, bool bExpand)
{
	if (!FoldoutHelper || !FoldoutHelper->SectionContent)
		return;
	const bool bIsExpanded = FoldoutHelper->SectionContent->GetVisibility() == ESlateVisibility::Visible;
	if (bExpand && !bIsExpanded)
		FoldoutHelper->ToggleVisibility();
	else if (!bExpand && bIsExpanded)
		FoldoutHelper->ToggleVisibility();
}

void UZLDebugUIWidget::HandleSchemaNavLeftRight(bool bRight)
{
	if (!SchemaNavEntries.IsValidIndex(SchemaNavSelectedIndex))
		return;
	const FDebugSchemaNavEntry& E = SchemaNavEntries[SchemaNavSelectedIndex];
	if (E.Kind == EDebugSchemaNavEntryKind::Foldout && E.FoldoutHelper)
	{
		ApplyFoldoutExpandCollapse(E.FoldoutHelper.Get(), bRight);
		ApplySchemaNavSelectionVisual();
		return;
	}
	if (UWidget* W = E.ValueWidget.Get())
	{
		if (Cast<UStateKeyInputTextBox>(W))
			return;
		if (UStateKeyInputComboBox* Combo = Cast<UStateKeyInputComboBox>(W))
			CycleSchemaCombo(Combo, bRight);
		else if (UStateKeyInputCheckBox* CB = Cast<UStateKeyInputCheckBox>(W))
			ToggleSchemaCheckBox(CB);
	}
}

void UZLDebugUIWidget::ApplySchemaRowBorderFocusState(UBorder* Border, bool bFocused)
{
	if (!IsValid(Border))
		return;
	FSlateBrush B;
	if (bFocused)
	{
		B.DrawAs = ESlateBrushDrawType::Border;
		B.TintColor = FSlateColor(FLinearColor::Black);
		B.Margin = FMargin(2.0f);
	}
	else
		B.DrawAs = ESlateBrushDrawType::NoDrawType;
	Border->SetBrush(B);
	Border->SetBrushColor(FLinearColor::White);
}

bool UZLDebugUIWidget::IsWidgetUnderSchemaOptionsBox(const UWidget* Widget) const
{
	if (!Widget || !SchemaOptionsVBox)
		return false;
	for (const UWidget* Cur = Widget; Cur; Cur = Cur->GetParent())
		if (Cur == SchemaOptionsVBox)
			return true;
	return false;
}

void UZLDebugUIWidget::SchemaNavSetSelectedIndexIfChanged(int32 NewIdx)
{
	if (SchemaNavSelectedIndex == NewIdx)
		return;
	SchemaNavSelectedIndex = NewIdx;
	ApplySchemaNavSelectionVisual();
}

void UZLDebugUIWidget::SyncSchemaNavSelectionFromUMGWidget(UWidget* LeafUMG)
{
	if (!LeafUMG || SchemaNavEntries.Num() == 0)
		return;
	for (UWidget* Cur = LeafUMG; Cur; Cur = Cur->GetParent())
	{
		for (int32 i = 0; i < SchemaNavEntries.Num(); ++i)
		{
			const FDebugSchemaNavEntry& E = SchemaNavEntries[i];
			if (E.Kind == EDebugSchemaNavEntryKind::Value && E.ValueWidget.Get() == Cur)
			{
				SchemaNavSetSelectedIndexIfChanged(i);
				return;
			}
			if (E.Kind == EDebugSchemaNavEntryKind::Foldout && E.RowBorder.Get() == Cur)
			{
				SchemaNavSetSelectedIndexIfChanged(i);
				return;
			}
		}
		if (UBorder* HitBorder = Cast<UBorder>(Cur))
		{
			for (int32 i = 0; i < SchemaNavEntries.Num(); ++i)
			{
				const FDebugSchemaNavEntry& E = SchemaNavEntries[i];
				if (E.Kind == EDebugSchemaNavEntryKind::Value && E.RowBorder.Get() == HitBorder)
				{
					SchemaNavSetSelectedIndexIfChanged(i);
					return;
				}
			}
		}
	}
}

void UZLDebugUIWidget::NativeOnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent)
{
	Super::NativeOnFocusChanging(PreviousFocusPath, NewWidgetPath, InFocusEvent);

	if (!NewWidgetPath.IsValid() || NewWidgetPath.Widgets.Num() == 0 || SchemaNavEntries.Num() == 0)
	{
		return;
	}

	for (int32 i = NewWidgetPath.Widgets.Num() - 1; i >= 0; --i)
	{
		UWidget* const UMG = GetUMGWidgetFromSlateWidget(NewWidgetPath.Widgets[i].GetWidgetPtr());
		if (!UMG || !IsWidgetUnderSchemaOptionsBox(UMG))
		{
			continue;
		}
		SyncSchemaNavSelectionFromUMGWidget(UMG);
		return;
	}
}

FReply UZLDebugUIWidget::NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (SchemaNavEntries.Num() > 0 && SchemaOptionsVBox)
	{
		FSlateApplication& SlateApp = FSlateApplication::Get();
		const FWidgetPath Path = SlateApp.LocateWindowUnderMouse(
			InMouseEvent.GetScreenSpacePosition(),
			SlateApp.GetInteractiveTopLevelWindows(),
			/*bIgnoreEnabledStatus*/ true,
			InMouseEvent.GetUserIndex());

		if (Path.IsValid() && Path.Widgets.Num() > 0)
		{
			for (int32 i = Path.Widgets.Num() - 1; i >= 0; --i)
			{
				UWidget* const UMG = GetUMGWidgetFromSlateWidget(Path.Widgets[i].GetWidgetPtr());
				if (!UMG || !IsWidgetUnderSchemaOptionsBox(UMG))
				{
					continue;
				}
				SyncSchemaNavSelectionFromUMGWidget(UMG);
				break;
			}
		}
	}

	return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
}

void UZLDebugUIWidget::CycleSchemaCombo(UStateKeyInputComboBox* Combo, bool bForward)
{
	if (!IsValid(Combo))
		return;
	const int32 Count = Combo->GetOptionCount();
	if (Count <= 1)
		return;
	int32 Idx = Combo->FindOptionIndex(Combo->GetSelectedOption());
	if (Idx == INDEX_NONE)
		Idx = 0;
	const int32 Dir = bForward ? 1 : -1;
	Idx = (Idx + Dir + Count) % Count;
	Combo->SetSelectedOption(Combo->GetOptionAtIndex(Idx));
}

void UZLDebugUIWidget::ToggleSchemaCheckBox(UStateKeyInputCheckBox* CheckBox)
{
	if (!IsValid(CheckBox))
		return;
	const bool bNewChecked = !CheckBox->IsChecked();
	CheckBox->SetIsChecked(bNewChecked);
	CheckBox->OnCheckBoxChanged(bNewChecked);
}

bool UZLDebugUIWidget::TryHandleSchemaNavKeys(const FKeyEvent& InKeyEvent)
{
	if (const UZLDebugUIProjectSettings* Settings = GetDefault<UZLDebugUIProjectSettings>())
	{
		if (Settings->enableDebugUIKeyboardControl != bEnableDebugUIKeyboardControl)
		{
			SetEnableDebugUIKeyboardControl(Settings->enableDebugUIKeyboardControl);
		}
	}

	if (!bEnableDebugUIKeyboardControl)
		return false;
	if (SchemaNavEntries.Num() == 0 || !SchemaOptionsVBox)
		return false;
	if (!IsInViewport())
		return false;
	const ESlateVisibility RootVis = GetVisibility();
	if (RootVis == ESlateVisibility::Collapsed || RootVis == ESlateVisibility::Hidden)
		return false;
	const uint32 UserIndex = InKeyEvent.GetUserIndex();
	UWidget* const FocusedLeaf = GetFocusedUMGWidgetFromSlateForUser(UserIndex);
	if (ZL_IsKeyboardFocusInsideEditableTextField(FocusedLeaf))
		return false;
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Up || Key == EKeys::Down)
	{
		SchemaNavMoveSelectionVertical(Key == EKeys::Up ? -1 : 1);
		return true;
	}
	if (Key == EKeys::Left || Key == EKeys::Right)
	{
		HandleSchemaNavLeftRight(Key == EKeys::Right);
		return true;
	}
	return false;
}

FReply UZLDebugUIWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (TryHandleSchemaNavKeys(InKeyEvent))
	{
		return FReply::Handled();
	}

	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UZLDebugUIWidget::LoadDebugUIUserSettings()
{
	if (const UZLDebugUIProjectSettings* Settings = GetDefault<UZLDebugUIProjectSettings>())
	{
		bEnableDebugUIKeyboardControl = Settings->enableDebugUIKeyboardControl;
	}

	if (bEnableDebugUIKeyboardControl && !bRegisteredForSchemaNavGlobalInput)
	{
		ZLDebugUISchemaNavGlobals::RegisterDebugWidget(this);
		bRegisteredForSchemaNavGlobalInput = true;
	}
	else if (!bEnableDebugUIKeyboardControl && bRegisteredForSchemaNavGlobalInput)
	{
		ZLDebugUISchemaNavGlobals::UnregisterDebugWidget(this);
		bRegisteredForSchemaNavGlobalInput = false;
	}
}

void UZLDebugUIWidget::SaveDebugUIUserSettings() const
{
	if (UZLDebugUIProjectSettings* Settings = GetMutableDefault<UZLDebugUIProjectSettings>())
	{
		Settings->enableDebugUIKeyboardControl = bEnableDebugUIKeyboardControl;
		Settings->SaveConfig();
	}
}

void UZLDebugUIWidget::SetEnableDebugUIKeyboardControl(bool bEnable)
{
	if (bEnableDebugUIKeyboardControl == bEnable)
		return;

	bEnableDebugUIKeyboardControl = bEnable;
	SaveDebugUIUserSettings();

	if (bEnableDebugUIKeyboardControl && !bRegisteredForSchemaNavGlobalInput)
	{
		ZLDebugUISchemaNavGlobals::RegisterDebugWidget(this);
		bRegisteredForSchemaNavGlobalInput = true;
	}
	else if (!bEnableDebugUIKeyboardControl && bRegisteredForSchemaNavGlobalInput)
	{
		ZLDebugUISchemaNavGlobals::UnregisterDebugWidget(this);
		bRegisteredForSchemaNavGlobalInput = false;
	}
}

void UStateKeyInputComboBox::OnComboBoxChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (ParentDebugUI && ParentDebugUI->IsSchemaValueRebuildInProgress())
	{
		return;
	}

    // When the dropdown shows descriptions as labels, the incoming SelectedItem is the
    // display label; convert it back to the true value before broadcasting.
    SelectedItem = GetTrueValueForDisplayLabel(SelectedItem);

    FString Json;

    FString CleanKey = KeyName;
    int32 ArrayIndex = -1;
    const FString IndexToken = TEXT("_INDEX_");

    UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();

    if (KeyName.Contains(IndexToken))
    {
        int32 IndexStart = KeyName.Find(IndexToken, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
        if (IndexStart != INDEX_NONE)
        {
            FString IndexStr = KeyName.Mid(IndexStart + IndexToken.Len());
            ArrayIndex = FCString::Atoi(*IndexStr);
            CleanKey = KeyName.Left(IndexStart);
        }
    }

    const bool bTreatAsNullSelection = SupportsNullableDebugInput(StateKeyInfo, DataType) && IsNullTokenInput(SelectedItem);
    if (bTreatAsNullSelection)
    {
        if (ArrayIndex >= 0 && DataType == EStateKeyDataType::StringArray)
        {
            TArray<FString> WorkingArray;
            bool Found = false;
            if (StateManager)
            {
                StateManager->GetCurrentStateValue<TArray<FString>>(CleanKey, WorkingArray, Found);
            }
            if (!Found)
            {
                WorkingArray = StateKeyInfo.DefaultStringArray;
            }

            if (WorkingArray.IsValidIndex(ArrayIndex))
            {
                WorkingArray.RemoveAt(ArrayIndex);
            }

            const bool bOmitNullableEmptyFromPending = StateKeyInfo.bAllowNullValue
                && WorkingArray.Num() == 0
                && (StateKeyInfo.bDefaultValueIsNull || StateKeyInfo.DefaultStringArray.Num() == 0);

            if (InstantBroadcastChange)
            {
                if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
                {
                    Delegates->OnRecieveData.Broadcast(CreateStateChangeJsonStr<TArray<FString>>(CleanKey, WorkingArray));
                }
                if (ParentDebugUI)
                {
                    if (bOmitNullableEmptyFromPending)
                    {
                        RemoveJsonObjectKey(CleanKey, ParentDebugUI->ModifiedStateObject);
                    }
                    else
                    {
                        UpdateJsonObjectKey<TArray<FString>>(CleanKey, WorkingArray, ParentDebugUI->ModifiedStateObject);
                    }
                    ParentDebugUI->ClearPendingNullStateKey(CleanKey);
                }
            }
            else if (ParentDebugUI)
            {
                if (bOmitNullableEmptyFromPending)
                {
                    RemoveJsonObjectKey(CleanKey, ParentDebugUI->ModifiedStateObject);
                    ParentDebugUI->StagePendingNullStateKey(CleanKey);
                }
                else
                {
                    UpdateJsonObjectKey<TArray<FString>>(CleanKey, WorkingArray, ParentDebugUI->ModifiedStateObject);
                    ParentDebugUI->ClearPendingNullStateKey(CleanKey);
                }
            }
            return;
        }

        if (InstantBroadcastChange)
        {
            if (StateManager)
            {
                StateManager->RemoveCurrentStateValue(CleanKey);
            }
            if (ParentDebugUI)
            {
                ParentDebugUI->ClearPendingNullStateKey(CleanKey);
                ParentDebugUI->TriggerRefreshUI();
            }
        }
        else if (ParentDebugUI)
        {
            RemoveJsonObjectKey(CleanKey, ParentDebugUI->ModifiedStateObject);
            ParentDebugUI->StagePendingNullStateKey(CleanKey);
        }
        return;
    }

    if (ParentDebugUI)
    {
        ParentDebugUI->ClearPendingNullStateKey(CleanKey);
    }

    if (ArrayIndex >= 0)
    {
        if (DataType == EStateKeyDataType::StringArray)
        {
            TArray<FString> WorkingArray;
            bool Found = false;
            if (StateManager)
                StateManager->GetCurrentStateValue<TArray<FString>>(CleanKey, WorkingArray, Found);

            if (!Found)
                WorkingArray = StateKeyInfo.DefaultStringArray;

            if (WorkingArray.Num() <= ArrayIndex)
                WorkingArray.SetNum(ArrayIndex + 1);

            WorkingArray[ArrayIndex] = SelectedItem;

            if (InstantBroadcastChange)
                Json = CreateStateChangeJsonStr<TArray<FString>>(CleanKey, WorkingArray);
            else
                UpdateJsonObjectKey<TArray<FString>>(CleanKey, WorkingArray, ParentDebugUI->ModifiedStateObject);
        }
        else if (DataType == EStateKeyDataType::NumberArray)
        {
            TArray<double> WorkingArray;
            bool Found = false;
            if (StateManager)
                StateManager->GetCurrentStateValue<TArray<double>>(CleanKey, WorkingArray, Found);

            if (!Found)
                WorkingArray = StateKeyInfo.DefaultNumberArray;

            if (WorkingArray.Num() <= ArrayIndex)
                WorkingArray.SetNum(ArrayIndex + 1);

            WorkingArray[ArrayIndex] = ClampNumberValueForSchema(StateKeyInfo, FCString::Atod(*SelectedItem));

            if (InstantBroadcastChange)
                Json = CreateStateChangeJsonStr<TArray<double>>(CleanKey, WorkingArray);
            else
                UpdateJsonObjectKey<TArray<double>>(CleanKey, WorkingArray, ParentDebugUI->ModifiedStateObject);
        }

        if (InstantBroadcastChange)
        {
            if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
            {
                Delegates->OnRecieveData.Broadcast(Json);
            }
        }
    }
    else
    {
        if (InstantBroadcastChange)
        {
            if (DataType == EStateKeyDataType::String)
                Json = CreateStateChangeJsonStr<FString>(CleanKey, SelectedItem);
            else
                Json = CreateStateChangeJsonStr<double>(CleanKey, ClampNumberValueForSchema(StateKeyInfo, FCString::Atod(*SelectedItem)));

            if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
            {
                Delegates->OnRecieveData.Broadcast(Json);
            }
        }
        else
        {
            if (DataType == EStateKeyDataType::String)
            {
                UpdateJsonObjectKey<FString>(CleanKey, SelectedItem, ParentDebugUI->ModifiedStateObject);
            }
            else
            {
                UpdateJsonObjectKey<double>(CleanKey, ClampNumberValueForSchema(StateKeyInfo, FCString::Atod(*SelectedItem)), ParentDebugUI->ModifiedStateObject);
            }
        }
    }
}

void UStateKeyInputComboBox::TriggerResend()
{
	FString SelectedItem = GetSelectedOption();

	OnComboBoxChanged(SelectedItem, ESelectInfo::Direct);
}

UWidget* UStateKeyInputComboBox::GenerateComboOptionWidget(FString Item)
{
	UTextBlock* ItemText = NewObject<UTextBlock>(this);
	if (!ItemText)
	{
		return nullptr;
	}

	ItemText->SetText(FText::FromString(Item));
	FSlateFontInfo FontInfo = ItemText->GetFont();
	FontInfo.Size = 16;
	ItemText->SetFont(FontInfo);
	ItemText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));

	// When labels show a truncated description, tooltip the full description text.
	if (bDisplayDescriptionAsOptions)
	{
		const FString TrueValue = GetTrueValueForDisplayLabel(Item).TrimStartAndEnd();
		if (!TrueValue.IsEmpty())
		{
			const FString* FullDescription = OptionDescriptions.Find(TrueValue);
			const FString TooltipText = FullDescription ? FullDescription->TrimStartAndEnd() : TrueValue;
			if (!TooltipText.IsEmpty() && !TooltipText.Equals(Item, ESearchCase::CaseSensitive))
			{
				if (UWidget* TooltipWidget = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::CreateStyledTooltipWidget(ItemText, TooltipText))
				{
					ItemText->SetToolTip(TooltipWidget);
				}
			}
		}
		return ItemText;
	}

	for (const TPair<FString, FString>& Pair : OptionDescriptions)
	{
		if (!Pair.Key.Equals(Item, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString Description = Pair.Value.TrimStartAndEnd();
		if (Description.IsEmpty())
		{
			break;
		}

		if (UWidget* TooltipWidget = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::CreateStyledTooltipWidget(ItemText, Description))
		{
			ItemText->SetToolTip(TooltipWidget);
		}
		break;
	}

	return ItemText;
}

void UStateKeyInputComboBox::RefreshSelectedOptionTooltip()
{
	const FString SelectedItem = GetSelectedOption();
	if (SelectedItem.IsEmpty())
	{
		SetToolTip(nullptr);
		return;
	}

	if (bDisplayDescriptionAsOptions)
	{
		const FString TrueValue = GetTrueValueForDisplayLabel(SelectedItem).TrimStartAndEnd();
		if (!TrueValue.IsEmpty())
		{
			const FString* FullDescription = OptionDescriptions.Find(TrueValue);
			const FString TooltipText = FullDescription ? FullDescription->TrimStartAndEnd() : TrueValue;
			if (!TooltipText.IsEmpty() && !TooltipText.Equals(SelectedItem, ESearchCase::CaseSensitive))
			{
				if (UWidget* TooltipWidget = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::CreateStyledTooltipWidget(this, TooltipText))
				{
					SetToolTip(TooltipWidget);
					return;
				}
			}
		}
		SetToolTip(nullptr);
		return;
	}

	for (const TPair<FString, FString>& Pair : OptionDescriptions)
	{
		if (!Pair.Key.Equals(SelectedItem, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString Description = Pair.Value.TrimStartAndEnd();
		if (Description.IsEmpty())
		{
			break;
		}

		if (UWidget* TooltipWidget = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::CreateStyledTooltipWidget(this, Description))
		{
			SetToolTip(TooltipWidget);
			return;
		}
		break;
	}

	SetToolTip(nullptr);
}

FString UStateKeyInputComboBox::GetDisplayLabelForValue(const FString& TrueValue) const
{
	if (const FString* Label = ValueToDisplayLabel.Find(TrueValue))
	{
		return *Label;
	}
	return TrueValue;
}

FString UStateKeyInputComboBox::GetTrueValueForDisplayLabel(const FString& InDisplayLabel) const
{
	if (const FString* Value = DisplayLabelToValue.Find(InDisplayLabel))
	{
		return *Value;
	}
	return InDisplayLabel;
}

void UStateKeyInputComboBox::AddValueOptionWithLabel(const FString& TrueValue)
{
	FString Label = TrueValue;
	if (bDisplayDescriptionAsOptions)
	{
		if (const FString* Description = OptionDescriptions.Find(TrueValue))
		{
			const FString TrimmedDescription = Description->TrimStartAndEnd();
			if (!TrimmedDescription.IsEmpty())
			{
				Label = TruncateDescriptionForDropdownDisplay(TrimmedDescription);
				// Disambiguate descriptions shared by multiple values so the
				// reverse mapping stays unique and the dropdown has no duplicate rows.
				if (const FString* ExistingValue = DisplayLabelToValue.Find(Label))
				{
					if (!ExistingValue->Equals(TrueValue, ESearchCase::CaseSensitive))
					{
						Label = FString::Printf(TEXT("%s (%s)"), *Label, *TrueValue);
					}
				}
			}
		}
	}

	DisplayLabelToValue.Add(Label, TrueValue);
	ValueToDisplayLabel.Add(TrueValue, Label);
	AddOption(Label);
}

void UStateKeyInputComboBox::SelectByTrueValue(const FString& TrueValue)
{
	SetSelectedOption(GetDisplayLabelForValue(TrueValue));
}

FString UStateKeyInputComboBox::GetSelectedTrueValue() const
{
	return GetTrueValueForDisplayLabel(GetSelectedOption());
}

void UStateKeyInputCheckBox::OnCheckBoxChanged(bool bIsChecked)
{
	if (ParentDebugUI && ParentDebugUI->IsSchemaValueRebuildInProgress())
	{
		return;
	}

    const FString& Key = KeyName;

    if (InstantBroadcastChange)
    {
        FString Json = CreateStateChangeJsonStr<bool>(Key, bIsChecked);

        if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
        {
            Delegates->OnRecieveData.Broadcast(Json);
        }
    }
    else
    {
        UpdateJsonObjectKey<bool>(Key, bIsChecked, ParentDebugUI->ModifiedStateObject);
    }
}

void UStateKeyInputCheckBox::TriggerResend()
{
	bool bCurrentState = IsChecked();

	OnCheckBoxChanged(bCurrentState);
}

void UStateKeyInputConfigArrayCheckBox::OnConfigArrayCheckBoxChanged(bool bIsChecked)
{
	if (ParentDebugUI && ParentDebugUI->IsSchemaValueRebuildInProgress())
	{
		return;
	}

	if (ParentKey.IsEmpty())
	{
		// Fall back to the per-index behaviour when no parent was wired up.
		UStateKeyInputCheckBox::OnCheckBoxChanged(bIsChecked);
		return;
	}

	UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();

	auto TryGetPendingBoolArray = [this](const FString& InKey, TArray<bool>& OutArray) -> bool
	{
		if (!ParentDebugUI || !ParentDebugUI->ModifiedStateObject.IsValid())
		{
			return false;
		}

		TArray<FString> KeyParts;
		InKey.ParseIntoArray(KeyParts, TEXT("."), true);
		if (KeyParts.Num() == 0)
		{
			return false;
		}

		TSharedPtr<FJsonObject> CurrentObject = ParentDebugUI->ModifiedStateObject;
		for (int32 PartIdx = 0; PartIdx < KeyParts.Num() - 1; ++PartIdx)
		{
			if (!CurrentObject.IsValid() || !CurrentObject->HasTypedField<EJson::Object>(KeyParts[PartIdx]))
			{
				return false;
			}
			CurrentObject = CurrentObject->GetObjectField(KeyParts[PartIdx]);
		}

		if (!CurrentObject.IsValid() || !CurrentObject->HasTypedField<EJson::Array>(KeyParts.Last()))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>& JsonArray = CurrentObject->GetArrayField(KeyParts.Last());
		OutArray.Reset(JsonArray.Num());
		for (const TSharedPtr<FJsonValue>& JsonValue : JsonArray)
		{
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::Boolean)
			{
				return false;
			}
			OutArray.Add(JsonValue->AsBool());
		}

		return true;
	};

	TArray<bool> WorkingArray;
	bool bFound = TryGetPendingBoolArray(ParentKey, WorkingArray);
	if (!bFound && StateManager)
	{
		StateManager->GetCurrentStateValue<TArray<bool>>(ParentKey, WorkingArray, bFound);
	}

	const int32 TargetLength = ExpectedArrayLength > 0 ? ExpectedArrayLength : DefaultArrayTemplate.Num();
	if (TargetLength > 0)
	{
		if (WorkingArray.Num() < TargetLength)
		{
			const int32 PreviousNum = WorkingArray.Num();
			WorkingArray.SetNum(TargetLength);
			for (int32 FillIdx = PreviousNum; FillIdx < TargetLength; ++FillIdx)
			{
				WorkingArray[FillIdx] = DefaultArrayTemplate.IsValidIndex(FillIdx) ? DefaultArrayTemplate[FillIdx] : false;
			}
		}
		else if (WorkingArray.Num() > TargetLength)
		{
			WorkingArray.SetNum(TargetLength);
		}
	}

	if (WorkingArray.Num() <= Index)
	{
		WorkingArray.SetNum(Index + 1);
	}
	WorkingArray[Index] = bIsChecked;

	if (InstantBroadcastChange)
	{
		const FString Json = CreateStateChangeJsonStr<TArray<bool>>(ParentKey, WorkingArray);
		if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
		{
			Delegates->OnRecieveData.Broadcast(Json);
		}
	}
	else if (ParentDebugUI)
	{
		UpdateJsonObjectKey<TArray<bool>>(ParentKey, WorkingArray, ParentDebugUI->ModifiedStateObject);
	}
}

void UStateKeyInputConfigArrayCheckBox::TriggerResendConfigArray()
{
	OnConfigArrayCheckBoxChanged(IsChecked());
}

TSharedRef<SWidget> UStateKeyInputAcceptedValuesTextBox::RebuildWidget()
{
	TSharedRef<SWidget> InnerWidget = Super::RebuildWidget();
	TWeakObjectPtr<UStateKeyInputAcceptedValuesTextBox> WeakSelf(this);

	SuggestionList = SNew(SListView<TSharedPtr<FAcceptedValueSuggestion>>)
		.ListItemsSource(&SuggestionItems)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow_Lambda([WeakSelf](TSharedPtr<FAcceptedValueSuggestion> Item, const TSharedRef<STableViewBase>& OwnerTable) -> TSharedRef<ITableRow>
		{
			if (UStateKeyInputAcceptedValuesTextBox* Strong = WeakSelf.Get())
			{
				return Strong->GenerateSuggestionRow(Item, OwnerTable);
			}
			return SNew(STableRow<TSharedPtr<FAcceptedValueSuggestion>>, OwnerTable);
		})
		.OnSelectionChanged_Lambda([WeakSelf](TSharedPtr<FAcceptedValueSuggestion> Item, ESelectInfo::Type SelectInfo)
		{
			if (UStateKeyInputAcceptedValuesTextBox* Strong = WeakSelf.Get())
			{
				Strong->HandleSuggestionSelected(Item, SelectInfo);
			}
		});

	TSharedRef<SWidget> MenuContentWidget = CreateStandardSuggestionMenuContent(SuggestionList.ToSharedRef());

	TSharedRef<SWidget> ClickDetectingInput = SNew(SClickDetector)
		.OnClicked_Lambda([WeakSelf]()
		{
			if (UStateKeyInputAcceptedValuesTextBox* Strong = WeakSelf.Get())
			{
				Strong->HandleInputFocused();
			}
		})
		[
			InnerWidget
		];

	SuggestionAnchor = SNew(SMenuAnchor)
		.Placement(MenuPlacement_BelowAnchor)
		.UseApplicationMenuStack(false)
		.MenuContent(MenuContentWidget)
		[
			ClickDetectingInput
		];

	return SuggestionAnchor.ToSharedRef();
}

void UStateKeyInputAcceptedValuesTextBox::ReleaseSlateResources(bool bReleaseChildren)
{
	if (SuggestionAnchor.IsValid())
	{
		SuggestionAnchor->SetIsOpen(false);
	}
	SuggestionAnchor.Reset();
	SuggestionList.Reset();
	SuggestionItems.Reset();
	Super::ReleaseSlateResources(bReleaseChildren);
}

void UStateKeyInputAcceptedValuesTextBox::HandleAcceptedValuesTextChanged(const FText& InText)
{
	if (bSuppressTextChangedHandler)
	{
		return;
	}
	RefreshSuggestions();
}

void UStateKeyInputAcceptedValuesTextBox::BuildLabelForValue(const FString& TrueValue)
{
	if (ValueToDisplayLabel.Contains(TrueValue))
	{
		return;
	}

	FString Label = TrueValue;
	if (bDisplayDescriptionAsOptions)
	{
		if (const FString* Description = OptionDescriptions.Find(TrueValue))
		{
			const FString TrimmedDescription = Description->TrimStartAndEnd();
			if (!TrimmedDescription.IsEmpty())
			{
				Label = TruncateDescriptionForDropdownDisplay(TrimmedDescription);
				if (const FString* ExistingValue = DisplayLabelToValue.Find(Label))
				{
					if (!ExistingValue->Equals(TrueValue, ESearchCase::CaseSensitive))
					{
						Label = FString::Printf(TEXT("%s (%s)"), *Label, *TrueValue);
					}
				}
			}
		}
	}

	DisplayLabelToValue.Add(Label, TrueValue);
	ValueToDisplayLabel.Add(TrueValue, Label);
}

void UStateKeyInputAcceptedValuesTextBox::BuildDisplayLabels()
{
	DisplayLabelToValue.Reset();
	ValueToDisplayLabel.Reset();

	if (!bDisplayDescriptionAsOptions)
	{
		return;
	}

	for (const FString& Accepted : AcceptedValues)
	{
		const FString Trimmed = Accepted.TrimStartAndEnd();
		if (!Trimmed.IsEmpty())
		{
			BuildLabelForValue(Trimmed);
		}
	}

	for (const TPair<FString, FString>& Pair : OptionDescriptions)
	{
		BuildLabelForValue(Pair.Key);
	}
}

FString UStateKeyInputAcceptedValuesTextBox::GetDisplayLabelForValue(const FString& TrueValue) const
{
	if (const FString* Label = ValueToDisplayLabel.Find(TrueValue))
	{
		return *Label;
	}
	return TrueValue;
}

FString UStateKeyInputAcceptedValuesTextBox::GetTrueValueForDisplayLabel(const FString& InDisplayLabel) const
{
	if (const FString* Value = DisplayLabelToValue.Find(InDisplayLabel))
	{
		return *Value;
	}
	return InDisplayLabel;
}

void UStateKeyInputAcceptedValuesTextBox::HandleInputFocused()
{
	RefreshSuggestions(/*bShowAllOptions=*/true);
}

void UStateKeyInputAcceptedValuesTextBox::RefreshSuggestions(bool bShowAllOptions)
{
	SuggestionItems.Reset();
	if (!SuggestionAnchor.IsValid() || !SuggestionList.IsValid())
	{
		return;
	}

	const FString TokenLower = GetText().ToString().TrimStartAndEnd().ToLower();
	const bool bListAllOptions = bShowAllOptions || TokenLower.IsEmpty();

	TSet<FString> SeenUpper;
	for (const FString& Accepted : AcceptedValues)
	{
		const FString Trimmed = Accepted.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			continue;
		}

		const FString Description = OptionDescriptions.Contains(Trimmed)
			? OptionDescriptions[Trimmed].TrimStartAndEnd()
			: FString();

		if (!bListAllOptions)
		{
			const bool bMatchDescription = bDisplayDescriptionAsOptions && !Description.IsEmpty();
			const bool bMatches = Trimmed.ToLower().Contains(TokenLower)
				|| (bMatchDescription && Description.ToLower().Contains(TokenLower));
			if (!bMatches)
			{
				continue;
			}
		}

		const FString KeyUpper = Trimmed.ToUpper();
		if (SeenUpper.Contains(KeyUpper))
		{
			continue;
		}
		SeenUpper.Add(KeyUpper);

		TSharedPtr<FAcceptedValueSuggestion> Suggestion = MakeShared<FAcceptedValueSuggestion>();
		Suggestion->Value = Trimmed;
		Suggestion->Description = Description;
		Suggestion->DisplayLabel = GetDisplayLabelForValue(Trimmed);
		SuggestionItems.Add(Suggestion);
	}

	SuggestionItems.Sort([](const TSharedPtr<FAcceptedValueSuggestion>& A, const TSharedPtr<FAcceptedValueSuggestion>& B)
	{
		if (!A.IsValid() || !B.IsValid())
		{
			return false;
		}
		return A->DisplayLabel < B->DisplayLabel;
	});

	SuggestionList->RequestListRefresh();
	SuggestionAnchor->SetIsOpen(SuggestionItems.Num() > 0, false);
}

TSharedRef<ITableRow> UStateKeyInputAcceptedValuesTextBox::GenerateSuggestionRow(
	TSharedPtr<FAcceptedValueSuggestion> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString ValueText = Item.IsValid() ? Item->Value : FString();
	const FString DescriptionText = Item.IsValid() ? Item->Description.TrimStartAndEnd() : FString();

	if (bDisplayDescriptionAsOptions && !DescriptionText.IsEmpty())
	{
		const FString LabelText = Item.IsValid() && !Item->DisplayLabel.IsEmpty() ? Item->DisplayLabel : DescriptionText;
		return SNew(STableRow<TSharedPtr<FAcceptedValueSuggestion>>, OwnerTable)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor::White)
				.ToolTip(ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::CreateStyledTooltip(DescriptionText))
				.Padding(FMargin(4.0f, 3.0f))
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.Padding(6, 3, 4, 3)
						[
							SNew(STextBlock)
								.Text(FText::FromString(LabelText))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
								.ColorAndOpacity(FSlateColor(FLinearColor::Black))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Right)
						.Padding(8, 3, 6, 3)
						[
							SNew(STextBlock)
								.Text(FText::FromString(ValueText))
								.Font(FCoreStyle::GetDefaultFontStyle("Italic", 16))
								.ColorAndOpacity(FSlateColor(FLinearColor::Black))
						]
				]
		];
	}

	return SNew(STableRow<TSharedPtr<FAcceptedValueSuggestion>>, OwnerTable)
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor::White)
			.ToolTip(ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::CreateStyledTooltip(DescriptionText))
			.Padding(FMargin(6.0f, 3.0f))
			[
				SNew(STextBlock)
					.Text(FText::FromString(ValueText))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
					.ColorAndOpacity(FSlateColor(FLinearColor::Black))
			]
	];
}

void UStateKeyInputAcceptedValuesTextBox::HandleSuggestionSelected(
	TSharedPtr<FAcceptedValueSuggestion> Item,
	ESelectInfo::Type SelectInfo)
{
	if (!Item.IsValid())
	{
		return;
	}
	if (SelectInfo != ESelectInfo::OnMouseClick && SelectInfo != ESelectInfo::OnKeyPress)
	{
		return;
	}

	const FString TextToDisplay = (bDisplayDescriptionAsOptions && !Item->DisplayLabel.IsEmpty())
		? Item->DisplayLabel
		: Item->Value;

	{
		TGuardValue<bool> Guard(bSuppressTextChangedHandler, true);
		SetText(FText::FromString(TextToDisplay));
	}
	if (SuggestionAnchor.IsValid())
	{
		SuggestionAnchor->SetIsOpen(false);
	}
	if (MyEditableTextBlock.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(MyEditableTextBlock, EFocusCause::SetDirectly);
	}
	if (SuggestionList.IsValid())
	{
		SuggestionList->ClearSelection();
	}
}

void UStateKeyInputAcceptedValuesTextBox::OnTextValueCommitted(const FText& ComittedText, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnUserMovedFocus && SuggestionAnchor.IsValid() && SuggestionAnchor->IsOpen())
	{
		bool bFocusInsideSuggestionPopup = false;
		if (SuggestionList.IsValid() && FSlateApplication::IsInitialized())
		{
			TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
			for (const SWidget* CurrentWidget = FocusedWidget.Get(); CurrentWidget != nullptr; CurrentWidget = CurrentWidget->GetParentWidget().Get())
			{
				if (CurrentWidget == SuggestionList.Get())
				{
					bFocusInsideSuggestionPopup = true;
					break;
				}
			}
		}

		if (bFocusInsideSuggestionPopup)
		{
			return;
		}
	}

	if (SuggestionAnchor.IsValid())
	{
		SuggestionAnchor->SetIsOpen(false);
	}

	FText EffectiveText = ComittedText;
	if (bDisplayDescriptionAsOptions)
	{
		EffectiveText = FText::FromString(GetTrueValueForDisplayLabel(ComittedText.ToString()));
	}

	UStateKeyInputTextBox::OnTextValueCommitted(EffectiveText, CommitMethod);
}

namespace ZLDebugUIConfigAutofill
{
static bool IsZLVEPluginAvailable()
{
	static TOptional<bool> CachedResult;
	if (!CachedResult.IsSet())
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ZLVE"));
		CachedResult = Plugin.IsValid() && Plugin->IsEnabled();
	}
	return CachedResult.GetValue();
}

static bool IsValidConfigurationKeyForAutofill(UStateKeyInfoAsset* Schema, const FString& KeyName)
{
	if (!IsValid(Schema) || KeyName != TEXT("configuration"))
	{
		return false;
	}

	if (const FStateKeyInfo* Info = Schema->KeyInfos.Find(KeyName))
	{
		return Info->GetDataTypeEnum() == EStateKeyDataType::String;
	}
	return false;
}

bool ShouldUseConfigurationAutofill(UStateKeyInfoAsset* Schema, const FString& KeyName)
{
	return IsZLVEPluginAvailable() && IsValidConfigurationKeyForAutofill(Schema, KeyName);
}
}

TSharedRef<SWidget> UStateKeyInputConfigurationTextBox::RebuildWidget()
{
	TSharedRef<SWidget> InnerWidget = Super::RebuildWidget();

	TWeakObjectPtr<UStateKeyInputConfigurationTextBox> WeakSelf(this);

	SuggestionList = SNew(SListView<TSharedPtr<FConfigurationAutofillSuggestion>>)
		.ListItemsSource(&SuggestionItems)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow_Lambda([WeakSelf](TSharedPtr<FConfigurationAutofillSuggestion> Item, const TSharedRef<STableViewBase>& OwnerTable) -> TSharedRef<ITableRow>
		{
			if (UStateKeyInputConfigurationTextBox* Strong = WeakSelf.Get())
			{
				return Strong->GenerateSuggestionRow(Item, OwnerTable);
			}
			return SNew(STableRow<TSharedPtr<FConfigurationAutofillSuggestion>>, OwnerTable);
		})
		.OnSelectionChanged_Lambda([WeakSelf](TSharedPtr<FConfigurationAutofillSuggestion> Item, ESelectInfo::Type SelectInfo)
		{
			if (UStateKeyInputConfigurationTextBox* Strong = WeakSelf.Get())
			{
				Strong->HandleSuggestionSelected(Item, SelectInfo);
			}
		});

	TSharedRef<SWidget> MenuContentWidget = CreateStandardSuggestionMenuContent(SuggestionList.ToSharedRef());

	SuggestionAnchor = SNew(SMenuAnchor)
		.Placement(MenuPlacement_BelowAnchor)
		.UseApplicationMenuStack(false)
		.MenuContent(MenuContentWidget)
		[
			InnerWidget
		];

	return SuggestionAnchor.ToSharedRef();
}

void UStateKeyInputConfigurationTextBox::ReleaseSlateResources(bool bReleaseChildren)
{
	if (SuggestionAnchor.IsValid())
	{
		SuggestionAnchor->SetIsOpen(false);
	}
	SuggestionAnchor.Reset();
	SuggestionList.Reset();
	SuggestionItems.Reset();
	Super::ReleaseSlateResources(bReleaseChildren);
}

void UStateKeyInputConfigurationTextBox::HandleAutofillTextChanged(const FText& InText)
{
	if (bSuppressTextChangedHandler)
	{
		return;
	}

	RefreshSuggestions();
}

void UStateKeyInputConfigurationTextBox::OnTextValueCommitted(const FText& ComittedText, ETextCommit::Type CommitMethod)
{
	// While the suggestion popup is open, clicking suggestion rows/scrollbar can move focus
	// away from the text box. Ignore that transient focus commit only when the new focus
	// is still inside the suggestion popup itself.
	if (CommitMethod == ETextCommit::OnUserMovedFocus && SuggestionAnchor.IsValid() && SuggestionAnchor->IsOpen())
	{
		bool bFocusInsideSuggestionPopup = false;
		if (SuggestionList.IsValid() && FSlateApplication::IsInitialized())
		{
			TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
			for (const SWidget* CurrentWidget = FocusedWidget.Get(); CurrentWidget != nullptr; CurrentWidget = CurrentWidget->GetParentWidget().Get())
			{
				if (CurrentWidget == SuggestionList.Get())
				{
					bFocusInsideSuggestionPopup = true;
					break;
				}
			}
		}

		if (bFocusInsideSuggestionPopup)
		{
			return;
		}
	}

	// Any real commit path (enter, focus moved elsewhere) should collapse the popup.
	if (SuggestionAnchor.IsValid())
	{
		SuggestionAnchor->SetIsOpen(false);
	}

	UStateKeyInputTextBox::OnTextValueCommitted(ComittedText, CommitMethod);
}

void UStateKeyInputConfigurationTextBox::GetTokenContext(const FString& CurrentText, FString& OutBeforeToken, FString& OutToken) const
{
	int32 LastCommaIdx = INDEX_NONE;
	CurrentText.FindLastChar(TEXT(','), LastCommaIdx);
	if (LastCommaIdx == INDEX_NONE)
	{
		OutBeforeToken = FString();
		OutToken = CurrentText.TrimStartAndEnd();
	}
	else
	{
		OutBeforeToken = CurrentText.Left(LastCommaIdx + 1);
		OutToken = CurrentText.RightChop(LastCommaIdx + 1).TrimStartAndEnd();
	}
}

void UStateKeyInputConfigurationTextBox::RefreshSuggestions()
{
	SuggestionItems.Reset();

	if (!SuggestionAnchor.IsValid() || !SuggestionList.IsValid())
	{
		return;
	}

	UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
	UStateKeyInfoAsset* Schema = StateManager ? StateManager->GetCurrentSchemaAsset() : nullptr;
	if (!IsValid(Schema))
	{
		SuggestionAnchor->SetIsOpen(false);
		return;
	}

	const FString CurrentText = GetText().ToString();
	FString BeforeToken;
	FString Token;
	GetTokenContext(CurrentText, BeforeToken, Token);

	// Do not generate the full suggestion corpus until the user has started typing
	// the current token. This avoids expensive scans in large schemas.
	if (Token.TrimStartAndEnd().IsEmpty())
	{
		SuggestionAnchor->SetIsOpen(false);
		return;
	}

	const FString TokenLower = Token.ToLower();

	if (Schema->DimeModelData.Num() == 0)
	{
		SuggestionAnchor->SetIsOpen(false);
		return;
	}

	TSet<FString> SeenSuggestionsUpper;
	for (const FDIMEModelMetadata& ModelMetadata : Schema->DimeModelData)
	{
		const FString ModelName = ModelMetadata.ModelName.TrimStartAndEnd();
		if (ModelName.IsEmpty())
		{
			continue;
		}

		for (const FDIMEModelCodeMetadata& CodeMetadata : ModelMetadata.Codes)
		{
			const FString Code = CodeMetadata.Code.TrimStartAndEnd();
			const FString GroupName = CodeMetadata.Group.TrimStartAndEnd();
			if (Code.IsEmpty() || Code.Equals(TEXT("RESET"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			const bool bGroupMatchesToken = GroupName.IsEmpty() ? false : GroupName.ToLower().Contains(TokenLower);
			const bool bModelMatchesToken = ModelName.ToLower().Contains(TokenLower);
			const bool bCodeMatchesToken = Code.ToLower().Contains(TokenLower);
			if (!bCodeMatchesToken && !bGroupMatchesToken && !bModelMatchesToken)
			{
				continue;
			}

			const FString SuggestionKeyUpper = (ModelName + TEXT("|") + GroupName + TEXT("|") + Code).ToUpper();
			if (SeenSuggestionsUpper.Contains(SuggestionKeyUpper))
			{
				continue;
			}

			SeenSuggestionsUpper.Add(SuggestionKeyUpper);

			TSharedPtr<FConfigurationAutofillSuggestion> Suggestion = MakeShared<FConfigurationAutofillSuggestion>();
			Suggestion->Code = Code;
			Suggestion->Model = ModelName;
			Suggestion->Group = GroupName;
			Suggestion->Description = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::ResolveCodeDescription(Schema, ModelName, GroupName, Code);
			SuggestionItems.Add(Suggestion);
		}
	}

	if (SuggestionList.IsValid())
	{
		SuggestionList->RequestListRefresh();
	}

	if (SuggestionItems.Num() == 0)
	{
		SuggestionAnchor->SetIsOpen(false);
	}
	else
	{
		SuggestionAnchor->SetIsOpen(true, /*bFocusMenu=*/false);
	}
}

TSharedRef<ITableRow> UStateKeyInputConfigurationTextBox::GenerateSuggestionRow(
	TSharedPtr<FConfigurationAutofillSuggestion> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString CodeText = Item.IsValid() ? Item->Code : FString();
	const FString GroupPathText = Item.IsValid()
		? (Item->Group.TrimStartAndEnd().IsEmpty()
			? Item->Model
			: FString::Printf(TEXT("%s/%s"), *Item->Model, *Item->Group))
		: FString();
	const FString DescriptionText = Item.IsValid() ? Item->Description.TrimStartAndEnd() : FString();

	FSlateFontInfo CodeFont = FCoreStyle::GetDefaultFontStyle("Regular", 16);
	FSlateFontInfo ItalicFont = FCoreStyle::GetDefaultFontStyle("Italic", 16);
	return SNew(STableRow<TSharedPtr<FConfigurationAutofillSuggestion>>, OwnerTable)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor::White)
				.ToolTip(ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::CreateStyledTooltip(DescriptionText))
				.Padding(FMargin(4.0f, 3.0f))
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(6, 3, 4, 3)
						[
							SNew(STextBlock)
								.Text(FText::FromString(CodeText))
								.Font(CodeFont)
								.ColorAndOpacity(FSlateColor(FLinearColor::Black))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Right)
						.Padding(8, 3, 6, 3)
						[
							SNew(STextBlock)
								.Text(FText::FromString(GroupPathText))
								.Font(ItalicFont)
								.ColorAndOpacity(FSlateColor(FLinearColor::Black))
						]
				]
		];
}

void UStateKeyInputConfigurationTextBox::HandleSuggestionSelected(
	TSharedPtr<FConfigurationAutofillSuggestion> Item,
	ESelectInfo::Type SelectInfo)
{
	if (!Item.IsValid())
	{
		return;
	}
	// Only react to explicit user clicks/keyboard selections, not programmatic clears.
	if (SelectInfo != ESelectInfo::OnMouseClick && SelectInfo != ESelectInfo::OnKeyPress)
	{
		return;
	}

	const FString CurrentText = GetText().ToString();
	FString BeforeToken;
	FString Token;
	GetTokenContext(CurrentText, BeforeToken, Token);

	FString Rebuilt = BeforeToken;
	if (!Rebuilt.IsEmpty() && !Rebuilt.EndsWith(TEXT(",")))
	{
		Rebuilt += TEXT(",");
	}
	Rebuilt += Item->Code;

	{
		TGuardValue<bool> Guard(bSuppressTextChangedHandler, true);
		SetText(FText::FromString(Rebuilt));
	}

	if (SuggestionAnchor.IsValid())
	{
		SuggestionAnchor->SetIsOpen(false);
	}

	// Keep keyboard focus on the underlying text box so the user can keep editing.
	if (MyEditableTextBlock.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(MyEditableTextBlock, EFocusCause::SetDirectly);
	}

	if (SuggestionList.IsValid())
	{
		SuggestionList->ClearSelection();
	}
}

void UStateKeyInputTextBox::OnTextValueCommitted(const FText& ComittedText, ETextCommit::Type CommitMethod)
{
	if (ParentDebugUI && ParentDebugUI->IsSchemaValueRebuildInProgress())
	{
		return;
	}

    FString Json;

    FString CleanKey = KeyName;
    int32 ArrayIndex = -1;
    const FString IndexToken = TEXT("_INDEX_");

    UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();

    if (KeyName.Contains(IndexToken))
    {
        int32 IndexStart = KeyName.Find(IndexToken, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
        if (IndexStart != INDEX_NONE)
        {
            FString IndexStr = KeyName.Mid(IndexStart + IndexToken.Len());
            ArrayIndex = FCString::Atoi(*IndexStr);
            CleanKey = KeyName.Left(IndexStart);
        }
    }

    const FString CommittedString = ComittedText.ToString();
    const bool bTreatAsNullSelection = SupportsNullableDebugInput(StateKeyInfo, DataType) && IsNullTokenInput(CommittedString);
    if (bTreatAsNullSelection)
    {
        if (InstantBroadcastChange)
        {
            if (StateManager)
            {
                StateManager->RemoveCurrentStateValue(CleanKey);
            }
            if (ParentDebugUI)
            {
                ParentDebugUI->ClearPendingNullStateKey(CleanKey);
                ParentDebugUI->TriggerRefreshUI();
            }
        }
        else if (ParentDebugUI)
        {
            RemoveJsonObjectKey(CleanKey, ParentDebugUI->ModifiedStateObject);
            ParentDebugUI->StagePendingNullStateKey(CleanKey);
        }
        return;
    }

    if (ParentDebugUI)
    {
        ParentDebugUI->ClearPendingNullStateKey(CleanKey);
    }

    if (ArrayIndex >= 0)
    {
        if (DataType == EStateKeyDataType::StringArray)
        {
            TArray<FString> WorkingArray;
            bool Found = false;
            if (StateManager)
                StateManager->GetCurrentStateValue<TArray<FString>>(CleanKey, WorkingArray, Found);

            if (WorkingArray.Num() <= ArrayIndex)
                WorkingArray.SetNum(ArrayIndex + 1);

            WorkingArray[ArrayIndex] = CommittedString;

            if (InstantBroadcastChange)
                Json = CreateStateChangeJsonStr<TArray<FString>>(CleanKey, WorkingArray);
            else
                UpdateJsonObjectKey<TArray<FString>>(CleanKey, WorkingArray, ParentDebugUI->ModifiedStateObject);
        }
        else if (DataType == EStateKeyDataType::NumberArray)
        {
            TArray<double> WorkingArray;
            bool Found = false;
            if (StateManager)
                StateManager->GetCurrentStateValue<TArray<double>>(CleanKey, WorkingArray, Found);

            if (WorkingArray.Num() <= ArrayIndex)
                WorkingArray.SetNum(ArrayIndex + 1);

            WorkingArray[ArrayIndex] = ClampNumberValueForSchema(StateKeyInfo, FCString::Atod(*CommittedString));

            if (InstantBroadcastChange)
                Json = CreateStateChangeJsonStr<TArray<double>>(CleanKey, WorkingArray);
            else
                UpdateJsonObjectKey<TArray<double>>(CleanKey, WorkingArray, ParentDebugUI->ModifiedStateObject);
        }
        else if (DataType == EStateKeyDataType::BoolArray)
        {
            TArray<bool> WorkingArray;
            bool Found = false;
            if (StateManager)
                StateManager->GetCurrentStateValue<TArray<bool>>(CleanKey, WorkingArray, Found);

            if (WorkingArray.Num() <= ArrayIndex)
                WorkingArray.SetNum(ArrayIndex + 1);

            const FString Lower = CommittedString.ToLower();
            WorkingArray[ArrayIndex] = (Lower == "true" || Lower == "1");

            if (InstantBroadcastChange)
                Json = CreateStateChangeJsonStr<TArray<bool>>(CleanKey, WorkingArray);
            else
                UpdateJsonObjectKey<TArray<bool>>(CleanKey, WorkingArray, ParentDebugUI->ModifiedStateObject);
        }
    }
    else
    {
        if (DataType == EStateKeyDataType::Number)
        {
            double NumberValue = ClampNumberValueForSchema(StateKeyInfo, FCString::Atod(*CommittedString));
            if (InstantBroadcastChange)
                Json = CreateStateChangeJsonStr<double>(CleanKey, NumberValue);
            else
                UpdateJsonObjectKey<double>(CleanKey, NumberValue, ParentDebugUI->ModifiedStateObject);
        }
        else if (DataType == EStateKeyDataType::Bool)
        {
            const FString Lower = CommittedString.ToLower();
            bool BoolValue = (Lower == "true" || Lower == "1");
            if (InstantBroadcastChange)
                Json = CreateStateChangeJsonStr<bool>(CleanKey, BoolValue);
            else
                UpdateJsonObjectKey<bool>(CleanKey, BoolValue, ParentDebugUI->ModifiedStateObject);
        }
        else
        {
            if (InstantBroadcastChange)
                Json = CreateStateChangeJsonStr<FString>(CleanKey, CommittedString);
            else
                UpdateJsonObjectKey<FString>(CleanKey, CommittedString, ParentDebugUI->ModifiedStateObject);
        }
    }

    if (InstantBroadcastChange)
    {
        if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
        {
            Delegates->OnRecieveData.Broadcast(Json);
        }
    }
}

void UStateKeyInputTextBox::TriggerResend()
{
	FText CurrentText = GetText();

	OnTextValueCommitted(CurrentText, ETextCommit::OnEnter);
}

void UZLDebugUIWidget::RebuildDebugUI()
{
	RebuildDebugUIWithNesting();
}

bool UZLDebugUIWidget::IsDebugUIPresented() const
{
	const ESlateVisibility RootVis = GetVisibility();
	return RootVis != ESlateVisibility::Collapsed && RootVis != ESlateVisibility::Hidden;
}

void UZLDebugUIWidget::RebuildDebugUIWithNesting()
{
	if (!IsDebugUIPresented())
	{
		// Match prior behaviour when hidden: clear deferred preset apply without broadcasting state.
		if (bNeedsInitialDefaultPresetApply)
		{
			bNeedsInitialDefaultPresetApply = false;
		}
		return;
	}

	if (!IsValid(TargetSchema) || !SchemaOptionsVBox) return;

	CaptureSchemaNavSelectionToken();

	SyncPresetsCacheFromDisk();
	PresetIncludeBySchemaKey.Empty();
	PresetValueWidgetsBySchemaKey.Empty();

	if (SchemaTitle)
	{
		SchemaTitle->SetText(FText::FromString("Schema: " + TargetSchema->GetName()));
	}

	ModifiedStateObject = MakeShared<FJsonObject>();
	ClearPendingNullStateKeys();

	UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();

	{
		TGuardValue<bool> RebuildSuppressGuard(bSuppressSchemaValueChangeBroadcast, true);

		SchemaOptionsVBox->ClearChildren();
		FoldoutHelpers.Empty();
		ArrayResizeHelpers.Empty();
		ResetSchemaNavOrder();

	FSlateColor BlackColor = FSlateColor(FLinearColor::Black);
	FTableRowStyle RowStyle;
	RowStyle.TextColor = BlackColor;
	RowStyle.SelectedTextColor = BlackColor;

	TArray<FString> SortedKeys;
	TargetSchema->KeyInfos.GenerateKeyArray(SortedKeys);

	SortedKeys.Sort();

	TMap<FString, UVerticalBox*> FoldoutSections;

	for (const FString& Key : SortedKeys)
	{
		if (const FStateKeyInfo* SchemaKey = TargetSchema->KeyInfos.Find(Key))
		{
			const FString& KeyName = Key;
			const FStateKeyInfo& StateKeyInfo = *SchemaKey;
			bool CurrValFound = false;

			TArray<FString> KeyParts;
			KeyName.ParseIntoArray(KeyParts, TEXT("."), true);

			UVerticalBox* ParentBox = SchemaOptionsVBox;
			FString CurrentPath = "";

			// Create foldouts for nested keys
			for (int32 i = 0; i < KeyParts.Num() - 1; ++i)
			{
				CurrentPath += KeyParts[i];
				if (!FoldoutSections.Contains(CurrentPath))
				{
					// Create the button and title
					UButton* ToggleButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
					
					// Style the button
					FButtonStyle ButtonStyle;
					FSlateColorBrush NormalBrush(FLinearColor(0.1f, 0.1f, 0.1f, 0.5f));
					FSlateColorBrush HoveredBrush(FLinearColor(0.2f, 0.2f, 0.2f, 0.5f));
					FSlateColorBrush DarkBrush(FLinearColor(0.05f, 0.05f, 0.05f, 0.5f));
					ButtonStyle.SetNormal(DarkBrush);
					ButtonStyle.SetHovered(HoveredBrush);
					ButtonStyle.SetPressed(NormalBrush);
					ToggleButton->SetStyle(ButtonStyle);

					UHorizontalBox* HeaderBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
					
					UTextBlock* ArrowText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
					ArrowText->SetText(FText::FromString(TEXT(" >																																")));
					FSlateFontInfo ArrowFontInfo = ArrowText->GetFont();
					ArrowFontInfo.Size = 16;
					ArrowText->SetFont(ArrowFontInfo);
					
					
					UTextBlock* SectionTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
					SectionTitle->SetText(FText::FromString(KeyParts[i]));
					FSlateFontInfo FontInfo = SectionTitle->GetFont();
					FontInfo.Size = 16;
					SectionTitle->SetFont(FontInfo);

					HeaderBox->AddChildToHorizontalBox(SectionTitle)->SetPadding(FMargin(0, 0, 0, 0));
					HeaderBox->AddChildToHorizontalBox(ArrowText)->SetPadding(FMargin(0, 0, 0, 0));

					// Add the horizontal box containing the arrow and title to the button
					ToggleButton->AddChild(HeaderBox);

					// The content box that will be toggled
					UVerticalBox* SectionContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
					SectionContent->SetVisibility(ESlateVisibility::Collapsed);

					// Create a helper object to handle the toggle
					UFoldoutHelper* FoldoutHelper = NewObject<UFoldoutHelper>(this);
					FoldoutHelper->SectionContent = SectionContent;
					FoldoutHelper->ArrowText = ArrowText;
					FoldoutHelper->ParentWidget = this;
					FoldoutHelper->FoldoutPath = CurrentPath;
					FoldoutHelpers.Add(FoldoutHelper); // Keep the helper alive

					if (ExpandedFoldouts.Contains(CurrentPath))
					{
						SectionContent->SetVisibility(ESlateVisibility::Visible);
						ArrowText->SetText(FText::FromString(TEXT(" v																																")));
					}

					// Bind the button's OnClicked event
					ToggleButton->OnClicked.AddDynamic(FoldoutHelper, &UFoldoutHelper::ToggleVisibility);

					UBorder* FoldoutRowFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
					FoldoutRowFrame->SetPadding(FMargin(0.f));
					ApplySchemaRowBorderFocusState(FoldoutRowFrame, false);
					FoldoutRowFrame->SetContent(ToggleButton);
					UVerticalBoxSlot* ButtonSlot = ParentBox->AddChildToVerticalBox(FoldoutRowFrame);
					ButtonSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
					ButtonSlot->SetPadding(FMargin(0, 2));
					RegisterSchemaNavFoldout(FoldoutRowFrame, FoldoutHelper);
					ParentBox->AddChildToVerticalBox(SectionContent);

					// Add the new section to our map and set it as the new parent
					FoldoutSections.Add(CurrentPath, SectionContent);
					ParentBox = SectionContent;
				}
				else
				{
					ParentBox = *FoldoutSections.Find(CurrentPath);
				}
				CurrentPath += ".";
			}

			const FString DisplayKeyName = KeyParts.Last();

			const int32 ColLabel = bPresetPickKeysMode ? 1 : 0;
			const int32 ColInput = bPresetPickKeysMode ? 2 : 1;
			const int32 ColResend = bPresetPickKeysMode ? 3 : 2;

			TArray<UWidget*>& RegValueWidgets = PresetValueWidgetsBySchemaKey.FindOrAdd(KeyName);
			RegValueWidgets.Empty();

			UBorder* RowFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			RowFrame->SetPadding(FMargin(0.f));
			ApplySchemaRowBorderFocusState(RowFrame, false);

			UGridPanel* RowGrid = WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass());
			RowFrame->SetContent(RowGrid);

			if (bPresetPickKeysMode)
			{
				RowGrid->SetColumnFill(0, 0.08f);
				RowGrid->SetColumnFill(1, allowResendCurrentValues ? 0.36f : 0.46f);
				RowGrid->SetColumnFill(2, allowResendCurrentValues ? 0.36f : 0.46f);
				if (allowResendCurrentValues)
					RowGrid->SetColumnFill(3, 0.2f);

				UCheckBox* IncludeCb = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
				IncludeCb->SetIsChecked(false);
				IncludeCb->SetRenderTransform(FWidgetTransform(FVector2D(0.f, 0.f), FVector2D(2.0f, 2.0f), FVector2D::ZeroVector, 0.f));
				FCheckBoxStyle IncludeStyle = IncludeCb->GetWidgetStyle();
				IncludeStyle.UncheckedImage.TintColor = FSlateColor(FLinearColor(0.05f, 0.05f, 0.05f));
				IncludeStyle.CheckedImage.TintColor = FSlateColor(FLinearColor(0.05f, 0.05f, 0.05f));
				IncludeStyle.UncheckedHoveredImage.TintColor = FSlateColor(FLinearColor::Gray);
				IncludeStyle.CheckedPressedImage.TintColor = FSlateColor(FLinearColor::White);
#if UNREAL_5_3_OR_NEWER
				IncludeCb->SetWidgetStyle(IncludeStyle);
#endif
				UGridSlot* IncSlot = RowGrid->AddChildToGrid(IncludeCb, 0, 0);
				IncSlot->SetPadding(FMargin(5.f));
				IncSlot->SetHorizontalAlignment(HAlign_Center);
				IncSlot->SetVerticalAlignment(VAlign_Center);
				PresetIncludeBySchemaKey.Add(KeyName, IncludeCb);
				IncludeCb->OnCheckStateChanged.AddDynamic(this, &UZLDebugUIWidget::OnPresetIncludeCheckboxStateChanged);
			}
			else if (allowResendCurrentValues)
			{
				RowGrid->SetColumnFill(0, 0.8f);
				RowGrid->SetColumnFill(1, 0.8f);
				RowGrid->SetColumnFill(2, 0.4f);
			}
			else
			{
				RowGrid->SetColumnFill(0, 1.0f);
				RowGrid->SetColumnFill(1, 1.0f);
			}

			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Label->SetText(FText::FromString(DisplayKeyName));

			FSlateFontInfo FontInfo = Label->GetFont();
			FontInfo.Size = 16;
			Label->SetFont(FontInfo);

			UGridSlot* LabelSlot = RowGrid->AddChildToGrid(Label, 0, ColLabel);
			LabelSlot->SetPadding(FMargin(5.0f, 5.0f, 5.0f, 5.0f));
			LabelSlot->SetHorizontalAlignment(HAlign_Left);
			LabelSlot->SetVerticalAlignment(VAlign_Center);

			UWidget* InputWidget = nullptr;
			EHorizontalAlignment Alignment = HAlign_Fill;

			switch (StateKeyInfo.GetDataTypeEnum())
			{
			case EStateKeyDataType::String:
			{
				FString CurrentVal = StateKeyInfo.DefaultStringValue;
				if (StateManager)
					StateManager->GetCurrentStateValue<FString>(KeyName, CurrentVal, CurrValFound);

				const bool bUseAcceptedValuesAutofillText =
					StateKeyInfo.bLimitValues
					&& !IsCameraFieldKey(KeyName)
					&& StateKeyInfo.AcceptedStringValues.Num() > GAcceptedValuesAutofillThreshold;
				if (StateKeyInfo.bLimitValues && !bUseAcceptedValuesAutofillText)
				{
					UStateKeyInputComboBox* ComboBox = WidgetTree->ConstructWidget<UStateKeyInputComboBox>(UStateKeyInputComboBox::StaticClass());
					ComboBox->KeyName = KeyName;
					ComboBox->ParentDebugUI = this;
					ComboBox->InstantBroadcastChange = instantProcess;
					ComboBox->DataType = EStateKeyDataType::String;
					ComboBox->StateKeyInfo = StateKeyInfo;
					// Resolve descriptions before adding options so labels can be swapped
					// to descriptions when the schema requests it.
					FString ModelName;
					FString GroupName;
					const bool bIsModelGroupKey = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::SplitModelGroupKey(KeyName, ModelName, GroupName);
					if (bIsModelGroupKey && IsValid(TargetSchema))
					{
						TArray<FString> OptionsForDescriptionScan = StateKeyInfo.AcceptedStringValues;
						if (!CurrentVal.IsEmpty() && !OptionsForDescriptionScan.Contains(CurrentVal))
						{
							OptionsForDescriptionScan.Add(CurrentVal);
						}

						for (const FString& Option : OptionsForDescriptionScan)
						{
							const FString Description = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::ResolveCodeDescription(TargetSchema, ModelName, GroupName, Option);
							if (!Description.IsEmpty())
							{
								ComboBox->OptionDescriptions.FindOrAdd(Option) = Description;
							}
						}
					}

					ComboBox->bDisplayDescriptionAsOptions = StateKeyInfo.bDisplayDescriptionAsOptions && ComboBox->OptionDescriptions.Num() > 0;

					for (const FString& Option : StateKeyInfo.AcceptedStringValues)
					{
						ComboBox->AddValueOptionWithLabel(Option);
					}
					if (StateKeyInfo.bAllowNullValue)
					{
						ComboBox->AddOption(GDebugNullToken);
					}

					if (!StateKeyInfo.AcceptedStringValues.Contains(CurrentVal))
					{
						ComboBox->AddValueOptionWithLabel(CurrentVal);
					}

#if UNREAL_5_3_OR_NEWER
					ComboBox->SetItemStyle(RowStyle);
#endif

					if (ComboBox->OptionDescriptions.Num() > 0)
					{
						ComboBox->OnGenerateWidgetEvent.BindDynamic(ComboBox, &UStateKeyInputComboBox::GenerateComboOptionWidget);
					}

					const bool bSelectNullStringOption = StateKeyInfo.bAllowNullValue
						&& !CurrValFound
						&& (StateKeyInfo.bDefaultValueIsNull || CurrentVal.TrimStartAndEnd().IsEmpty());
					ComboBox->SelectByTrueValue(bSelectNullStringOption ? GDebugNullToken : CurrentVal);
					ComboBox->OnSelectionChanged.AddDynamic(ComboBox, &UStateKeyInputComboBox::OnComboBoxChanged);
					InputWidget = ComboBox;
				}
				else
				{
					UStateKeyInputTextBox* TextBox = nullptr;
					if (bUseAcceptedValuesAutofillText)
					{
						UStateKeyInputAcceptedValuesTextBox* AcceptedValuesTextBox = WidgetTree->ConstructWidget<UStateKeyInputAcceptedValuesTextBox>(UStateKeyInputAcceptedValuesTextBox::StaticClass());
						AcceptedValuesTextBox->AcceptedValues = StateKeyInfo.AcceptedStringValues;

						FString ModelName;
						FString GroupName;
						const bool bIsModelGroupKey = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::SplitModelGroupKey(KeyName, ModelName, GroupName);
						if (bIsModelGroupKey && IsValid(TargetSchema))
						{
							TArray<FString> OptionsForDescriptionScan = StateKeyInfo.AcceptedStringValues;
							if (!CurrentVal.IsEmpty() && !OptionsForDescriptionScan.Contains(CurrentVal))
							{
								OptionsForDescriptionScan.Add(CurrentVal);
							}

							for (const FString& Option : OptionsForDescriptionScan)
							{
								const FString Description = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::ResolveCodeDescription(TargetSchema, ModelName, GroupName, Option);
								if (!Description.IsEmpty())
								{
									AcceptedValuesTextBox->OptionDescriptions.FindOrAdd(Option) = Description;
								}
							}
						}

						AcceptedValuesTextBox->bDisplayDescriptionAsOptions = StateKeyInfo.bDisplayDescriptionAsOptions && AcceptedValuesTextBox->OptionDescriptions.Num() > 0;
						AcceptedValuesTextBox->BuildDisplayLabels();
						AcceptedValuesTextBox->OnTextChanged.AddDynamic(AcceptedValuesTextBox, &UStateKeyInputAcceptedValuesTextBox::HandleAcceptedValuesTextChanged);
						TextBox = AcceptedValuesTextBox;
					}
					else if (ZLDebugUIConfigAutofill::ShouldUseConfigurationAutofill(TargetSchema, KeyName))
					{
						UStateKeyInputConfigurationTextBox* ConfigTextBox = WidgetTree->ConstructWidget<UStateKeyInputConfigurationTextBox>(UStateKeyInputConfigurationTextBox::StaticClass());
						ConfigTextBox->OnTextChanged.AddDynamic(ConfigTextBox, &UStateKeyInputConfigurationTextBox::HandleAutofillTextChanged);
						TextBox = ConfigTextBox;
					}
					else
					{
						TextBox = WidgetTree->ConstructWidget<UStateKeyInputTextBox>(UStateKeyInputTextBox::StaticClass());
					}

					if (UStateKeyInputAcceptedValuesTextBox* AcceptedValuesTextBox = Cast<UStateKeyInputAcceptedValuesTextBox>(TextBox))
					{
						TextBox->SetText(FText::FromString(AcceptedValuesTextBox->GetDisplayLabelForValue(CurrentVal)));
					}
					else
					{
						TextBox->SetText(FText::FromString(CurrentVal));
					}
#if UNREAL_5_3_OR_NEWER
					TextBox->WidgetStyle.TextStyle.ColorAndOpacity = BlackColor;
					TextBox->WidgetStyle.TextStyle.SetFontSize(16.0f);
#endif
					TextBox->KeyName = KeyName;
					TextBox->ParentDebugUI = this;
					TextBox->InstantBroadcastChange = instantProcess;
					TextBox->DataType = EStateKeyDataType::String;
					TextBox->StateKeyInfo = StateKeyInfo;
					if (UStateKeyInputConfigurationTextBox* ConfigTextBox = Cast<UStateKeyInputConfigurationTextBox>(TextBox))
					{
						ConfigTextBox->OnTextCommitted.AddDynamic(ConfigTextBox, &UStateKeyInputConfigurationTextBox::OnTextValueCommitted);
					}
					else if (UStateKeyInputAcceptedValuesTextBox* AcceptedValuesTextBox = Cast<UStateKeyInputAcceptedValuesTextBox>(TextBox))
					{
						AcceptedValuesTextBox->OnTextCommitted.AddDynamic(AcceptedValuesTextBox, &UStateKeyInputAcceptedValuesTextBox::OnTextValueCommitted);
					}
					else
					{
						TextBox->OnTextCommitted.AddDynamic(TextBox, &UStateKeyInputTextBox::OnTextValueCommitted);
					}
					InputWidget = TextBox;
				}

				RegisterSchemaNavValue(RowFrame, InputWidget);

				if (bPresetPickKeysMode && InputWidget)
					RegValueWidgets.Add(InputWidget);

				const bool bSkipStringDefault = StateKeyInfo.bAllowNullValue
					&& !CurrValFound
					&& (StateKeyInfo.bDefaultValueIsNull || CurrentVal.TrimStartAndEnd().IsEmpty());
				if (!bSkipStringDefault)
				{
					UpdateJsonObjectKey<FString>(KeyName, CurrentVal, ModifiedStateObject);
				}
				break;
			}
			case EStateKeyDataType::StringArray:
			{
				TArray<FString> CurrentVals;
				if (StateManager)
					StateManager->GetCurrentStateValue<TArray<FString>>(KeyName, CurrentVals, CurrValFound);

				TArray<FString> WorkingArray = CurrValFound ? CurrentVals : StateKeyInfo.DefaultStringArray;

				const int32 NumValues = WorkingArray.Num();
				UVerticalBox* StringArrayRows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
				for (int32 i = 0; i < NumValues; ++i)
				{
					const FString& Value = WorkingArray[i];
					InputWidget = nullptr;

					const bool bUseAcceptedValuesAutofillText =
						StateKeyInfo.bLimitValues
						&& !IsCameraFieldKey(KeyName)
						&& StateKeyInfo.AcceptedStringValues.Num() > GAcceptedValuesAutofillThreshold;
					if (StateKeyInfo.bLimitValues && !bUseAcceptedValuesAutofillText)
					{
						UStateKeyInputComboBox* ComboBox = WidgetTree->ConstructWidget<UStateKeyInputComboBox>(UStateKeyInputComboBox::StaticClass());
						ComboBox->KeyName = KeyName + "_INDEX_" + FString::FromInt(i);
						ComboBox->ParentDebugUI = this;
						ComboBox->InstantBroadcastChange = instantProcess;
						ComboBox->DataType = EStateKeyDataType::StringArray;
						ComboBox->StateKeyInfo = StateKeyInfo;

						// Resolve descriptions before adding options so labels can be
						// swapped to descriptions when the schema requests it.
						FString ModelName;
						FString GroupName;
						const bool bIsModelGroupKey = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::SplitModelGroupKey(KeyName, ModelName, GroupName);
						if (bIsModelGroupKey && IsValid(TargetSchema))
						{
							TArray<FString> OptionsForDescriptionScan = StateKeyInfo.AcceptedStringValues;
							if (!Value.IsEmpty() && !OptionsForDescriptionScan.Contains(Value))
							{
								OptionsForDescriptionScan.Add(Value);
							}

							for (const FString& Option : OptionsForDescriptionScan)
							{
								const FString Description = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::ResolveCodeDescription(
									TargetSchema,
									ModelName,
									GroupName,
									Option);
								if (!Description.IsEmpty())
								{
									ComboBox->OptionDescriptions.FindOrAdd(Option) = Description;
								}
							}
						}

						ComboBox->bDisplayDescriptionAsOptions = StateKeyInfo.bDisplayDescriptionAsOptions && ComboBox->OptionDescriptions.Num() > 0;

						for (const FString& Option : StateKeyInfo.AcceptedStringValues)
						{
							ComboBox->AddValueOptionWithLabel(Option);
						}
						if (StateKeyInfo.bAllowNullValue)
						{
							ComboBox->AddOption(GDebugNullToken);
						}

						if (ComboBox->OptionDescriptions.Num() > 0)
						{
							ComboBox->OnGenerateWidgetEvent.BindDynamic(ComboBox, &UStateKeyInputComboBox::GenerateComboOptionWidget);
						}
#if UNREAL_5_3_OR_NEWER
						ComboBox->SetItemStyle(RowStyle);
#endif
						ComboBox->SelectByTrueValue(Value);
						ComboBox->OnSelectionChanged.AddDynamic(ComboBox, &UStateKeyInputComboBox::OnComboBoxChanged);
						InputWidget = ComboBox;
					}
					else
					{
						UStateKeyInputTextBox* TextBox = nullptr;
						if (bUseAcceptedValuesAutofillText)
						{
							UStateKeyInputAcceptedValuesTextBox* AcceptedValuesTextBox = WidgetTree->ConstructWidget<UStateKeyInputAcceptedValuesTextBox>(UStateKeyInputAcceptedValuesTextBox::StaticClass());
							AcceptedValuesTextBox->AcceptedValues = StateKeyInfo.AcceptedStringValues;

							FString ModelName;
							FString GroupName;
							const bool bIsModelGroupKey = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::SplitModelGroupKey(KeyName, ModelName, GroupName);
							if (bIsModelGroupKey && IsValid(TargetSchema))
							{
								TArray<FString> OptionsForDescriptionScan = StateKeyInfo.AcceptedStringValues;
								if (!Value.IsEmpty() && !OptionsForDescriptionScan.Contains(Value))
								{
									OptionsForDescriptionScan.Add(Value);
								}

								for (const FString& Option : OptionsForDescriptionScan)
								{
									const FString Description = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::ResolveCodeDescription(
										TargetSchema,
										ModelName,
										GroupName,
										Option);
									if (!Description.IsEmpty())
									{
										AcceptedValuesTextBox->OptionDescriptions.FindOrAdd(Option) = Description;
									}
								}
							}

							AcceptedValuesTextBox->bDisplayDescriptionAsOptions = StateKeyInfo.bDisplayDescriptionAsOptions && AcceptedValuesTextBox->OptionDescriptions.Num() > 0;
							AcceptedValuesTextBox->BuildDisplayLabels();
							AcceptedValuesTextBox->OnTextChanged.AddDynamic(AcceptedValuesTextBox, &UStateKeyInputAcceptedValuesTextBox::HandleAcceptedValuesTextChanged);
							TextBox = AcceptedValuesTextBox;
						}
						else
						{
							TextBox = WidgetTree->ConstructWidget<UStateKeyInputTextBox>(UStateKeyInputTextBox::StaticClass());
						}
						if (UStateKeyInputAcceptedValuesTextBox* AcceptedValuesTextBox = Cast<UStateKeyInputAcceptedValuesTextBox>(TextBox))
						{
							TextBox->SetText(FText::FromString(AcceptedValuesTextBox->GetDisplayLabelForValue(Value)));
						}
						else
						{
							TextBox->SetText(FText::FromString(Value));
						}
#if UNREAL_5_3_OR_NEWER
						TextBox->WidgetStyle.TextStyle.ColorAndOpacity = BlackColor;
						TextBox->WidgetStyle.TextStyle.SetFontSize(16.0f);
#endif
						TextBox->KeyName = KeyName + "_INDEX_" + FString::FromInt(i);
						TextBox->ParentDebugUI = this;
						TextBox->InstantBroadcastChange = instantProcess;
						TextBox->DataType = EStateKeyDataType::StringArray;
						TextBox->StateKeyInfo = StateKeyInfo;
						if (UStateKeyInputAcceptedValuesTextBox* AcceptedValuesTextBox = Cast<UStateKeyInputAcceptedValuesTextBox>(TextBox))
						{
							AcceptedValuesTextBox->OnTextCommitted.AddDynamic(AcceptedValuesTextBox, &UStateKeyInputAcceptedValuesTextBox::OnTextValueCommitted);
						}
						else
						{
							TextBox->OnTextCommitted.AddDynamic(TextBox, &UStateKeyInputTextBox::OnTextValueCommitted);
						}
						InputWidget = TextBox;
					}

					if (InputWidget)
					{
						RegisterSchemaNavValue(RowFrame, InputWidget);
						if (bPresetPickKeysMode)
						{
							RegValueWidgets.Add(InputWidget);
						}

						UVerticalBoxSlot* EntrySlot = StringArrayRows->AddChildToVerticalBox(InputWidget);
						EntrySlot->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
						EntrySlot->SetHorizontalAlignment(HAlign_Fill);
						EntrySlot->SetVerticalAlignment(VAlign_Center);
					}
				}

				if (StateKeyInfo.bAllowDynamicArraySize)
				{
					const FString FillValue = (StateKeyInfo.bLimitValues && StateKeyInfo.AcceptedStringValues.Num() > 0)
						? StateKeyInfo.AcceptedStringValues[0]
						: FString();

					UHorizontalBox* ResizeButtonsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

					UStringArrayResizeHelper* AddHelper = NewObject<UStringArrayResizeHelper>(this);
					AddHelper->ParentWidget = this;
					AddHelper->KeyName = KeyName;
					AddHelper->FillValue = FillValue;
					AddHelper->Delta = 1;
					AddHelper->bInstantBroadcast = instantProcess;
					ArrayResizeHelpers.Add(AddHelper);

					UButton* AddButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
					UTextBlock* AddText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
					AddText->SetText(FText::FromString(TEXT("+")));
					AddButton->AddChild(AddText);
					AddButton->OnClicked.AddDynamic(AddHelper, &UStringArrayResizeHelper::ApplyResize);

					UHorizontalBoxSlot* AddSlot = ResizeButtonsRow->AddChildToHorizontalBox(AddButton);
					AddSlot->SetPadding(FMargin(5.f, 0.f, 5.f, 0.f));
					AddSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

					UStringArrayResizeHelper* RemoveHelper = NewObject<UStringArrayResizeHelper>(this);
					RemoveHelper->ParentWidget = this;
					RemoveHelper->KeyName = KeyName;
					RemoveHelper->FillValue = FillValue;
					RemoveHelper->Delta = -1;
					RemoveHelper->bInstantBroadcast = instantProcess;
					ArrayResizeHelpers.Add(RemoveHelper);

					UButton* RemoveButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
					UTextBlock* RemoveText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
					RemoveText->SetText(FText::FromString(TEXT("-")));
					RemoveButton->AddChild(RemoveText);
					RemoveButton->OnClicked.AddDynamic(RemoveHelper, &UStringArrayResizeHelper::ApplyResize);

					UHorizontalBoxSlot* RemoveSlot = ResizeButtonsRow->AddChildToHorizontalBox(RemoveButton);
					RemoveSlot->SetPadding(FMargin(5.f, 0.f, 5.f, 0.f));
					RemoveSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

					UVerticalBoxSlot* ResizeButtonsSlot = StringArrayRows->AddChildToVerticalBox(ResizeButtonsRow);
					ResizeButtonsSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
					ResizeButtonsSlot->SetHorizontalAlignment(HAlign_Left);
					ResizeButtonsSlot->SetVerticalAlignment(VAlign_Center);
				}

				UGridSlot* InputSlot = RowGrid->AddChildToGrid(StringArrayRows, 0, ColInput);
				InputSlot->SetPadding(FMargin(5.f));
				InputSlot->SetHorizontalAlignment(Alignment);
				InputSlot->SetVerticalAlignment(VAlign_Center);
				InputWidget = nullptr;

				const bool bSkipStringArrayDefault = StateKeyInfo.bAllowNullValue
					&& !CurrValFound
					&& (StateKeyInfo.bDefaultValueIsNull || WorkingArray.Num() == 0);
				if (!bSkipStringArrayDefault)
				{
					UpdateJsonObjectKey<TArray<FString>>(KeyName, WorkingArray, ModifiedStateObject);
				}
				break;
			}
			case EStateKeyDataType::Number:
			{
				double CurrentVal = StateKeyInfo.DefaultNumberValue;
				if (StateManager)
					StateManager->GetCurrentStateValue<double>(KeyName, CurrentVal, CurrValFound);
				CurrentVal = ClampNumberValueForSchema(StateKeyInfo, CurrentVal);

				if (StateKeyInfo.bLimitValues)
				{
					UStateKeyInputComboBox* ComboBox = WidgetTree->ConstructWidget<UStateKeyInputComboBox>(UStateKeyInputComboBox::StaticClass());
					ComboBox->KeyName = KeyName;
					ComboBox->ParentDebugUI = this;
					ComboBox->InstantBroadcastChange = instantProcess;
					ComboBox->DataType = EStateKeyDataType::Number;
					ComboBox->StateKeyInfo = StateKeyInfo;

					// Resolve descriptions before adding options so labels can be swapped
					// to descriptions when the schema requests it.
					FString ModelName;
					FString GroupName;
					const bool bIsModelGroupKey = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::SplitModelGroupKey(KeyName, ModelName, GroupName);
					if (bIsModelGroupKey && IsValid(TargetSchema))
					{
						for (double Option : StateKeyInfo.AcceptedNumberValues)
						{
							const FString OptionStr = FString::SanitizeFloat(Option);
							const FString Description = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::ResolveCodeDescription(TargetSchema, ModelName, GroupName, OptionStr);
							if (!Description.IsEmpty())
							{
								ComboBox->OptionDescriptions.FindOrAdd(OptionStr) = Description;
							}
						}
					}

					ComboBox->bDisplayDescriptionAsOptions = StateKeyInfo.bDisplayDescriptionAsOptions && ComboBox->OptionDescriptions.Num() > 0;

					for (double Option : StateKeyInfo.AcceptedNumberValues)
					{
						ComboBox->AddValueOptionWithLabel(FString::SanitizeFloat(Option));
					}
					if (StateKeyInfo.bAllowNullValue)
					{
						ComboBox->AddOption(GDebugNullToken);
					}

					if (ComboBox->OptionDescriptions.Num() > 0)
					{
						ComboBox->OnGenerateWidgetEvent.BindDynamic(ComboBox, &UStateKeyInputComboBox::GenerateComboOptionWidget);
					}

#if UNREAL_5_3_OR_NEWER
					ComboBox->SetItemStyle(RowStyle);
#endif

					const bool bSelectNullNumberOption = StateKeyInfo.bAllowNullValue
						&& !CurrValFound
						&& StateKeyInfo.bDefaultValueIsNull;
					ComboBox->SelectByTrueValue(bSelectNullNumberOption ? GDebugNullToken : FString::SanitizeFloat(CurrentVal));
					ComboBox->OnSelectionChanged.AddDynamic(ComboBox, &UStateKeyInputComboBox::OnComboBoxChanged);
					InputWidget = ComboBox;
				}
				else
				{
					UStateKeyInputTextBox* NumberBox = WidgetTree->ConstructWidget<UStateKeyInputTextBox>(UStateKeyInputTextBox::StaticClass());
					const bool bSelectNullNumberText = StateKeyInfo.bAllowNullValue
						&& !CurrValFound
						&& StateKeyInfo.bDefaultValueIsNull;
					NumberBox->SetText(bSelectNullNumberText ? FText::FromString(GDebugNullToken) : FText::AsNumber(CurrentVal));
#if UNREAL_5_3_OR_NEWER
					NumberBox->WidgetStyle.TextStyle.ColorAndOpacity = BlackColor;
					NumberBox->WidgetStyle.TextStyle.SetFontSize(16.0f);
#endif
					NumberBox->KeyName = KeyName;
					NumberBox->ParentDebugUI = this;
					NumberBox->InstantBroadcastChange = instantProcess;
					NumberBox->DataType = EStateKeyDataType::Number;
					NumberBox->StateKeyInfo = StateKeyInfo;
					NumberBox->OnTextCommitted.AddDynamic(NumberBox, &UStateKeyInputTextBox::OnTextValueCommitted);
					InputWidget = NumberBox;
				}

				RegisterSchemaNavValue(RowFrame, InputWidget);

				if (bPresetPickKeysMode && InputWidget)
					RegValueWidgets.Add(InputWidget);

				const bool bSkipNumberDefault = StateKeyInfo.bAllowNullValue
					&& !CurrValFound
					&& StateKeyInfo.bDefaultValueIsNull;
				if (!bSkipNumberDefault)
				{
					UpdateJsonObjectKey<double>(KeyName, CurrentVal, ModifiedStateObject);
				}
				break;
			}
			case EStateKeyDataType::NumberArray:
			{
				TArray<double> CurrentVals;
				if (StateManager)
					StateManager->GetCurrentStateValue<TArray<double>>(KeyName, CurrentVals, CurrValFound);

				TArray<double> WorkingArray = CurrValFound ? CurrentVals : StateKeyInfo.DefaultNumberArray;
				for (double& ValueToClamp : WorkingArray)
				{
					ValueToClamp = ClampNumberValueForSchema(StateKeyInfo, ValueToClamp);
				}

				const int32 NumValues = WorkingArray.Num();
				const int32 ItemsPerRow = 4;
				const float PaddingBetween = 5.f;

				int GridIdx = 0;

				for (int32 i = 0; i < NumValues; i += ItemsPerRow)
				{
					UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

					for (int32 j = 0; j < ItemsPerRow && (i + j) < NumValues; ++j)
					{
						const double Value = WorkingArray[i + j];
						InputWidget = nullptr;

						if (StateKeyInfo.bLimitValues)
						{
							UStateKeyInputComboBox* ComboBox = WidgetTree->ConstructWidget<UStateKeyInputComboBox>(UStateKeyInputComboBox::StaticClass());
							ComboBox->KeyName = KeyName + "_INDEX_" + FString::FromInt(i + j);
							ComboBox->ParentDebugUI = this;
							ComboBox->InstantBroadcastChange = instantProcess;
							ComboBox->DataType = EStateKeyDataType::NumberArray;
							ComboBox->StateKeyInfo = StateKeyInfo;

							// Resolve descriptions before adding options so labels can be
							// swapped to descriptions when the schema requests it.
							FString ModelName;
							FString GroupName;
							const bool bIsModelGroupKey = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::SplitModelGroupKey(KeyName, ModelName, GroupName);
							if (bIsModelGroupKey && IsValid(TargetSchema))
							{
								for (double Option : StateKeyInfo.AcceptedNumberValues)
								{
									const FString OptionStr = FString::SanitizeFloat(Option);
									const FString Description = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::ResolveCodeDescription(TargetSchema, ModelName, GroupName, OptionStr);
									if (!Description.IsEmpty())
									{
										ComboBox->OptionDescriptions.FindOrAdd(OptionStr) = Description;
									}
								}
							}

							ComboBox->bDisplayDescriptionAsOptions = StateKeyInfo.bDisplayDescriptionAsOptions && ComboBox->OptionDescriptions.Num() > 0;

							for (double Option : StateKeyInfo.AcceptedNumberValues)
							{
								ComboBox->AddValueOptionWithLabel(FString::SanitizeFloat(Option));
							}
							if (StateKeyInfo.bAllowNullValue)
							{
								ComboBox->AddOption(GDebugNullToken);
							}

							if (ComboBox->OptionDescriptions.Num() > 0)
							{
								ComboBox->OnGenerateWidgetEvent.BindDynamic(ComboBox, &UStateKeyInputComboBox::GenerateComboOptionWidget);
							}

#if UNREAL_5_3_OR_NEWER
							ComboBox->SetItemStyle(RowStyle);
#endif

							ComboBox->SelectByTrueValue(FString::SanitizeFloat(Value));
							ComboBox->OnSelectionChanged.AddDynamic(ComboBox, &UStateKeyInputComboBox::OnComboBoxChanged);
							InputWidget = ComboBox;
						}
						else
						{
							UStateKeyInputTextBox* NumberBox = WidgetTree->ConstructWidget<UStateKeyInputTextBox>(UStateKeyInputTextBox::StaticClass());
							NumberBox->SetText(FText::AsNumber(Value));
#if UNREAL_5_3_OR_NEWER
							NumberBox->WidgetStyle.TextStyle.ColorAndOpacity = BlackColor;
							NumberBox->WidgetStyle.TextStyle.SetFontSize(16.0f);
#endif
							NumberBox->KeyName = KeyName + "_INDEX_" + FString::FromInt(i + j);
							NumberBox->DataType = EStateKeyDataType::NumberArray;
							NumberBox->ParentDebugUI = this;
							NumberBox->InstantBroadcastChange = instantProcess;
							NumberBox->StateKeyInfo = StateKeyInfo;
							NumberBox->OnTextCommitted.AddDynamic(NumberBox, &UStateKeyInputTextBox::OnTextValueCommitted);
							InputWidget = NumberBox;
						}

						if (InputWidget)
						{
							RegisterSchemaNavValue(RowFrame, InputWidget);
							if (bPresetPickKeysMode)
								RegValueWidgets.Add(InputWidget);
							UHorizontalBoxSlot* ArraySlot = RowBox->AddChildToHorizontalBox(InputWidget);
							ArraySlot->SetPadding(FMargin(PaddingBetween, 0.0, 0.0, 0.0));
							ArraySlot->SetHorizontalAlignment(HAlign_Fill);
							ArraySlot->SetVerticalAlignment(VAlign_Center);
						}
					}

					UGridSlot* InputSlot = RowGrid->AddChildToGrid(RowBox, GridIdx, ColInput);
					InputSlot->SetPadding(FMargin(0.f, 5.f, 5.f, 5.f));
					InputSlot->SetHorizontalAlignment(Alignment);
					InputSlot->SetVerticalAlignment(VAlign_Center);
					InputWidget = nullptr;
					GridIdx++;
				}

				const bool bSkipNumberArrayDefault = StateKeyInfo.bAllowNullValue
					&& !CurrValFound
					&& (StateKeyInfo.bDefaultValueIsNull || WorkingArray.Num() == 0);
				if (!bSkipNumberArrayDefault)
				{
					UpdateJsonObjectKey<TArray<double>>(KeyName, WorkingArray, ModifiedStateObject);
				}
				break;
			}
			case EStateKeyDataType::Bool:
			{
				bool CurrentVal = StateKeyInfo.DefaultBoolValue;
				if (StateManager)
					StateManager->GetCurrentStateValue<bool>(KeyName, CurrentVal, CurrValFound);

				UStateKeyInputCheckBox* CheckBox = WidgetTree->ConstructWidget<UStateKeyInputCheckBox>(UStateKeyInputCheckBox::StaticClass());
				CheckBox->KeyName = KeyName;
				CheckBox->ParentDebugUI = this;
				CheckBox->InstantBroadcastChange = instantProcess;
				CheckBox->SetIsChecked(CurrentVal);
				CheckBox->SetRenderTransform(FWidgetTransform(FVector2D(0.f, 0.f), FVector2D(2.0f, 2.0f), FVector2D::ZeroVector, 0.f));
				CheckBox->OnCheckStateChanged.AddDynamic(CheckBox, &UStateKeyInputCheckBox::OnCheckBoxChanged);
				FCheckBoxStyle CheckBoxStyle = CheckBox->GetWidgetStyle();

				CheckBoxStyle.UncheckedImage.TintColor = FSlateColor(FLinearColor(0.05f, 0.05f, 0.05f));
				CheckBoxStyle.CheckedImage.TintColor = FSlateColor(FLinearColor(0.05f, 0.05f, 0.05f));
				CheckBoxStyle.UncheckedHoveredImage.TintColor = FSlateColor(FLinearColor::Gray);
				CheckBoxStyle.CheckedPressedImage.TintColor = FSlateColor(FLinearColor::White);
#if UNREAL_5_3_OR_NEWER
				CheckBox->SetWidgetStyle(CheckBoxStyle);
#endif
				Alignment = HAlign_Right;
				InputWidget = CheckBox;

				RegisterSchemaNavValue(RowFrame, InputWidget);

				if (bPresetPickKeysMode && InputWidget)
					RegValueWidgets.Add(InputWidget);

				UpdateJsonObjectKey<bool>(KeyName, CurrentVal, ModifiedStateObject);
				break;
			}
			case EStateKeyDataType::BoolArray:
			{
				TArray<bool> CurrentVals;
				if (StateManager)
					StateManager->GetCurrentStateValue<TArray<bool>>(KeyName, CurrentVals, CurrValFound);

				TArray<bool> WorkingArray = CurrValFound ? CurrentVals : StateKeyInfo.DefaultBoolArray;

				const int32 NumValues = WorkingArray.Num();
				const bool bHasLabels = (StateKeyInfo.AcceptedStringValues.Num() == NumValues) && NumValues > 0;

				// Route per-index changes for labeled BoolArrays of shape {currentModel}.{Group} through
				// the config-array wrapper so the broadcast is the full TArray<bool> (which the
				// configuration sync reconciles into the comma-delimited string), not the indexed key.
				bool bIsModelConfigBoolArray = false;
				if (bHasLabels)
				{
					FString ModelNameFromKey;
					FString GroupNameFromKey;
					const bool bHasModelGroupKey = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::SplitModelGroupKey(KeyName, ModelNameFromKey, GroupNameFromKey);
					if (bHasModelGroupKey)
					{
						bool bMatchesDimeMetadata = false;
						if (IsValid(TargetSchema))
						{
							for (const FDIMEModelMetadata& ModelMetadata : TargetSchema->DimeModelData)
							{
								if (!ModelMetadata.ModelName.Equals(ModelNameFromKey, ESearchCase::IgnoreCase))
								{
									continue;
								}

								for (const FDIMEModelCodeMetadata& CodeMetadata : ModelMetadata.Codes)
								{
									if (CodeMetadata.Group.Equals(GroupNameFromKey, ESearchCase::IgnoreCase))
									{
										bMatchesDimeMetadata = true;
										break;
									}
								}

								if (bMatchesDimeMetadata)
								{
									break;
								}
							}
						}

						bool bMatchesCurrentModelPrefix = false;
						FString CurrentModel;
						bool bHasModel = false;
						if (StateManager)
						{
							StateManager->GetCurrentStateValue<FString>(TEXT("model"), CurrentModel, bHasModel);
						}

						if (bHasModel && !CurrentModel.IsEmpty())
						{
							const FString ModelPrefix = CurrentModel + TEXT(".");
							if (KeyName.StartsWith(ModelPrefix, ESearchCase::CaseSensitive))
							{
								const FString TrailingGroup = KeyName.RightChop(ModelPrefix.Len());
								bMatchesCurrentModelPrefix = !TrailingGroup.IsEmpty() && !TrailingGroup.Contains(TEXT("."));
							}
						}

						bIsModelConfigBoolArray = bMatchesDimeMetadata || bMatchesCurrentModelPrefix;
					}
				}

				UVerticalBox* BoolArrayRows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
				for (int32 i = 0; i < NumValues; ++i)
				{
					const bool Value = WorkingArray[i];
					InputWidget = nullptr;

					UStateKeyInputCheckBox* CheckBox = nullptr;
					if (bIsModelConfigBoolArray)
					{
						UStateKeyInputConfigArrayCheckBox* ConfigCheckBox = WidgetTree->ConstructWidget<UStateKeyInputConfigArrayCheckBox>(UStateKeyInputConfigArrayCheckBox::StaticClass());
						ConfigCheckBox->ParentKey = KeyName;
						ConfigCheckBox->Index = i;
						ConfigCheckBox->ExpectedArrayLength = NumValues;
						ConfigCheckBox->DefaultArrayTemplate = StateKeyInfo.DefaultBoolArray;
						if (ConfigCheckBox->DefaultArrayTemplate.Num() < NumValues)
						{
							ConfigCheckBox->DefaultArrayTemplate.SetNum(NumValues);
						}
						ConfigCheckBox->KeyName = KeyName + "_INDEX_" + FString::FromInt(i);
						ConfigCheckBox->OnCheckStateChanged.AddDynamic(ConfigCheckBox, &UStateKeyInputConfigArrayCheckBox::OnConfigArrayCheckBoxChanged);
						CheckBox = ConfigCheckBox;
					}
					else
					{
						CheckBox = WidgetTree->ConstructWidget<UStateKeyInputCheckBox>(UStateKeyInputCheckBox::StaticClass());
						CheckBox->KeyName = KeyName + "_INDEX_" + FString::FromInt(i);
						CheckBox->OnCheckStateChanged.AddDynamic(CheckBox, &UStateKeyInputCheckBox::OnCheckBoxChanged);
					}
					CheckBox->ParentDebugUI = this;
					CheckBox->InstantBroadcastChange = instantProcess;
					CheckBox->SetIsChecked(Value);
					CheckBox->SetRenderTransform(FWidgetTransform(FVector2D(0.f, 0.f), FVector2D(2.0f, 2.0f), FVector2D::ZeroVector, 0.f));
					FCheckBoxStyle CheckBoxStyle = CheckBox->GetWidgetStyle();

					CheckBoxStyle.UncheckedImage.TintColor = FSlateColor(FLinearColor(0.05f, 0.05f, 0.05f));
					CheckBoxStyle.CheckedImage.TintColor = FSlateColor(FLinearColor(0.05f, 0.05f, 0.05f));
					CheckBoxStyle.UncheckedHoveredImage.TintColor = FSlateColor(FLinearColor::Gray);
					CheckBoxStyle.CheckedPressedImage.TintColor = FSlateColor(FLinearColor::White);
#if UNREAL_5_3_OR_NEWER
					CheckBox->SetWidgetStyle(CheckBoxStyle);
#endif
					Alignment = HAlign_Right;
					InputWidget = CheckBox;

					if (InputWidget)
					{
						RegisterSchemaNavValue(RowFrame, InputWidget);
						if (bPresetPickKeysMode)
							RegValueWidgets.Add(InputWidget);

						UHorizontalBox* EntryRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
						UHorizontalBoxSlot* CheckSlot = EntryRow->AddChildToHorizontalBox(InputWidget);
						CheckSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 0.f));
						CheckSlot->SetHorizontalAlignment(HAlign_Left);
						CheckSlot->SetVerticalAlignment(VAlign_Center);

						if (bHasLabels)
						{
							const FString CodeValue = StateKeyInfo.AcceptedStringValues[i];

							FString Description;
							FString ModelName;
							FString GroupName;
							const bool bHasModelGroupKey = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::SplitModelGroupKey(KeyName, ModelName, GroupName);
							if (bHasModelGroupKey && IsValid(TargetSchema))
							{
								Description = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::ResolveCodeDescription(
									TargetSchema,
									ModelName,
									GroupName,
									CodeValue).TrimStartAndEnd();
							}

							// When the schema requests it (and a description exists), show the
							// description as the checkbox label and reveal the underlying code via
							// the tooltip; otherwise keep the code as the label with the description tooltip.
							const bool bShowDescriptionAsLabel = StateKeyInfo.bDisplayDescriptionAsOptions && !Description.IsEmpty();
							const FString LabelText = bShowDescriptionAsLabel ? Description : CodeValue;
							const FString TooltipString = bShowDescriptionAsLabel ? CodeValue : Description;

							UTextBlock* CheckLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
							CheckLabel->SetText(FText::FromString(LabelText));
							FSlateFontInfo LabelFontInfo = CheckLabel->GetFont();
							LabelFontInfo.Size = 14;
							CheckLabel->SetFont(LabelFontInfo);

							if (UWidget* TooltipWidget = ZLDebugUISchemaNavGlobals::ZLDebugUIDimeDescriptions::CreateStyledTooltipWidget(CheckLabel, TooltipString))
							{
								CheckLabel->SetToolTip(TooltipWidget);
								CheckBox->SetToolTip(TooltipWidget);
							}

							UHorizontalBoxSlot* CheckLabelSlot = EntryRow->AddChildToHorizontalBox(CheckLabel);
							CheckLabelSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
							CheckLabelSlot->SetHorizontalAlignment(HAlign_Left);
							CheckLabelSlot->SetVerticalAlignment(VAlign_Center);
						}

						UVerticalBoxSlot* EntrySlot = BoolArrayRows->AddChildToVerticalBox(EntryRow);
						EntrySlot->SetPadding(FMargin(0.f, 1.f, 0.f, 1.f));
						EntrySlot->SetHorizontalAlignment(HAlign_Left);
						EntrySlot->SetVerticalAlignment(VAlign_Center);
					}
				}

				UGridSlot* InputSlot = RowGrid->AddChildToGrid(BoolArrayRows, 0, ColInput);
				InputSlot->SetPadding(FMargin(20.f, 5.f, 5.f, 5.f));
				InputSlot->SetHorizontalAlignment(HAlign_Left);
				InputSlot->SetVerticalAlignment(VAlign_Center);
				InputWidget = nullptr;

				UpdateJsonObjectKey<TArray<bool>>(KeyName, WorkingArray, ModifiedStateObject);
				break;
			}
			default:
				break;
			}

			if (InputWidget)
			{
				UGridSlot* InputSlot = RowGrid->AddChildToGrid(InputWidget, 0, ColInput);
				InputSlot->SetPadding(FMargin(5.f));
				InputSlot->SetHorizontalAlignment(Alignment);
				InputSlot->SetVerticalAlignment(VAlign_Center);

				if (allowResendCurrentValues)
				{
					UButton* ResendButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
					UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

					if (ButtonText)
					{
						ButtonText->SetText(FText::FromString(TEXT("Resend")));

						FSlateFontInfo ButtonFontInfo = ButtonText->GetFont();
						ButtonFontInfo.Size = 16;

						ButtonText->SetFont(ButtonFontInfo);
						ButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));

						ResendButton->AddChild(ButtonText);

						UButtonSlot* TextSlot = Cast<UButtonSlot>(ButtonText->Slot);

						if (TextSlot)
						{
							TextSlot->SetHorizontalAlignment(HAlign_Center);
							TextSlot->SetVerticalAlignment(VAlign_Center);
							TextSlot->SetPadding(FMargin(-1.0f, 0.0f, 0.0f, 0.0f));
						}
					}

					if (UStateKeyInputTextBox* TextBox = Cast<UStateKeyInputTextBox>(InputWidget))
					{
						ResendButton->OnClicked.AddDynamic(TextBox, &UStateKeyInputTextBox::TriggerResend);
					}
					else if (UStateKeyInputComboBox* ComboBox = Cast<UStateKeyInputComboBox>(InputWidget))
					{
						ResendButton->OnClicked.AddDynamic(ComboBox, &UStateKeyInputComboBox::TriggerResend);
					}
					else if (UStateKeyInputCheckBox* CheckBox = Cast<UStateKeyInputCheckBox>(InputWidget))
					{
						ResendButton->OnClicked.AddDynamic(CheckBox, &UStateKeyInputCheckBox::TriggerResend);
					}

					UGridSlot* ResendSlot = RowGrid->AddChildToGrid(ResendButton, 0, ColResend);
					ResendSlot->SetPadding(FMargin(5.f));
					ResendSlot->SetHorizontalAlignment(Alignment);
					ResendSlot->SetVerticalAlignment(VAlign_Center);
				}
			}

			ParentBox->AddChildToVerticalBox(RowFrame);
		}
	}

	}

	if (PresetOptionsDropdown && !addNewPresetUIVisible)
		PopulatePresetOptionsDropdown();

	if (bPresetPickKeysMode)
		UpdatePresetSaveButtonEnabledState();

	RestoreSchemaNavSelectionAfterRebuild();
	ApplySchemaNavSelectionVisual();

	if (bEnableDebugUIKeyboardControl && bRegisteredForSchemaNavGlobalInput)
	{
		ZLDebugUISchemaNavGlobals::EnsureProcessorRegistered();
	}
}
