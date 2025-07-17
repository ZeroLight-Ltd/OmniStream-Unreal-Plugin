// Copyright ZeroLight ltd. All Rights Reserved.

#if WITH_EDITOR

#include "ZLStateEditor.h"
#include "ZLCloudPluginVersion.h"

#if UNREAL_5_6_OR_NEWER
#include "UObject/SavePackage.h"
#endif

#define LOCTEXT_NAMESPACE "FZLStateEditor"

DEFINE_LOG_CATEGORY(LogZLStateEditor);

TMap<FString, FStateKeyInfo> FZLStateEditor::ConvertToSerializableMap(const TMap<FString, StateKeyInfo>& StateKeyInfoMap)
{
	TMap<FString, FStateKeyInfo> SerializableMap;

	for (const auto& Pair : StateKeyInfoMap)
	{
		const FString& Key = Pair.Key;
		const StateKeyInfo& Info = Pair.Value;

		FStateKeyInfo Serializable;

		Serializable.DataType = Info.dataType;
		Serializable.bLimitValues = Info.limitValues;

		for (const TSharedPtr<FJsonValue>& Accepted : Info.acceptedValues)
		{
			if (Accepted.IsValid())
			{
				if (Info.dataType == "String" || Info.dataType == "StringArray")
					Serializable.AcceptedStringValues.Add(Accepted->AsString());
				else if(Info.dataType == "Number" || Info.dataType == "NumberArray")
					Serializable.AcceptedNumberValues.Add(Accepted->AsNumber());
			}
		}

		if (Info.defaultValue.IsValid())
		{
			if (Info.dataType == "String")
			{
				Serializable.DefaultStringValue = Info.defaultValue->AsString();
			}
			else if (Info.dataType == "Number")
			{
				Serializable.DefaultNumberValue = static_cast<float>(Info.defaultValue->AsNumber());
			}
			else if (Info.dataType == "Bool")
			{
				Serializable.DefaultBoolValue = Info.defaultValue->AsBool();
			}
			else if (Info.dataType == "StringArray")
			{
				const TArray<TSharedPtr<FJsonValue>> Array = Info.defaultValue->AsArray();
				for (const auto& Item : Array)
				{
					Serializable.DefaultStringArray.Add(Item->AsString());
				}
			}
			else if (Info.dataType == "NumberArray")
			{
				const TArray<TSharedPtr<FJsonValue>> Array = Info.defaultValue->AsArray();
				for (const auto& Item : Array)
				{
					Serializable.DefaultNumberArray.Add(static_cast<float>(Item->AsNumber()));
				}
			}
			else if (Info.dataType == "BoolArray")
			{
				const TArray<TSharedPtr<FJsonValue>> Array = Info.defaultValue->AsArray();
				for (const auto& Item : Array)
				{
					Serializable.DefaultBoolArray.Add(Item->AsBool());
				}
			}
		}

		SerializableMap.Add(Key, Serializable);
	}

	return SerializableMap;
}

void FZLStateEditor::SaveAssetFromMap()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	TArray<FString> SavePath;
	const FString DefaultPath = FPaths::ProjectContentDir();
	const FString FileTypes = TEXT("Unreal Asset (*.uasset)|*.uasset");

	const bool bSave = DesktopPlatform->SaveFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Save State Schema Asset"),
		DefaultPath,
		TEXT("NewSchemaAsset"),
		FileTypes,
		EFileDialogFlags::None,
		SavePath
	);

	if (bSave)
	{
		const FString SaveFileName = FString(SavePath[0]);
		FString PackageName;
		FString AssetName;
		FPackageName::TryConvertFilenameToLongPackageName(SaveFileName, PackageName);
		AssetName = FPaths::GetBaseFilename(SaveFileName);

		if (!FPackageName::IsValidObjectPath(PackageName + TEXT(".") + AssetName))
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid package path: %s"), *PackageName);
			return;
		}

		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();

		UStateKeyInfoAsset* NewAsset = NewObject<UStateKeyInfoAsset>(Package, *AssetName, RF_Public | RF_Standalone);
		NewAsset->KeyInfos = ConvertToSerializableMap(keyInfos);

		FAssetRegistryModule::AssetCreated(NewAsset);
		NewAsset->MarkPackageDirty();

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

		#if UNREAL_5_6_OR_NEWER
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, NewAsset, *PackageFileName, SaveArgs);
		#else
			UPackage::SavePackage(Package, NewAsset, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *PackageFileName);
		#endif

		UE_LOG(LogTemp, Log, TEXT("Asset saved to: %s"), *PackageFileName);
	}
}

void FZLStateEditor::LoadFromUAsset()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	FString OpenPath;
	const FString DefaultPath = FPaths::ProjectContentDir();
	const FString FileTypes = TEXT("Unreal Asset (*.uasset)|*.uasset");

	TArray<FString> OutFiles;
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Open State Schema Asset"),
		DefaultPath,
		TEXT(""),
		FileTypes,
		EFileDialogFlags::None,
		OutFiles
	);

	if (bOpened && OutFiles.Num() > 0)
	{
		FString FilePath = OutFiles[0];
		FString PackageName;
		if (!FPackageName::TryConvertFilenameToLongPackageName(FilePath, PackageName))
		{
			UE_LOG(LogTemp, Error, TEXT("Could not convert filename to package: %s"), *FilePath);
			return;
		}

		FString AssetName = FPaths::GetBaseFilename(FilePath);

		UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		if (!Package)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to load package: %s"), *PackageName);
			return;
		}

		UStateKeyInfoAsset* LoadedAsset = FindObject<UStateKeyInfoAsset>(Package, *AssetName);
		if (!LoadedAsset)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to find asset in package: %s (make sure this is a schema .uasset file)"), *AssetName);
			return;
		}


		keyInfos = ConvertToEditorMap(LoadedAsset->KeyInfos, ActiveJsonObject);
		UpdateJsonStr(); //Refresh editor, maybe use invalidate call here?
		UpdateJsonData(s_currentJsonStr);

		UE_LOG(LogTemp, Log, TEXT("Loaded %d keys from asset."), keyInfos.Num());
	}
}

TMap<FString, StateKeyInfo> FZLStateEditor::ConvertToEditorMap(const TMap<FString, FStateKeyInfo>& SavedAsset, TSharedPtr<FJsonObject>& OutJsonObject)
{
	OutJsonObject = MakeShared<FJsonObject>();

	TMap<FString, StateKeyInfo> RuntimeMap;

	for (const auto& Pair : SavedAsset)
	{
		const FString& FullKey = Pair.Key;
		const FStateKeyInfo& Info = Pair.Value;

		StateKeyInfo RuntimeInfo;
		RuntimeInfo.dataType = Info.DataType;
		RuntimeInfo.limitValues = Info.bLimitValues;

		TSharedPtr<FJsonValue> JsonValue;

		if (Info.DataType == "String")
		{
			RuntimeInfo.defaultValue = MakeShared<FJsonValueString>(Info.DefaultStringValue);
			JsonValue = RuntimeInfo.defaultValue;

			for (const FString& Val : Info.AcceptedStringValues)
			{
				RuntimeInfo.acceptedValues.Add(MakeShared<FJsonValueString>(Val));
			}
		}
		else if (Info.DataType == "Number")
		{
			RuntimeInfo.defaultValue = MakeShared<FJsonValueNumber>(Info.DefaultNumberValue);
			JsonValue = RuntimeInfo.defaultValue;

			for (const float Val : Info.AcceptedNumberValues)
			{
				RuntimeInfo.acceptedValues.Add(MakeShared<FJsonValueNumber>(Val));
			}
		}
		else if (Info.DataType == "Bool")
		{
			RuntimeInfo.defaultValue = MakeShared<FJsonValueBoolean>(Info.DefaultBoolValue);
			JsonValue = RuntimeInfo.defaultValue;
		}
		else if (Info.DataType == "StringArray")
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FString& Val : Info.DefaultStringArray)
				Arr.Add(MakeShared<FJsonValueString>(Val));
			RuntimeInfo.defaultValue = MakeShared<FJsonValueArray>(Arr);
			JsonValue = RuntimeInfo.defaultValue;

			for (const FString& Val : Info.AcceptedStringValues)
			{
				RuntimeInfo.acceptedValues.Add(MakeShared<FJsonValueString>(Val));
			}
		}
		else if (Info.DataType == "NumberArray")
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (float Val : Info.DefaultNumberArray)
				Arr.Add(MakeShared<FJsonValueNumber>(Val));
			RuntimeInfo.defaultValue = MakeShared<FJsonValueArray>(Arr);
			JsonValue = RuntimeInfo.defaultValue;

			for (const float Val : Info.AcceptedNumberValues)
			{
				RuntimeInfo.acceptedValues.Add(MakeShared<FJsonValueNumber>(Val));
			}
		}
		else if (Info.DataType == "BoolArray")
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (bool Val : Info.DefaultBoolArray)
				Arr.Add(MakeShared<FJsonValueBoolean>(Val));
			RuntimeInfo.defaultValue = MakeShared<FJsonValueArray>(Arr);
			JsonValue = RuntimeInfo.defaultValue;
		}

		RuntimeMap.Add(FullKey, RuntimeInfo);

		if (JsonValue.IsValid())
		{
			TArray<FString> Parts;
			FullKey.ParseIntoArray(Parts, TEXT("."));

			TSharedPtr<FJsonObject> CurrentObj = OutJsonObject;
			for (int32 i = 0; i < Parts.Num(); ++i)
			{
				const FString& Part = Parts[i];

				if (i == Parts.Num() - 1)
				{
					CurrentObj->SetField(Part, JsonValue);
				}
				else
				{
					TSharedPtr<FJsonObject> NextObj;
					TSharedPtr<FJsonValue> Existing = CurrentObj->TryGetField(Part);

					if (Existing.IsValid() && Existing->Type == EJson::Object)
					{
						NextObj = Existing->AsObject();
					}
					else
					{
						NextObj = MakeShared<FJsonObject>();
						CurrentObj->SetObjectField(Part, NextObj);
					}

					CurrentObj = NextObj;
				}
			}
		}
	}

	return RuntimeMap;
}

void FZLStateEditor::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SSplitter)
						.Orientation(Orient_Horizontal)
						+ SSplitter::Slot()
						.Value(0.35f)
						[
							SNew(SSplitter)
								.Orientation(Orient_Vertical)
								+ SSplitter::Slot()
								.Value(0.75f)
								[
									SNew(SVerticalBox)
										+ SVerticalBox::Slot()
										.FillHeight(0.5f)
										[
											SNew(SBox)
												.HeightOverride(300)
												[
													SNew(SScrollBox)
														+ SScrollBox::Slot()
														[
															SAssignNew(JsonTextBox, SMultiLineEditableTextBox)
															.Text_Lambda([this] {
																return FText::FromString(s_currentJsonStr);
															})
															.OnTextChanged(this, &FZLStateEditor::OnJsonTextChanged)
														]
												]
										]
								]
								+ SSplitter::Slot()
								.Resizable(false)
								.Value(0.175f)
								[
									SNew(SVerticalBox)
										+ SVerticalBox::Slot()
										.FillHeight(0.5f)
										[
											SNew(SVerticalBox)
											/*+ SVerticalBox::Slot().Padding(0,0,0,4).AutoHeight()[SNew(SButton)
											.Text(FText::FromString("Validate JSON Schema Keys")
											)]*/

											+ SVerticalBox::Slot().Padding(0,0,0,4).AutoHeight()[SNew(SButton)
											.OnClicked_Lambda([this] {
												if(keyInfos.Num() > 0)
													this->SaveAssetFromMap();
												return FReply::Handled();
											})
											.Text(FText::FromString("Save Schema")
											)]
											
												
											+ SVerticalBox::Slot().Padding(0,0,0,4).AutoHeight()[SNew(SButton)
											.OnClicked_Lambda([this] {
												this->LoadFromUAsset();
												return FReply::Handled();
											})
											.Text(FText::FromString("Load Schema")
											)]
										]
								]
						]
						+ SSplitter::Slot()
						.Value(0.65f)
						[
							SNew(SSplitter)
							.Orientation(Orient_Vertical)
							+ SSplitter::Slot()
							.Resizable(false)
							.Value(0.065f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.MaxHeight(25.0f)
								[
									SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.Padding(0,2,0,0)
										.FillWidth(0.7f)
										[
											SNew(SEditableTextBox)
											.Text_Lambda([this] {
												return FText::FromString(newKeyStr); 
											})
											.OnTextChanged_Lambda([this](const FText & NewText){
												newKeyStr = NewText.ToString();
											})
										]
										+ SHorizontalBox::Slot()
										.FillWidth(0.3f)
										[
											SNew(SButton)
											.Text(FText::FromString("Add New Key"))
											.OnClicked_Lambda([this]()
											{
												if (!newKeyStr.IsEmpty())
												{
													if (!this->keyInfos.Contains(newKeyStr))
													{
														this->keyInfos.Add(newKeyStr);
														this->keyInfos[newKeyStr].dataType = "String";
														this->keyInfos[newKeyStr].defaultValue = MakeShared<FJsonValueString>("");

														if(!AreaExpansionStates.Contains(newKeyStr))
															AreaExpansionStates.Add(newKeyStr, TPair<bool, bool>(true, true));
														else
															AreaExpansionStates[newKeyStr].Key = true;

														TArray<FString> Parts;
														newKeyStr.ParseIntoArray(Parts, TEXT("."));

														if (Parts.Num() > 0)
														{
															TSharedPtr<FJsonObject> CurrentObj = this->ActiveJsonObject;

															for (int32 i = 0; i < Parts.Num(); ++i)
															{
																const FString& Part = Parts[i];

																if (i == Parts.Num() - 1)
																{
																	CurrentObj->SetField(Part, MakeShared<FJsonValueString>(""));
																}
																else
																{
																	TSharedPtr<FJsonObject> NextObj;

																	if (TSharedPtr<FJsonValue> ExistingValue = CurrentObj->TryGetField(Part))
																	{
																		if (ExistingValue->Type == EJson::Object)
																		{
																			NextObj = ExistingValue->AsObject();
																		}
																	}

																	if (!NextObj.IsValid())
																	{
																		NextObj = MakeShared<FJsonObject>();
																		CurrentObj->SetObjectField(Part, NextObj);
																	}

																	CurrentObj = NextObj;
																}
															}
														}

														this->UpdateJsonStr();
														this->UpdateJsonData(s_currentJsonStr);
													}
												}

												return FReply::Handled();
											})
										]
								]
							]
							+ SSplitter::Slot()
							.Value(0.9f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot()
									.FillHeight(0.5f)
									[
										SAssignNew(ButtonsContainer, SVerticalBox)
									]
								]
							]
						]
				]
		];

		if (s_currentJsonStr != "")
			UpdateJsonData(s_currentJsonStr);
}

void FZLStateEditor::OnJsonTextChanged(const FText& NewText)
{
	FString JsonString = NewText.ToString();
	s_currentJsonStr = JsonString;
	UpdateJsonData(JsonString);
	RemoveInvalidKeyInfoEntries();
}

void FZLStateEditor::RemoveInvalidKeyInfoEntries()
{
	TArray<FString> keysToTrim;
	for (const auto& Pair : keyInfos)
	{
		const FString& FullKey = Pair.Key;
		const StateKeyInfo& Info = Pair.Value;

		TArray<FString> Parts;
		FullKey.ParseIntoArray(Parts, TEXT("."), true);

		TSharedPtr<FJsonObject> CurrentObject = ActiveJsonObject;

		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const FString& Part = Parts[i];

			if (!CurrentObject->HasField(Part))
			{
				keysToTrim.Add(FullKey);
			}

			if (i == Parts.Num() - 1)
			{
				continue;
			}

			TSharedPtr<FJsonObject> NextObject = CurrentObject->GetObjectField(Part);
			if (!NextObject.IsValid())
			{
				keysToTrim.Add(FullKey);
			}

			CurrentObject = NextObject;
		}
	}

	for (int i = 0; i < keysToTrim.Num(); i++)
		keyInfos.Remove(keysToTrim[i]);
}

void FZLStateEditor::UpdateJsonStr()
{
	FString JsonString_UpdatedSchema;

	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString_UpdatedSchema);
	FJsonSerializer::Serialize(ActiveJsonObject.ToSharedRef(), Writer);
	s_currentJsonStr = JsonString_UpdatedSchema;
	JsonTextBox->SetText(FText::FromString(JsonString_UpdatedSchema));

	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

void FZLStateEditor::UpdateJsonData(const FString& JsonString)
{
	ButtonsContainer->ClearChildren();
	TSharedPtr<FJsonObject> JsonDataObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (FJsonSerializer::Deserialize(Reader, JsonDataObject) && JsonDataObject.IsValid())
	{
		GenerateUIFromJson(JsonDataObject, "");
	}
}

void FZLStateEditor::GenerateUIFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& Prefix, const bool SetActiveObject)
{
	if(SetActiveObject)
		this->ActiveJsonObject = JsonObject;

	for (const auto& Pair : JsonObject->Values)
	{
		FString Key = Prefix.IsEmpty() ? Pair.Key : Prefix + TEXT(".") + Pair.Key;
		if (Pair.Value->Type == EJson::Object)
		{
			GenerateUIFromJson(Pair.Value->AsObject(), Key, false);
		}
		else
		{
			if (!keyInfos.Contains(Key))
			{
				keyInfos.Add(Key);

				if (Pair.Value->Type == EJson::String && s_DataTypes.ContainsByPredicate([&Pair](const TSharedPtr<FString>& DataType)
				{
					return DataType.IsValid() && *DataType == Pair.Value->AsString();
				}))
					keyInfos[Key].dataType = Pair.Value->AsString();
				else
				{
					switch (Pair.Value->Type)
					{
					case EJson::Number:
						keyInfos[Key].dataType = "Number";
						keyInfos[Key].defaultValue = MakeShared<FJsonValueNumber>(Pair.Value->AsNumber());
						break;
					case EJson::String:
						keyInfos[Key].dataType = "String";
						keyInfos[Key].defaultValue = MakeShared<FJsonValueString>(Pair.Value->AsString());
						break;
					case EJson::Boolean:
						keyInfos[Key].dataType = "Bool";
						keyInfos[Key].defaultValue = MakeShared<FJsonValueBoolean>(Pair.Value->AsBool());
						break;
					case EJson::Array:
						keyInfos[Key].dataType = "StringArray";
						keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(Pair.Value->AsArray());
						break;
					default:
						UE_LOG(LogZLStateEditor, Log, TEXT("Unknown data type in %s schema key"), *Key)
						continue;
					}
				}
			}

			TSharedPtr<SVerticalBox> ArrayBox = SNew(SVerticalBox);
			TSharedPtr<SVerticalBox> AcceptedValuesArrayBox = SNew(SVerticalBox);

			bool bIsExpanded = AreaExpansionStates.Contains(Key) ? AreaExpansionStates[Key].Key : false;
			bool bIsAdvancedExpanded = AreaExpansionStates.Contains(Key) ? AreaExpansionStates[Key].Value : false;

			ButtonsContainer->AddSlot()
			.AutoHeight()
			.Padding(0,0,0,10)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(!bIsExpanded)
				.OnAreaExpansionChanged_Lambda([this, Key](bool bExpanded)
				{
					if (AreaExpansionStates.Contains(Key))
						AreaExpansionStates[Key].Key = bExpanded;
					else
						AreaExpansionStates.Add(Key, TPair<bool, bool>(bExpanded, false));
				})
				.BorderBackgroundColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
				.BodyBorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BodyBorderBackgroundColor(FLinearColor::White)
				.HeaderContent()
				[
					SNew(STextBlock).Text(FText::FromString(Key))
				]
				.BodyContent()
				[
					SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						.Padding(0,0,0,2)
						[
							SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth()
								.Padding(0,2,2,0)
								[
									SNew(STextBlock)
									.Text(FText::FromString("Type"))
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SNew(SComboBox<TSharedPtr<FString>>)
									.OptionsSource(&s_DataTypes)
									.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
									{
										return SNew(STextBlock).Text(FText::FromString(*Item));
									})
									.OnSelectionChanged_Lambda([this, Pair, Key, JsonObject](TSharedPtr<FString> NewSelection, ESelectInfo::Type)
									{
										keyInfos[Key].dataType = *NewSelection;
										keyInfos[Key].ResetDefaultValue();
										JsonObject->SetField(Pair.Key, keyInfos[Key].defaultValue);
										UpdateJsonStr();

										this->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
									})
									.Content()
									[
										SNew(STextBlock).Text_Lambda([this, Key]() 
										{ 
											return FText::FromString(keyInfos.Contains(Key) ? keyInfos[Key].dataType : "Select Type");
										})
									]
								]
						]
						+ SVerticalBox::Slot().AutoHeight()
						.Padding(0,4,0,0)
						[
							SNew(SExpandableArea)
							.InitiallyCollapsed(!bIsAdvancedExpanded)
							.OnAreaExpansionChanged_Lambda([this, Key](bool bExpanded)
							{
								if (AreaExpansionStates.Contains(Key))
									AreaExpansionStates[Key].Value = bExpanded;
								else
									AreaExpansionStates.Add(Key, TPair<bool, bool>(true, bExpanded));
							})
							.BorderBackgroundColor(FLinearColor(0.55f, 0.55f, 0.55f, 1.0f))
							.BodyBorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
							.BodyBorderBackgroundColor(FLinearColor::Gray)
							.HeaderContent()
							[
								SNew(STextBlock).Text(FText::FromString("Advanced"))
							]
							.BodyContent()
							[
								SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight()
									.Padding(0,0,0,2)
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().AutoWidth()
										.Padding(2,2,2,0)
										[
											SNew(STextBlock).Text(FText::FromString("Default Value"))
										]
										+ SHorizontalBox::Slot().FillWidth(0.8f)
										[
											SNew(SEditableTextBox)
											.Visibility_Lambda([this, Key] {
												return keyInfos[Key].dataType == "String" ? EVisibility::Visible : EVisibility::Collapsed;
											})
											.OnTextCommitted_Lambda([this, Pair, Key, JsonObject](const FText& NewText, ETextCommit::Type)
											{
												keyInfos[Key].defaultValue = MakeShared<FJsonValueString>(NewText.ToString());
												JsonObject->SetField(Pair.Key, keyInfos[Key].defaultValue);
												UpdateJsonStr();
											})
											.Text_Lambda([this, Key] {
												if (keyInfos[Key].dataType != "String")
													return FText::FromString("");

												if (keyInfos[Key].defaultValue == nullptr)
													return FText::FromString("");
												else
													return FText::FromString(keyInfos[Key].defaultValue->AsString());
											})
										]
										+ SHorizontalBox::Slot().FillWidth(0.8f)
										[
											SNew(SNumericEntryBox<double>)
										#if UNREAL_5_2_OR_NEWER
											.MaxFractionalDigits(3)
										#endif
											.Visibility_Lambda([this, Key] {
												return keyInfos[Key].dataType == "Number" ? EVisibility::Visible : EVisibility::Collapsed;
											})
											.OnValueCommitted_Lambda([this, Pair, Key, JsonObject](const double NewValue, ETextCommit::Type)
											{
												FString FormattedString = FString::Printf(TEXT("%.3f"), NewValue);
												double RoundedValue = FMath::RoundToDouble(NewValue * 1000.0f) / 1000.0f;
												
												keyInfos[Key].defaultValue = MakeShared<FJsonValueNumber>(RoundedValue);
												JsonObject->SetNumberField(Pair.Key, RoundedValue);
												UpdateJsonStr();
											})
											.Value_Lambda([this, Key] {
												if (keyInfos[Key].dataType != "Number")
													return double(0.0f);

												return (keyInfos[Key].defaultValue != nullptr) ? keyInfos[Key].defaultValue->AsNumber() : double(0.0f);
											})
										]
										+ SHorizontalBox::Slot().AutoWidth()
										[
											SNew(SCheckBox)
											.Visibility_Lambda([this, Key] {
												return keyInfos[Key].dataType == "Bool" ? EVisibility::Visible : EVisibility::Collapsed;
											})
											.OnCheckStateChanged_Lambda([this, Pair, Key, JsonObject](const ECheckBoxState state)
											{
												keyInfos[Key].defaultValue = MakeShared<FJsonValueBoolean>(state == ECheckBoxState::Checked ? true : false);
												JsonObject->SetField(Pair.Key, keyInfos[Key].defaultValue);
												UpdateJsonStr();
											})
											.IsChecked_Lambda([this, Key] {
												if (keyInfos[Key].defaultValue != nullptr)
													return keyInfos[Key].defaultValue->AsBool() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
												return ECheckBoxState::Unchecked;
											})
										]									
									]
									+ SVerticalBox::Slot().AutoHeight()
									[
										SAssignNew(ArrayBox, SVerticalBox)
									]
									+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(SCheckBox)
										.Visibility_Lambda([this, Key] {
											return keyInfos[Key].AllowsLimitValues() ? EVisibility::Visible : EVisibility::Hidden;
										})
										.OnCheckStateChanged_Lambda([this, Key](ECheckBoxState NewState)
										{
											keyInfos[Key].limitValues = (NewState == ECheckBoxState::Checked) ? true : false;
											if (keyInfos[Key].acceptedValues.IsEmpty())
												keyInfos[Key].acceptedValues.Add(keyInfos[Key].defaultValue);
											
											this->UpdateJsonData(s_currentJsonStr);
										})
										.IsChecked_Lambda([this, Key] {
											return keyInfos[Key].limitValues ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
										})
										.Content()
										[
											SNew(STextBlock).Text(FText::FromString("Limit Accepted Values"))
										]
									]
									+ SVerticalBox::Slot().AutoHeight()
									[
										SAssignNew(AcceptedValuesArrayBox, SVerticalBox)
										.Visibility_Lambda([this, Key] {
											return (keyInfos[Key].limitValues) ? EVisibility::Visible : EVisibility::Hidden;
										})
									]
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						.Padding(0,4,0,0)
						[
							SNew(SButton)
							.Text(FText::FromString("Remove Key"))
							.HAlign(EHorizontalAlignment::HAlign_Center)
							.OnClicked_Lambda([this, Pair, Key, JsonObject]()
							{
								this->keyInfos.Remove(Key);

								JsonObject->RemoveField(Pair.Key);

								this->UpdateJsonStr();
								this->UpdateJsonData(s_currentJsonStr);


								return FReply::Handled();
							})
						]
				]
			];

			if (keyInfos[Key].IsArray())
			{
				if (keyInfos[Key].defaultValue.IsValid() && keyInfos[Key].defaultValue->Type == EJson::Array)
				{
					keyInfos[Key].defaultValueArray = keyInfos[Key].defaultValue->AsArray();
				}
				else
				{
					keyInfos[Key].defaultValueArray = TArray<TSharedPtr<FJsonValue>>();
					keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
				}

				if (keyInfos[Key].defaultValueArray.Num() > 0)
				{
					for (int32 i = 0; i < keyInfos[Key].defaultValueArray.Num(); ++i)
					{
						TSharedPtr<FJsonValue> ValuePtr = (keyInfos[Key].defaultValueArray)[i];

						if (keyInfos[Key].dataType == "StringArray")
						{
							ArrayBox->AddSlot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									[
										SNew(SEditableTextBox)
										.Text(FText::FromString(ValuePtr->AsString()))
										.OnTextCommitted_Lambda([this, JsonObject, Pair, i, Key](const FText& NewText, ETextCommit::Type)
										{
											keyInfos[Key].defaultValueArray[i] = MakeShared<FJsonValueString>(NewText.ToString());
											keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
											JsonObject->SetArrayField(Pair.Key, keyInfos[Key].defaultValueArray);
											UpdateJsonStr();
										})
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(4, 0)
									[
										SNew(SButton)
										.Text(FText::FromString("X"))
										.OnClicked_Lambda([this, JsonObject, Pair, i, Key]()
										{
											if (keyInfos[Key].defaultValueArray.IsValidIndex(i))
											{
												keyInfos[Key].defaultValueArray.RemoveAt(i);
												keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
												JsonObject->SetArrayField(Pair.Key, keyInfos[Key].defaultValueArray);
												UpdateJsonStr();
											}
											return FReply::Handled();
										})
									]
								];
						}
						else if (keyInfos[Key].dataType == "NumberArray")
						{
							ArrayBox->AddSlot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									[
										SNew(SNumericEntryBox<float>)
										.Value(ValuePtr->AsNumber())
										.OnValueCommitted_Lambda([this, JsonObject, Pair, i, Key](float NewValue, ETextCommit::Type)
										{
											FString FormattedString = FString::Printf(TEXT("%.3f"), NewValue);
											double RoundedValue = FMath::RoundToDouble(NewValue * 1000.0f) / 1000.0f;

											keyInfos[Key].defaultValueArray[i] = MakeShared<FJsonValueNumber>(RoundedValue);
											keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
											JsonObject->SetArrayField(Pair.Key, keyInfos[Key].defaultValueArray);
											UpdateJsonStr();
										})
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(4, 0)
									[
										SNew(SButton)
										.Text(FText::FromString("X"))
										.OnClicked_Lambda([this, JsonObject, Pair, i, Key]()
										{
											if (keyInfos[Key].defaultValueArray.IsValidIndex(i))
											{
												keyInfos[Key].defaultValueArray.RemoveAt(i);
												keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
												JsonObject->SetArrayField(Pair.Key, keyInfos[Key].defaultValueArray);
												UpdateJsonStr();
											}
											return FReply::Handled();
										})
									]
								];
						}
						else if (keyInfos[Key].dataType == "BoolArray")
						{
							ArrayBox->AddSlot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									[
										SNew(SCheckBox)
										.IsChecked(ValuePtr->AsBool() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
										.OnCheckStateChanged_Lambda([this, JsonObject, Pair, i, Key](ECheckBoxState NewState)
										{
											keyInfos[Key].defaultValueArray[i] = MakeShared<FJsonValueBoolean>(NewState == ECheckBoxState::Checked);
											keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
											JsonObject->SetArrayField(Pair.Key, keyInfos[Key].defaultValueArray);
											UpdateJsonStr();
										})
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(4, 0)
									[
										SNew(SButton)
											.Text(FText::FromString("X"))
											.OnClicked_Lambda([this, JsonObject, Pair, i, Key]()
										{
											if (keyInfos[Key].defaultValueArray.IsValidIndex(i))
											{
												keyInfos[Key].defaultValueArray.RemoveAt(i);
												keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
												JsonObject->SetArrayField(Pair.Key, keyInfos[Key].defaultValueArray);
												UpdateJsonStr();
											}
											return FReply::Handled();
										})
									]
								];
						}

					}
				}

				ArrayBox->AddSlot()
				.AutoHeight()
				.Padding(0, 4)
				[
					SNew(SButton)
					.Text(FText::FromString("Add Array Default Value"))
					.OnClicked_Lambda([this, JsonObject, Pair, Key]()
					{
						if (keyInfos[Key].dataType == "StringArray")
						{
							keyInfos[Key].defaultValueArray.Add(MakeShared<FJsonValueString>(""));
						}
						else if (keyInfos[Key].dataType == "NumberArray")
						{
							keyInfos[Key].defaultValueArray.Add(MakeShared<FJsonValueNumber>(0.0f));
						}
						else if (keyInfos[Key].dataType == "BoolArray")
						{
							keyInfos[Key].defaultValueArray.Add(MakeShared<FJsonValueBoolean>(false));
						}

						keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
						JsonObject->SetArrayField(Pair.Key, keyInfos[Key].defaultValueArray);

						UpdateJsonStr();

						return FReply::Handled();
					})
				];
			}

			if (keyInfos[Key].AllowsLimitValues() && keyInfos[Key].limitValues)
			{
				if (keyInfos[Key].acceptedValues.Num() > 0)
				{
					for (int32 i = 0; i < keyInfos[Key].acceptedValues.Num(); ++i)
					{
						TSharedPtr<FJsonValue> ValuePtr = (keyInfos[Key].acceptedValues)[i];

						if (keyInfos[Key].dataType == "StringArray" || keyInfos[Key].dataType == "String")
						{
							AcceptedValuesArrayBox->AddSlot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.FillWidth(1.0f)
										[
											SNew(SEditableTextBox)
												.Text(FText::FromString(ValuePtr->AsString()))
												.OnTextCommitted_Lambda([this, JsonObject, Pair, i, Key](const FText& NewText, ETextCommit::Type)
											{
												keyInfos[Key].acceptedValues[i] = MakeShared<FJsonValueString>(NewText.ToString());
												this->UpdateJsonData(s_currentJsonStr);
											})
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.VAlign(VAlign_Center)
										.Padding(4, 0)
										[
											SNew(SButton)
											.Text(FText::FromString("X"))
											.OnClicked_Lambda([this, JsonObject, Pair, i, Key]()
											{
												if (keyInfos[Key].acceptedValues.IsValidIndex(i))
												{
													keyInfos[Key].acceptedValues.RemoveAt(i);
													this->UpdateJsonData(s_currentJsonStr);
												}
												return FReply::Handled();
											})
										]
								];
						}
						else if (keyInfos[Key].dataType == "NumberArray" || keyInfos[Key].dataType == "Number")
						{
							AcceptedValuesArrayBox->AddSlot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.FillWidth(1.0f)
										[
											SNew(SNumericEntryBox<float>)
											.Value(ValuePtr->AsNumber())
											.OnValueCommitted_Lambda([this, JsonObject, Pair, i, Key](float NewValue, ETextCommit::Type)
											{
												FString FormattedString = FString::Printf(TEXT("%.3f"), NewValue);
												double RoundedValue = FMath::RoundToDouble(NewValue * 1000.0f) / 1000.0f;

												keyInfos[Key].acceptedValues[i] = MakeShared<FJsonValueNumber>(RoundedValue);
												this->UpdateJsonData(s_currentJsonStr);
											})
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.VAlign(VAlign_Center)
										.Padding(4, 0)
										[
											SNew(SButton)
												.Text(FText::FromString("X"))
												.OnClicked_Lambda([this, JsonObject, Pair, i, Key]()
											{
												if (keyInfos[Key].acceptedValues.IsValidIndex(i))
												{
													keyInfos[Key].acceptedValues.RemoveAt(i);
													this->UpdateJsonData(s_currentJsonStr);
												}
												return FReply::Handled();
											})
										]
								];
						}
					}
				}

				AcceptedValuesArrayBox->AddSlot()
					.AutoHeight()
					.Padding(0, 4)
					[
						SNew(SButton)
						.Text(FText::FromString("Add Accepted Value"))
						.OnClicked_Lambda([this, JsonObject, Pair, Key]()
						{
							if (keyInfos[Key].dataType == "StringArray" || keyInfos[Key].dataType == "String")
							{
								keyInfos[Key].acceptedValues.Add(MakeShared<FJsonValueString>(""));
							}
							else if (keyInfos[Key].dataType == "NumberArray" || keyInfos[Key].dataType == "Number")
							{
								keyInfos[Key].acceptedValues.Add(MakeShared<FJsonValueNumber>(0.0f));
							}

							this->UpdateJsonData(s_currentJsonStr);

							return FReply::Handled();
						})
					];
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

#endif
