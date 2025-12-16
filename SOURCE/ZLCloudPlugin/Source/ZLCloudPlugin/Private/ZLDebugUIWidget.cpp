#include "ZLDebugUIWidget.h"
#include "ZLCloudPluginStateManager.h"
#include "ZLCloudPluginVersion.h"

void UZLDebugUIWidget::NativeConstruct()
{
    Super::NativeConstruct();

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
}

void UZLDebugUIWidget::NativeDestruct()
{
    Super::NativeDestruct();

    if (InstantChangeToggle)
    {
        InstantChangeToggle->OnCheckStateChanged.RemoveDynamic(this, &UZLDebugUIWidget::OnSubmitStateInstantBoxChanged);
    }

    if (SubmitStateBtn)
    {
        SubmitStateBtn->OnClicked.RemoveDynamic(this, &UZLDebugUIWidget::OnSubmitState);
    }
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
    if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
    {
        FString JsonString;
        TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString, 1);
        FJsonSerializer::Serialize(ModifiedStateObject.ToSharedRef(), JsonWriter);
        JsonWriter->Close();

        Delegates->OnRecieveData.Broadcast(JsonString);
    }
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

void UStateKeyInputComboBox::OnComboBoxChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
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

            WorkingArray[ArrayIndex] = FCString::Atod(*SelectedItem);

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
                Json = CreateStateChangeJsonStr<double>(CleanKey, FCString::Atod(*SelectedItem));

            if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
            {
                Delegates->OnRecieveData.Broadcast(Json);
            }
        }
        else
        {
            UpdateJsonObjectKey<FString>(CleanKey, SelectedItem, ParentDebugUI->ModifiedStateObject);
        }
    }
}

void UStateKeyInputCheckBox::OnCheckBoxChanged(bool bIsChecked)
{
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

void UStateKeyInputTextBox::OnTextValueCommitted(const FText& ComittedText, ETextCommit::Type CommitMethod)
{
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

            WorkingArray[ArrayIndex] = ComittedText.ToString();

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

            WorkingArray[ArrayIndex] = FCString::Atod(*ComittedText.ToString());

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

            const FString Lower = ComittedText.ToString().ToLower();
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
            double NumberValue = FCString::Atod(*ComittedText.ToString());
            if (InstantBroadcastChange)
                Json = CreateStateChangeJsonStr<double>(CleanKey, NumberValue);
            else
                UpdateJsonObjectKey<double>(CleanKey, NumberValue, ParentDebugUI->ModifiedStateObject);
        }
        else if (DataType == EStateKeyDataType::Bool)
        {
            const FString Lower = ComittedText.ToString().ToLower();
            bool BoolValue = (Lower == "true" || Lower == "1");
            if (InstantBroadcastChange)
                Json = CreateStateChangeJsonStr<bool>(CleanKey, BoolValue);
            else
                UpdateJsonObjectKey<bool>(CleanKey, BoolValue, ParentDebugUI->ModifiedStateObject);
        }
        else
        {
            if (InstantBroadcastChange)
                Json = CreateStateChangeJsonStr<FString>(CleanKey, ComittedText.ToString());
            else
                UpdateJsonObjectKey<FString>(CleanKey, ComittedText.ToString(), ParentDebugUI->ModifiedStateObject);
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



void UZLDebugUIWidget::RebuildDebugUI()
{
	RebuildDebugUIWithNesting();
}

void UZLDebugUIWidget::RebuildDebugUIWithNesting()
{
	if (!IsValid(TargetSchema) || !SchemaOptionsVBox) return;

	if (SchemaTitle)
	{
		SchemaTitle->SetText(FText::FromString("Schema: " + TargetSchema->GetName()));
	}

	ModifiedStateObject = MakeShared<FJsonObject>();

	UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();

	SchemaOptionsVBox->ClearChildren();
	FoldoutHelpers.Empty();

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

					// Add the button and the content to the parent box
					UVerticalBoxSlot* ButtonSlot = ParentBox->AddChildToVerticalBox(ToggleButton);
					ButtonSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
					ButtonSlot->SetPadding(FMargin(0, 2));
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

			UGridPanel* RowGrid = WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass());
			RowGrid->SetColumnFill(0, 1.0f);
			RowGrid->SetColumnFill(1, 1.0f);

			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Label->SetText(FText::FromString(DisplayKeyName));

			FSlateFontInfo FontInfo = Label->GetFont();
			FontInfo.Size = 16;
			Label->SetFont(FontInfo);

			UGridSlot* LabelSlot = RowGrid->AddChildToGrid(Label, 0, 0);
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

				if (StateKeyInfo.bLimitValues)
				{
					UStateKeyInputComboBox* ComboBox = WidgetTree->ConstructWidget<UStateKeyInputComboBox>(UStateKeyInputComboBox::StaticClass());
					ComboBox->KeyName = KeyName;
					ComboBox->ParentDebugUI = this;
					ComboBox->InstantBroadcastChange = instantProcess;
					ComboBox->DataType = EStateKeyDataType::String;
					ComboBox->StateKeyInfo = StateKeyInfo;
					for (const FString& Option : StateKeyInfo.AcceptedStringValues)
					{
						ComboBox->AddOption(Option);
					}

					if (!StateKeyInfo.AcceptedStringValues.Contains(CurrentVal))
					{
						ComboBox->AddOption(CurrentVal);
					}

#if UNREAL_5_3_OR_NEWER
					ComboBox->SetItemStyle(RowStyle);
#endif

					ComboBox->SetSelectedOption(CurrentVal);
					ComboBox->OnSelectionChanged.AddDynamic(ComboBox, &UStateKeyInputComboBox::OnComboBoxChanged);
					InputWidget = ComboBox;
				}
				else
				{
					UStateKeyInputTextBox* TextBox = WidgetTree->ConstructWidget<UStateKeyInputTextBox>(UStateKeyInputTextBox::StaticClass());
					TextBox->SetText(FText::FromString(CurrentVal));
#if UNREAL_5_3_OR_NEWER
					TextBox->WidgetStyle.TextStyle.ColorAndOpacity = BlackColor;
					TextBox->WidgetStyle.TextStyle.SetFontSize(16.0f);
#endif
					TextBox->KeyName = KeyName;
					TextBox->ParentDebugUI = this;
					TextBox->InstantBroadcastChange = instantProcess;
					TextBox->DataType = EStateKeyDataType::String;
					TextBox->OnTextCommitted.AddDynamic(TextBox, &UStateKeyInputTextBox::OnTextValueCommitted);
					InputWidget = TextBox;
				}

				UpdateJsonObjectKey<FString>(KeyName, CurrentVal, ModifiedStateObject);
				break;
			}
			case EStateKeyDataType::StringArray:
			{
				TArray<FString> CurrentVals;
				if (StateManager)
					StateManager->GetCurrentStateValue<TArray<FString>>(KeyName, CurrentVals, CurrValFound);

				TArray<FString> WorkingArray = CurrValFound ? CurrentVals : StateKeyInfo.DefaultStringArray;

				const int32 NumValues = WorkingArray.Num();
				const int32 ItemsPerRow = (StateKeyInfo.bLimitValues) ? 2 : 4;
				const float PaddingBetween = 5.f;

				int GridIdx = 0;

				for (int32 i = 0; i < NumValues; i += ItemsPerRow)
				{
					UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

					for (int32 j = 0; j < ItemsPerRow && (i + j) < NumValues; ++j)
					{
						const FString& Value = WorkingArray[i + j];
						InputWidget = nullptr;

						if (StateKeyInfo.bLimitValues)
						{
							UStateKeyInputComboBox* ComboBox = WidgetTree->ConstructWidget<UStateKeyInputComboBox>(UStateKeyInputComboBox::StaticClass());
							ComboBox->KeyName = KeyName + "_INDEX_" + FString::FromInt(i + j);
							ComboBox->ParentDebugUI = this;
							ComboBox->InstantBroadcastChange = instantProcess;
							ComboBox->DataType = EStateKeyDataType::StringArray;
							ComboBox->StateKeyInfo = StateKeyInfo;
							for (const FString& Option : StateKeyInfo.AcceptedStringValues)
							{
								ComboBox->AddOption(Option);
							}

#if UNREAL_5_3_OR_NEWER
							ComboBox->SetItemStyle(RowStyle);
#endif

							ComboBox->SetSelectedOption(Value);
							ComboBox->OnSelectionChanged.AddDynamic(ComboBox, &UStateKeyInputComboBox::OnComboBoxChanged);
							InputWidget = ComboBox;
						}
						else
						{
							UStateKeyInputTextBox* TextBox = WidgetTree->ConstructWidget<UStateKeyInputTextBox>(UStateKeyInputTextBox::StaticClass());
							TextBox->SetText(FText::FromString(Value));
#if UNREAL_5_3_OR_NEWER
							TextBox->WidgetStyle.TextStyle.ColorAndOpacity = BlackColor;
							TextBox->WidgetStyle.TextStyle.SetFontSize(16.0f);
#endif
							TextBox->KeyName = KeyName + "_INDEX_" + FString::FromInt(i + j);
							TextBox->ParentDebugUI = this;
							TextBox->InstantBroadcastChange = instantProcess;
							TextBox->DataType = EStateKeyDataType::StringArray;
							TextBox->OnTextCommitted.AddDynamic(TextBox, &UStateKeyInputTextBox::OnTextValueCommitted);
							InputWidget = TextBox;
						}

						if (InputWidget)
						{
							UHorizontalBoxSlot* ArraySlot = RowBox->AddChildToHorizontalBox(InputWidget);
							ArraySlot->SetPadding(FMargin(PaddingBetween, 0.0, 0.0, 0.0));
							ArraySlot->SetHorizontalAlignment(HAlign_Fill);
							ArraySlot->SetVerticalAlignment(VAlign_Center);
						}
					}

					UGridSlot* InputSlot = RowGrid->AddChildToGrid(RowBox, GridIdx, 1);
					InputSlot->SetPadding(FMargin(0.f, 5.f, 5.f, 5.f));
					InputSlot->SetHorizontalAlignment(Alignment);
					InputSlot->SetVerticalAlignment(VAlign_Center);
					InputWidget = nullptr;
					GridIdx++;
				}
				UpdateJsonObjectKey<TArray<FString>>(KeyName, WorkingArray, ModifiedStateObject);
				break;
			}
			case EStateKeyDataType::Number:
			{
				double CurrentVal = StateKeyInfo.DefaultNumberValue;
				if (StateManager)
					StateManager->GetCurrentStateValue<double>(KeyName, CurrentVal, CurrValFound);

				if (StateKeyInfo.bLimitValues)
				{
					UStateKeyInputComboBox* ComboBox = WidgetTree->ConstructWidget<UStateKeyInputComboBox>(UStateKeyInputComboBox::StaticClass());
					ComboBox->KeyName = KeyName;
					ComboBox->ParentDebugUI = this;
					ComboBox->InstantBroadcastChange = instantProcess;
					ComboBox->DataType = EStateKeyDataType::Number;
					ComboBox->StateKeyInfo = StateKeyInfo;
					for (double Option : StateKeyInfo.AcceptedNumberValues)
					{
						ComboBox->AddOption(FString::SanitizeFloat(Option));
					}

#if UNREAL_5_3_OR_NEWER
					ComboBox->SetItemStyle(RowStyle);
#endif

					ComboBox->SetSelectedOption(FString::SanitizeFloat(CurrentVal));
					ComboBox->OnSelectionChanged.AddDynamic(ComboBox, &UStateKeyInputComboBox::OnComboBoxChanged);
					InputWidget = ComboBox;
				}
				else
				{
					UStateKeyInputTextBox* NumberBox = WidgetTree->ConstructWidget<UStateKeyInputTextBox>(UStateKeyInputTextBox::StaticClass());
					NumberBox->SetText(FText::AsNumber(CurrentVal));
#if UNREAL_5_3_OR_NEWER
					NumberBox->WidgetStyle.TextStyle.ColorAndOpacity = BlackColor;
					NumberBox->WidgetStyle.TextStyle.SetFontSize(16.0f);
#endif
					NumberBox->KeyName = KeyName;
					NumberBox->ParentDebugUI = this;
					NumberBox->InstantBroadcastChange = instantProcess;
					NumberBox->DataType = EStateKeyDataType::Number;
					NumberBox->OnTextCommitted.AddDynamic(NumberBox, &UStateKeyInputTextBox::OnTextValueCommitted);
					InputWidget = NumberBox;
				}

				UpdateJsonObjectKey<double>(KeyName, CurrentVal, ModifiedStateObject);
				break;
			}
			case EStateKeyDataType::NumberArray:
			{
				TArray<double> CurrentVals;
				if (StateManager)
					StateManager->GetCurrentStateValue<TArray<double>>(KeyName, CurrentVals, CurrValFound);

				TArray<double> WorkingArray = CurrValFound ? CurrentVals : StateKeyInfo.DefaultNumberArray;

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
							for (double Option : StateKeyInfo.AcceptedNumberValues)
							{
								ComboBox->AddOption(FString::SanitizeFloat(Option));
							}

#if UNREAL_5_3_OR_NEWER
							ComboBox->SetItemStyle(RowStyle);
#endif

							ComboBox->SetSelectedOption(FString::SanitizeFloat(Value));
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
							NumberBox->OnTextCommitted.AddDynamic(NumberBox, &UStateKeyInputTextBox::OnTextValueCommitted);
							InputWidget = NumberBox;
						}

						if (InputWidget)
						{
							UHorizontalBoxSlot* ArraySlot = RowBox->AddChildToHorizontalBox(InputWidget);
							ArraySlot->SetPadding(FMargin(PaddingBetween, 0.0, 0.0, 0.0));
							ArraySlot->SetHorizontalAlignment(HAlign_Fill);
							ArraySlot->SetVerticalAlignment(VAlign_Center);
						}
					}

					UGridSlot* InputSlot = RowGrid->AddChildToGrid(RowBox, GridIdx, 1);
					InputSlot->SetPadding(FMargin(0.f, 5.f, 5.f, 5.f));
					InputSlot->SetHorizontalAlignment(Alignment);
					InputSlot->SetVerticalAlignment(VAlign_Center);
					InputWidget = nullptr;
					GridIdx++;
				}

				UpdateJsonObjectKey<TArray<double>>(KeyName, WorkingArray, ModifiedStateObject);
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
				const int32 ItemsPerRow = 4;
				const float PaddingBetween = 20.0f;

				int GridIdx = 0;

				for (int32 i = 0; i < NumValues; i += ItemsPerRow)
				{
					UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

					for (int32 j = 0; j < ItemsPerRow && (i + j) < NumValues; ++j)
					{
						bool Value = WorkingArray[i + j];
						InputWidget = nullptr;

						UStateKeyInputCheckBox* CheckBox = WidgetTree->ConstructWidget<UStateKeyInputCheckBox>(UStateKeyInputCheckBox::StaticClass());
						CheckBox->KeyName = KeyName + "_INDEX_" + FString::FromInt(i + j);
						CheckBox->ParentDebugUI = this;
						CheckBox->InstantBroadcastChange = instantProcess;
						CheckBox->SetIsChecked(Value);
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

						if (InputWidget)
						{
							UHorizontalBoxSlot* ArraySlot = RowBox->AddChildToHorizontalBox(InputWidget);
							ArraySlot->SetPadding(FMargin(PaddingBetween, 0.0, 0.0, 0.0));
							ArraySlot->SetHorizontalAlignment(HAlign_Right);
							ArraySlot->SetVerticalAlignment(VAlign_Center);
						}
					}

					UGridSlot* InputSlot = RowGrid->AddChildToGrid(RowBox, GridIdx, 1);
					InputSlot->SetPadding(FMargin(20.f, 5.f, 5.f, 5.f));
					InputSlot->SetHorizontalAlignment(Alignment);
					InputSlot->SetVerticalAlignment(VAlign_Center);
					InputWidget = nullptr;
					GridIdx++;
				}

				UpdateJsonObjectKey<TArray<bool>>(KeyName, WorkingArray, ModifiedStateObject);
				break;
			}
			default:
				break;
			}

			if (InputWidget)
			{
				UGridSlot* InputSlot = RowGrid->AddChildToGrid(InputWidget, 0, 1);
				InputSlot->SetPadding(FMargin(5.f));
				InputSlot->SetHorizontalAlignment(Alignment);
				InputSlot->SetVerticalAlignment(VAlign_Center);
			}

			ParentBox->AddChildToVerticalBox(RowGrid);
		}
	}
}
