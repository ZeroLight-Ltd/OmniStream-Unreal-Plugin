// Copyright ZeroLight ltd. All Rights Reserved.

#if WITH_EDITOR

#include "SZLFilteredKeysEditor.h"
#include "JavaScriptKeyCodes.inl"
#include "EditorZLCloudPluginSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SZLFilteredKeysEditor"

namespace ZLFilteredKeyPresets
{
	// Q, W, E, A, S, D, I, U, O, J, K, L, arrows, Space
	static const TCHAR* DefaultKeys = TEXT("81,87,69,65,83,68,73,85,79,74,75,76,38,37,40,39,32");
	static const TCHAR* Control = TEXT("254,17");
	static const TCHAR* Shift = TEXT("253,16");
	static const TCHAR* AllLetters = TEXT("77,78,66,86,67,88,90,76,75,74,72,71,70,68,83,65,80,79,73,85,89,84,82,69,87,81");
	static const TCHAR* AllNumbers = TEXT("105,104,103,102,101,100,99,98,97,48,57,56,55,54,53,52,51,50,49");
	static const TCHAR* WASD = TEXT("68,65,83,87");
	static const TCHAR* Arrows = TEXT("39,40,37,38");
	static const TCHAR* IJKLOU = TEXT("73,74,75,76,79,85");
}

TWeakPtr<SWindow> SZLFilteredKeysEditor::s_Window;

static void ParseKeyCodes(const FString& CommaList, TArray<int32>& OutCodes)
{
	OutCodes.Reset();

	TArray<FString> Parts;
	CommaList.ParseIntoArray(Parts, TEXT(","), true);

	TSet<int32> Seen;
	for (const FString& Part : Parts)
	{
		const FString Trimmed = Part.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || !Trimmed.IsNumeric())
		{
			continue;
		}

		const int32 Code = FCString::Atoi(*Trimmed);
		if (Code < 0 || Code > 255 || Seen.Contains(Code))
		{
			continue;
		}

		Seen.Add(Code);
		OutCodes.Add(Code);
	}
}

static FString JoinKeyCodes(const TArray<int32>& Codes)
{
	TArray<FString> Parts;
	Parts.Reserve(Codes.Num());
	for (const int32 Code : Codes)
	{
		Parts.Add(FString::FromInt(Code));
	}
	return FString::Join(Parts, TEXT(","));
}

void SZLFilteredKeysEditor::OpenWindow()
{
	if (TSharedPtr<SWindow> Existing = s_Window.Pin())
	{
		Existing->BringToFront();
		return;
	}

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "OmniStream: Input Key Filter"))
		.CreateTitleBar(true)
		.FocusWhenFirstShown(true)
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		.ClientSize(FVector2D(640.0, 580.0))
		.MinWidth(480.0)
		.MinHeight(400.0)
		.SizingRule(ESizingRule::UserSized);

	Window->SetContent(SNew(SZLFilteredKeysEditor));
	Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([](const TSharedRef<SWindow>&)
	{
		s_Window = nullptr;
	}));

	s_Window = Window;
	FSlateApplication::Get().AddWindow(Window, true);
}

SZLFilteredKeysEditor::~SZLFilteredKeysEditor()
{
	StopRecording();
}

void SZLFilteredKeysEditor::Construct(const FArguments& InArgs)
{
	LoadFromSettings();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Help", "Keys the browser is allowed to send into the streamed app. Record keys, toggle the presets, or edit the comma-separated JavaScript key codes directly."))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(this, &SZLFilteredKeysEditor::GetStatusText)
				.ColorAndOpacity(this, &SZLFilteredKeysEditor::GetStatusColor)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.Text(this, &SZLFilteredKeysEditor::GetRecordButtonText)
					.OnClicked(this, &SZLFilteredKeysEditor::OnRecordClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearAll", "Clear All"))
					.OnClicked(this, &SZLFilteredKeysEditor::OnClearAllClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PresetsLabel", "Quick toggle"))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 8.0f)
			[
				SNew(SWrapBox)
				.UseAllottedSize(true)
				+ SWrapBox::Slot().Padding(0.0f, 0.0f, 6.0f, 6.0f)[MakePresetButton(LOCTEXT("PresetDefault", "Default"), ZLFilteredKeyPresets::DefaultKeys)]
				+ SWrapBox::Slot().Padding(0.0f, 0.0f, 6.0f, 6.0f)[MakePresetButton(LOCTEXT("PresetWASD", "WASD"), ZLFilteredKeyPresets::WASD)]
				+ SWrapBox::Slot().Padding(0.0f, 0.0f, 6.0f, 6.0f)[MakePresetButton(LOCTEXT("PresetIJKLOU", "IJKLOU"), ZLFilteredKeyPresets::IJKLOU)]
				+ SWrapBox::Slot().Padding(0.0f, 0.0f, 6.0f, 6.0f)[MakePresetButton(LOCTEXT("PresetArrows", "Arrows"), ZLFilteredKeyPresets::Arrows)]
				+ SWrapBox::Slot().Padding(0.0f, 0.0f, 6.0f, 6.0f)[MakePresetButton(LOCTEXT("PresetShift", "Shift"), ZLFilteredKeyPresets::Shift)]
				+ SWrapBox::Slot().Padding(0.0f, 0.0f, 6.0f, 6.0f)[MakePresetButton(LOCTEXT("PresetControl", "Control"), ZLFilteredKeyPresets::Control)]
				+ SWrapBox::Slot().Padding(0.0f, 0.0f, 6.0f, 6.0f)[MakePresetButton(LOCTEXT("PresetLetters", "All Letters"), ZLFilteredKeyPresets::AllLetters)]
				+ SWrapBox::Slot().Padding(0.0f, 0.0f, 6.0f, 6.0f)[MakePresetButton(LOCTEXT("PresetNumbers", "All Numbers"), ZLFilteredKeyPresets::AllNumbers)]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0.0f, 8.0f)
			[
				SAssignNew(KeyListView, SListView<TSharedPtr<FZLFilteredKeyItem>>)
				.ListItemsSource(&KeyItems)
				.OnGenerateRow(this, &SZLFilteredKeysEditor::GenerateKeyRow)
				.SelectionMode(ESelectionMode::None)
				.HeaderRow(
					SNew(SHeaderRow)
					+ SHeaderRow::Column("Name")
					.DefaultLabel(LOCTEXT("ColName", "Key"))
					.FillWidth(0.5f)
					+ SHeaderRow::Column("Code")
					.DefaultLabel(LOCTEXT("ColCode", "JS Code"))
					.FillWidth(0.3f)
					+ SHeaderRow::Column("Remove")
					.DefaultLabel(LOCTEXT("ColRemove", ""))
					.FillWidth(0.2f)
				)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RawLabel", "Key codes"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(RawCodesBox, SEditableTextBox)
				.IsEnabled_Lambda([this]() { return !bRecording; })
				.OnTextCommitted(this, &SZLFilteredKeysEditor::OnRawCodesCommitted)
				.HintText(LOCTEXT("RawHint", "e.g. 81,87,69,65,83,68"))
			]
		]
	];

	RefreshListFromCodes();
}

TSharedRef<SWidget> SZLFilteredKeysEditor::MakePresetButton(const FText& Label, const TCHAR* CommaSeparatedCodes)
{
	return SNew(SCheckBox)
		.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("ToggleButtonCheckbox")))
		.Padding(FMargin(8.0f, 4.0f))
		.ToolTipText_Lambda([this, CommaSeparatedCodes]()
		{
			return IsPresetActive(CommaSeparatedCodes)
				? LOCTEXT("PresetRemoveTip", "Remove these keys")
				: LOCTEXT("PresetAddTip", "Add these keys");
		})
		.IsChecked_Lambda([this, CommaSeparatedCodes]()
		{
			return IsPresetActive(CommaSeparatedCodes) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this, CommaSeparatedCodes](ECheckBoxState)
		{
			TogglePreset(CommaSeparatedCodes);
		})
		[
			SNew(STextBlock).Text(Label)
		];
}

void SZLFilteredKeysEditor::LoadFromSettings()
{
	const UZLCloudPluginSettings* Settings = GetDefault<UZLCloudPluginSettings>();
	if (Settings)
	{
		ParseKeyCodes(Settings->filteredKeyList, CurrentCodes);
	}
}

void SZLFilteredKeysEditor::SaveToSettings()
{
	UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();
	if (!Settings)
	{
		return;
	}

	Settings->filteredKeyList = JoinKeyCodes(CurrentCodes);

	if (FProperty* Property = UZLCloudPluginSettings::StaticClass()->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(UZLCloudPluginSettings, filteredKeyList)))
	{
		FPropertyChangedEvent ChangeEvent(Property);
		Settings->PostEditChangeProperty(ChangeEvent);
	}
	else
	{
		Settings->UpdateFilteredKeys();
	}
}

void SZLFilteredKeysEditor::RefreshListFromCodes()
{
	KeyItems.Reset();
	KeyItems.Reserve(CurrentCodes.Num());
	for (const int32 Code : CurrentCodes)
	{
		TSharedPtr<FZLFilteredKeyItem> Item = MakeShared<FZLFilteredKeyItem>();
		Item->Code = Code;
		Item->Name = ZLCloudPlugin::GetJavaScriptKeyDisplayName(Code);
		KeyItems.Add(Item);
	}

	if (KeyListView.IsValid())
	{
		KeyListView->RequestListRefresh();
	}

	UpdateRawCodesBox();
}

void SZLFilteredKeysEditor::SetCodes(const TArray<int32>& NewCodes, bool bSave)
{
	CurrentCodes = NewCodes;
	RefreshListFromCodes();
	if (bSave)
	{
		SaveToSettings();
	}
}

void SZLFilteredKeysEditor::AddKeyCodes(const TArray<int32>& CodesToAdd)
{
	TArray<int32> Merged;
	TSet<int32> Seen;
	Merged.Reserve(CodesToAdd.Num() + CurrentCodes.Num());

	auto AppendUnique = [&Merged, &Seen](int32 Code)
	{
		if (Code < 0 || Code > 255 || Seen.Contains(Code))
		{
			return;
		}
		Seen.Add(Code);
		Merged.Add(Code);
	};

	for (const int32 Code : CodesToAdd)
	{
		AppendUnique(Code);
	}
	for (const int32 Code : CurrentCodes)
	{
		AppendUnique(Code);
	}

	SetCodes(Merged, true);
}

void SZLFilteredKeysEditor::RemoveKeyCodes(const TArray<int32>& CodesToRemove)
{
	if (CodesToRemove.Num() == 0)
	{
		return;
	}

	const TSet<int32> ToRemove(CodesToRemove);
	CurrentCodes.RemoveAll([&ToRemove](const int32 Code)
	{
		return ToRemove.Contains(Code);
	});
	RefreshListFromCodes();
	SaveToSettings();
}

bool SZLFilteredKeysEditor::IsPresetActive(const TCHAR* CommaSeparatedCodes) const
{
	TArray<int32> PresetCodes;
	ParseKeyCodes(CommaSeparatedCodes, PresetCodes);
	if (PresetCodes.Num() == 0)
	{
		return false;
	}

	for (const int32 Code : PresetCodes)
	{
		if (!CurrentCodes.Contains(Code))
		{
			return false;
		}
	}

	return true;
}

void SZLFilteredKeysEditor::TogglePreset(const TCHAR* CommaSeparatedCodes)
{
	TArray<int32> PresetCodes;
	ParseKeyCodes(CommaSeparatedCodes, PresetCodes);
	if (PresetCodes.Num() == 0)
	{
		return;
	}

	if (IsPresetActive(CommaSeparatedCodes))
	{
		RemoveKeyCodes(PresetCodes);
	}
	else
	{
		AddKeyCodes(PresetCodes);
	}
}

void SZLFilteredKeysEditor::RemoveKeyCode(int32 Code)
{
	CurrentCodes.Remove(Code);
	RefreshListFromCodes();
	SaveToSettings();
}

void SZLFilteredKeysEditor::TryAddRecordedKey(const FKey& Key)
{
	const int32 Code = ZLCloudPlugin::FindJavaScriptKeyCodeFromFKey(Key);
	if (Code == INDEX_NONE || CurrentCodes.Contains(Code))
	{
		return;
	}

	AddKeyCodes({ Code });
}

void SZLFilteredKeysEditor::StartRecording()
{
	if (bRecording)
	{
		return;
	}

	bRecording = true;
	if (!PreInputKeyDownHandle.IsValid())
	{
		PreInputKeyDownHandle = FSlateApplication::Get().OnApplicationPreInputKeyDownListener().AddSP(
			this, &SZLFilteredKeysEditor::HandleApplicationKeyDown);
	}

	FSlateApplication::Get().SetKeyboardFocus(AsShared());
}

void SZLFilteredKeysEditor::StopRecording()
{
	bRecording = false;
	if (PreInputKeyDownHandle.IsValid())
	{
		FSlateApplication::Get().OnApplicationPreInputKeyDownListener().Remove(PreInputKeyDownHandle);
		PreInputKeyDownHandle.Reset();
	}
}

void SZLFilteredKeysEditor::HandleApplicationKeyDown(const FKeyEvent& InKeyEvent)
{
	if (!bRecording || !IsEditorWindowActive())
	{
		return;
	}

	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		StopRecording();
		return;
	}

	TryAddRecordedKey(Key);
}

bool SZLFilteredKeysEditor::IsEditorWindowActive() const
{
	const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	return Window.IsValid() && Window->IsActive();
}

FReply SZLFilteredKeysEditor::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bRecording)
	{
		return FReply::Handled();
	}

	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

FReply SZLFilteredKeysEditor::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	if (bRecording)
	{
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyChar(MyGeometry, InCharacterEvent);
}

FReply SZLFilteredKeysEditor::OnRecordClicked()
{
	if (bRecording)
	{
		StopRecording();
	}
	else
	{
		StartRecording();
	}

	return FReply::Handled();
}

FReply SZLFilteredKeysEditor::OnClearAllClicked()
{
	if (CurrentCodes.Num() == 0)
	{
		return FReply::Handled();
	}

	const EAppReturnType::Type Response = FMessageDialog::Open(
		EAppMsgType::YesNo,
		LOCTEXT("ClearConfirm", "Remove all allowed keys from the whitelist?"));

	if (Response == EAppReturnType::Yes)
	{
		StopRecording();
		SetCodes(TArray<int32>(), true);
	}

	return FReply::Handled();
}

FText SZLFilteredKeysEditor::GetRecordButtonText() const
{
	return bRecording
		? LOCTEXT("Stop", "Stop")
		: LOCTEXT("Record", "Record");
}

FText SZLFilteredKeysEditor::GetStatusText() const
{
	if (bRecording)
	{
		return LOCTEXT("RecordingStatus", "Recording: press keys to add them. Esc or Stop to finish.");
	}

	return FText::Format(
		LOCTEXT("IdleStatus", "{0} allowed key(s)"),
		FText::AsNumber(CurrentCodes.Num()));
}

FSlateColor SZLFilteredKeysEditor::GetStatusColor() const
{
	return bRecording
		? FSlateColor(FLinearColor(1.0f, 0.72f, 0.15f))
		: FSlateColor::UseForeground();
}

void SZLFilteredKeysEditor::UpdateRawCodesBox()
{
	if (RawCodesBox.IsValid())
	{
		RawCodesBox->SetText(FText::FromString(JoinKeyCodes(CurrentCodes)));
	}
}

void SZLFilteredKeysEditor::OnRawCodesCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (bRecording || CommitType == ETextCommit::OnCleared)
	{
		return;
	}

	TArray<int32> Parsed;
	ParseKeyCodes(NewText.ToString(), Parsed);
	SetCodes(Parsed, true);
}

TSharedRef<ITableRow> SZLFilteredKeysEditor::GenerateKeyRow(
	TSharedPtr<FZLFilteredKeyItem> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const int32 Code = Item.IsValid() ? Item->Code : 0;
	const FString Name = Item.IsValid() ? Item->Name : FString();

	return SNew(STableRow<TSharedPtr<FZLFilteredKeyItem>>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.5f)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 2.0f)
			[
				SNew(STextBlock).Text(FText::FromString(Name))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.3f)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(FText::AsNumber(Code))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.2f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.Padding(4.0f, 2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Remove", "Remove"))
				.OnClicked_Lambda([this, Code]()
				{
					RemoveKeyCode(Code);
					return FReply::Handled();
				})
			]
		];
}

#undef LOCTEXT_NAMESPACE

#endif
