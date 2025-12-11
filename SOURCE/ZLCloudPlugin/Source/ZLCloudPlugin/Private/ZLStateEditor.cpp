// Copyright ZeroLight ltd. All Rights Reserved.

#if WITH_EDITOR

#include "ZLStateEditor.h"
#include "ZLCloudPluginVersion.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "LevelSequence.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

#if UNREAL_5_6_OR_NEWER
#include "UObject/SavePackage.h"
#endif
#include <EditorZLCloudPluginSettings.h>

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
		Serializable.bIgnoredInDataHashes = Info.ignoredInDataHash;

		for (const TSharedPtr<FJsonValue>& Accepted : Info.acceptedValues)
		{
			if (Accepted.IsValid())
			{
				if (Info.dataType == "String" || Info.dataType == "StringArray")
					Serializable.AcceptedStringValues.Add(Accepted->AsString());
				else if (Info.dataType == "Number" || Info.dataType == "NumberArray")
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
	FString DefaultPath = FPaths::ProjectContentDir();
	FString DefaultFileName = TEXT("NewSchemaAsset");

	// Use last loaded schema path if available
	if (!lastOpenSchemaAssetPath.IsEmpty())
	{
		FString LastPathDir = FPaths::GetPath(lastOpenSchemaAssetPath);
		FString LastFileName = FPaths::GetBaseFilename(lastOpenSchemaAssetPath);

		if (!LastPathDir.IsEmpty())
		{
			DefaultPath = LastPathDir;
		}
		if (!LastFileName.IsEmpty())
		{
			DefaultFileName = LastFileName;
		}
	}

	const FString FileTypes = TEXT("Unreal Asset (*.uasset)|*.uasset");

	const bool bSave = DesktopPlatform->SaveFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Save State Schema Asset"),
		DefaultPath,
		DefaultFileName,
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

		// Store the saved file path for next save dialog default
		lastOpenSchemaAssetPath = SaveFileName;

		// Also save to .zlschema JSON file
		FString SchemaFileName = FPaths::GetPath(SaveFileName) / (AssetName + TEXT(".zlschema"));

		FString JsonOutputString;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonOutputString);

		if (FJsonSerializer::Serialize(NewAsset->SerializeStateKeyAsset_JsonSchemaCompliant(), JsonWriter))
		{
			if (FFileHelper::SaveStringToFile(JsonOutputString, *SchemaFileName))
			{
				UE_LOG(LogTemp, Log, TEXT("Schema JSON saved to: %s"), *SchemaFileName);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to save schema JSON file: %s"), *SchemaFileName);
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to serialize JSON for schema file"));
		}
	}
}

void FZLStateEditor::LoadFromUAsset()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	FString OpenPath;
	const FString DefaultPath = FPaths::ProjectContentDir();
	const FString FileTypes = TEXT("Schema Files (*.uasset;*.zlschema)|*.uasset;*.zlschema|Unreal Asset (*.uasset)|*.uasset|Schema JSON (*.zlschema)|*.zlschema");

	TArray<FString> OutFiles;
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Open State Schema"),
		DefaultPath,
		TEXT(""),
		FileTypes,
		EFileDialogFlags::None,
		OutFiles
	);

	if (bOpened && OutFiles.Num() > 0)
	{
		FString FilePath = OutFiles[0];
		FString FileExtension = FPaths::GetExtension(FilePath).ToLower();

		if (FileExtension == TEXT("zlschema"))
		{
			// Load from JSON file
			FString JsonString;
			if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to read schema JSON file: %s"), *FilePath);
				return;
			}

			// Parse JSON
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
			if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to parse JSON from schema file: %s"), *FilePath);
				return;
			}

			LoadFromJsonSchema(JsonObject);

			// Store the loaded file path for save dialog default
			lastOpenSchemaAssetPath = FilePath;

			UE_LOG(LogTemp, Log, TEXT("Loaded schema from JSON file: %s"), *FilePath);
		}
		else if (FileExtension == TEXT("uasset"))
		{
			// Load from uasset file
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
				UE_LOG(LogTemp, Error, TEXT("Failed to find asset in package: %s (make sure this is a schema .uasset or .zlschema file)"), *AssetName);
				return;
			}

			keyInfos = ConvertToEditorMap(LoadedAsset->KeyInfos, ActiveJsonObject);
			UpdateJsonStr(); //Refresh editor, maybe use invalidate call here?
			UpdateJsonData(s_currentJsonStr);

			// Store the loaded file path for save dialog default
			lastOpenSchemaAssetPath = FilePath;

			UE_LOG(LogTemp, Log, TEXT("Loaded %d keys from asset."), keyInfos.Num());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Unsupported file type: %s (expected .uasset or .zlschema)"), *FileExtension);
		}
	}
}

TSharedPtr<FJsonValue> ParseSchemaAndBuildDataRecursive(
	const TSharedPtr<FJsonObject>& SchemaNode,
	FString CurrentPath,
	TMap<FString, StateKeyInfo>& OutMap)
{
	if (!SchemaNode.IsValid()) return MakeShared<FJsonValueNull>();

	FString JsonType = SchemaNode->GetStringField("type");

	if (JsonType == "object" || SchemaNode->HasField("properties"))
	{
		TSharedPtr<FJsonObject> DataObject = MakeShared<FJsonObject>();

		const TSharedPtr<FJsonObject>* PropsObject;
		if (SchemaNode->TryGetObjectField("properties", PropsObject))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PropsObject)->Values)
			{
				FString KeyName = Pair.Key;
				FString NewPath = CurrentPath.IsEmpty() ? KeyName : CurrentPath + "." + KeyName;

				TSharedPtr<FJsonValue> ChildValue = ParseSchemaAndBuildDataRecursive(Pair.Value->AsObject(), NewPath, OutMap);

				if (ChildValue.IsValid())
				{
					DataObject->SetField(KeyName, ChildValue);
				}
			}
		}

		return MakeShared<FJsonValueObject>(DataObject);
	}

	StateKeyInfo NewInfo;

	if (JsonType == "string") NewInfo.dataType = "String";
	else if (JsonType == "number" || JsonType == "integer") NewInfo.dataType = "Number";
	else if (JsonType == "boolean") NewInfo.dataType = "Bool";
	else if (JsonType == "array")
	{
		const TSharedPtr<FJsonObject>* ItemsObj;
		if (SchemaNode->TryGetObjectField("items", ItemsObj))
		{
			FString ItemType = (*ItemsObj)->GetStringField("type");
			if (ItemType == "string") NewInfo.dataType = "StringArray";
			else if (ItemType == "number" || ItemType == "integer") NewInfo.dataType = "NumberArray";
			else if (ItemType == "boolean") NewInfo.dataType = "BoolArray";
		}
	}

	if (NewInfo.dataType == "Select Type") return MakeShared<FJsonValueNull>();

	const TArray<TSharedPtr<FJsonValue>>* EnumArray = nullptr;
	if (SchemaNode->TryGetArrayField("enum", EnumArray)) NewInfo.limitValues = true;
	else if (JsonType == "array" && SchemaNode->HasField("items"))
	{
		TSharedPtr<FJsonObject> Items = SchemaNode->GetObjectField("items");
		if (Items->TryGetArrayField("enum", EnumArray)) NewInfo.limitValues = true;
	}

	if (NewInfo.limitValues && EnumArray) NewInfo.acceptedValues = *EnumArray;

	if (SchemaNode->HasField("default"))
	{
		TSharedPtr<FJsonValue> DefVal = SchemaNode->TryGetField("default");
		NewInfo.defaultValue = DefVal;

		if (NewInfo.IsArray())
		{
			NewInfo.defaultValueArray = DefVal->AsArray();
		}
	}
	else
	{
		NewInfo.ResetDefaultValue();
	}

	OutMap.Add(CurrentPath, NewInfo);

	return NewInfo.defaultValue;
}

void FZLStateEditor::LoadFromJsonSchema(TSharedPtr<FJsonObject> Schema)
{
	if (!Schema.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("LoadFromJsonSchema: Invalid Schema Object"));
		return;
	}

	keyInfos.Empty();
	ActiveJsonObject.Reset();
	TSharedPtr<FJsonValue> ResultValue = ParseSchemaAndBuildDataRecursive(Schema, "", keyInfos);

	if (ResultValue.IsValid() && ResultValue->Type == EJson::Object)
	{
		ActiveJsonObject = ResultValue->AsObject();
		UE_LOG(LogTemp, Log, TEXT("Generated Data Object with %d keys."), keyInfos.Num());
	}
	else
	{
		ActiveJsonObject = MakeShared<FJsonObject>();
	}

	UpdateJsonStr();
	UpdateJsonData(s_currentJsonStr);

	UE_LOG(LogTemp, Log, TEXT("Loaded %d keys from JSON Schema"), keyInfos.Num());
}

TMap<FString, StateKeyInfo> FZLStateEditor::ConvertToEditorMap(const TMap<FString, FStateKeyInfo>& SavedAsset, TSharedPtr<FJsonObject>& OutJsonObject)
{
	OutJsonObject = MakeShared<FJsonObject>();

	TMap<FString, StateKeyInfo> RuntimeMap;

	for (const auto& Pair : SavedAsset)
	{
		const FString& FullKey = Pair.Key;
		const FStateKeyInfo& Info = Pair.Value;

		if (FullKey.EndsWith("."))
		{
			UE_LOG(LogZLStateEditor, Warning, TEXT("Invalid formatted subkey %s skipped"), *FullKey)
				continue;
		}

		StateKeyInfo RuntimeInfo;
		RuntimeInfo.dataType = Info.DataType;
		RuntimeInfo.limitValues = Info.bLimitValues;
		RuntimeInfo.ignoredInDataHash = Info.bIgnoredInDataHashes;

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
	PopulateAutoPopulateOptions();

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
								.Value(0.725f)
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
								.Value(0.2f)
								[
									SNew(SVerticalBox)
										+ SVerticalBox::Slot()
										.FillHeight(0.5f)
										[
											SNew(SVerticalBox)
												/*+ SVerticalBox::Slot().Padding(0,0,0,4).AutoHeight()[SNew(SButton)
												.Text(FText::FromString("Validate JSON Schema Keys")
												)]*/

												+SVerticalBox::Slot().Padding(0, 0, 0, 4).AutoHeight()[SNew(SButton)
												.OnClicked_Lambda([this] {
												if (keyInfos.Num() > 0)
													this->SaveAssetFromMap();
												return FReply::Handled();
											})
												.Text(FText::FromString("Save Schema")
												)]


												+ SVerticalBox::Slot().Padding(0, 0, 0, 4).AutoHeight()[SNew(SButton)
												.OnClicked_Lambda([this] {
												this->LoadFromUAsset();
												return FReply::Handled();
											})
												.Text(FText::FromString("Load Schema")
												)]

												+ SVerticalBox::Slot().Padding(15, 3, 0, 4).AutoHeight()
												[
													SNew(STextBlock)
														.Text(FText::FromString("Auto Generate Schema Tools"))

												]

												+ SVerticalBox::Slot()
												.Padding(0, 0, 0, 4)
												.AutoHeight()
												[
													SNew(SComboButton)
														.OnGetMenuContent_Lambda([this]()
													{
														FMenuBuilder MenuBuilder(true, nullptr);
														UEnum* EnumPtr = StaticEnum<EAutoPopulateType>();

														for (int32 i = 0; i < EnumPtr->NumEnums() - 1; ++i)
														{
															const int64 EnumValue = EnumPtr->GetValueByIndex(i);
															const FString Name = EnumPtr->GetNameStringByIndex(i);


															EAutoPopulateType Option = static_cast<EAutoPopulateType>(EnumValue);

															MenuBuilder.AddWidget(
																SNew(SCheckBox)
																.OnCheckStateChanged_Lambda([this, Option](ECheckBoxState NewState)
															{
																if (NewState == ECheckBoxState::Checked)
																{
																	if (!SelectedAutoPopulateOptions.Contains(Option))
																	{
																		SelectedAutoPopulateOptions.Add(Option);
																	}
																}
																else
																{
																	SelectedAutoPopulateOptions.Remove(Option);
																}
															})
																.IsChecked_Lambda([this, Option]()
															{
																return SelectedAutoPopulateOptions.Contains(Option) ?
																	ECheckBoxState::Checked : ECheckBoxState::Unchecked;
															})
																[
																	SNew(STextBlock).Text(StaticEnum<EAutoPopulateType>()->GetDisplayNameTextByValue(EnumValue))
																],
																FText::GetEmpty(),
																true // No label padding
															);
														}

														return MenuBuilder.MakeWidget();
													})
														.ContentPadding(FMargin(4, 2))
														.ButtonContent()
														[
															SNew(STextBlock)
																.Text_Lambda([this]()
															{
																// Display a summary of selected options
																FString SelectedText;
																for (EAutoPopulateType Option : SelectedAutoPopulateOptions)
																{
																	if (!SelectedText.IsEmpty()) SelectedText += TEXT(", ");
																	else SelectedText += TEXT(" ");
																	SelectedText += StaticEnum<EAutoPopulateType>()->GetNameStringByValue((int64)Option);
																}
																return FText::FromString(SelectedText);
															})
														]
												]

												+ SVerticalBox::Slot().Padding(0, 0, 0, 4).AutoHeight()[SNew(SButton)
												.OnClicked_Lambda([this] {
												this->AutoPopulateSchema(this->GetAutoPopulateTypeSelection());
												return FReply::Handled();
											})
												.Text(FText::FromString("Auto Populate Schema Keys")
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
												.Padding(0, 2, 0, 0)
												.FillWidth(0.7f)
												[
													SNew(SEditableTextBox)
														.Text_Lambda([this] {
														return FText::FromString(newKeyStr);
													})
														.OnTextChanged_Lambda([this](const FText& NewText) {
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
															while (!newKeyStr.IsEmpty() && FString(TEXT("!\"£$%&*")).Contains(newKeyStr.Left(1))) newKeyStr.RemoveAt(0);

															if (newKeyStr.EndsWith("."))
															{
																UE_LOG(LogZLStateEditor, Warning, TEXT("Nested key %s must end with valid sub key to be added"), *newKeyStr)
																	return FReply::Handled();
															}

															if (!this->keyInfos.Contains(newKeyStr))
															{
																this->keyInfos.Add(newKeyStr);
																this->keyInfos[newKeyStr].dataType = "String";
																this->keyInfos[newKeyStr].defaultValue = MakeShared<FJsonValueString>("");

																if (!AreaExpansionStates.Contains(newKeyStr))
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
	if (SetActiveObject)
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
				.Padding(0, 0, 0, 10)
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
								.Padding(0, 0, 0, 2)
								[
									SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().AutoWidth()
										.Padding(0, 2, 2, 0)
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
								.Padding(0, 4, 0, 0)
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
												.Padding(0, 0, 0, 2)
												[
													SNew(SHorizontalBox)
														+ SHorizontalBox::Slot().AutoWidth()
														.Padding(2, 2, 2, 0)
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
												+ SVerticalBox::Slot().AutoHeight()
												.Padding(0, 2, 0, 0)
												[
													SNew(SCheckBox)
														.OnCheckStateChanged_Lambda([this, Key](ECheckBoxState NewState)
													{
														keyInfos[Key].ignoredInDataHash = (NewState == ECheckBoxState::Checked) ? true : false;

														this->UpdateJsonData(s_currentJsonStr);
													})
														.Visibility_Lambda([this] {
														const UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();
														check(Settings);
														return Settings->showExperimentalFeatures ? EVisibility::Visible : EVisibility::Hidden;
													})
														.ToolTipText(FText::FromString("Ignored by content generation data hashing"))
														.IsChecked_Lambda([this, Key] {
														return keyInfos[Key].ignoredInDataHash ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
													})
														.Content()
														[
															SNew(STextBlock).Text(FText::FromString("Transient"))
														]
												]
										]
								]
								+ SVerticalBox::Slot().AutoHeight()
								.Padding(0, 4, 0, 0)
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

TArray<FName> GetAllVehicleLocationNamesInEditor()
{
	TArray<FName> LocationNames;
	LocationNames.Add(TEXT("None"));

	if (GEditor)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World)
		{
			UClass* VehicleLocationClass = StaticLoadClass(
				AActor::StaticClass(), nullptr,
				TEXT("/Script/ZLVehicleComponents.ZLVehicleLocation")
			);

			if (VehicleLocationClass)
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					AActor* Actor = *It;
					if (Actor->IsA(VehicleLocationClass))
					{
						FProperty* NameProp = VehicleLocationClass->FindPropertyByName(TEXT("LocationName"));
						if (NameProp)
						{
							FName LocationName = *NameProp->ContainerPtrToValuePtr<FName>(Actor);
							LocationNames.Add(LocationName);
						}
					}
				}
			}
		}
	}

	return LocationNames;
}

TMap<FName, TArray<FName>> GetAllZLNodesInEditor()
{
	TMap<FName, TArray<FName>> ZLNodeOptions;

	if (GEditor)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World)
		{
			UClass* ZLNodeClass = StaticLoadClass(
				AActor::StaticClass(), nullptr,
				TEXT("/Script/ZLWorldManager.ZLNodeController")
			);

			UClass* ZLLightNodeClass = StaticLoadClass(
				AActor::StaticClass(), nullptr,
				TEXT("/Script/ZLVehicleComponents.ZLLightNodeController")
			);

			FName NodeName = NAME_None;
			bool NodeHasSequence = false;
			ULevelSequence* sequence;

			if (ZLNodeClass)
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					AActor* Actor = *It;
					if (Actor->IsA(ZLNodeClass))
					{
#if WITH_EDITOR
						NodeName = FName(Actor->GetActorLabel());
#endif
						FProperty* ExcludeFromSchemaProp = ZLNodeClass->FindPropertyByName(TEXT("bExcludeFromSchema"));
						FProperty* ParentTriggerProp = ZLNodeClass->FindPropertyByName(TEXT("bTriggersAttachedControllers"));
						FProperty* ZLNodeSequenceProp = ZLNodeClass->FindPropertyByName(TEXT("Sequence"));

						if (ExcludeFromSchemaProp)
						{
							bool SkipParsing = *ExcludeFromSchemaProp->ContainerPtrToValuePtr<bool>(Actor);

							if (SkipParsing)
								continue;
						}

						if (ZLNodeSequenceProp)
						{
							FObjectProperty* SequenceObjectProp = CastField<FObjectProperty>(ZLNodeSequenceProp);

							UObject* RawObject = SequenceObjectProp->GetObjectPropertyValue_InContainer(Actor);

							sequence = Cast<ULevelSequence>(RawObject);

							if (sequence)
								NodeHasSequence = true;
							else
								NodeHasSequence = false;
						}

						if (ParentTriggerProp && !NodeHasSequence) //Check if any attached controllers have sequences and should be playable from this parent
						{
							TArray<AActor*> AttachedActors;

							Actor->GetAttachedActors(AttachedActors);

							for (AActor* ChildActor : AttachedActors)
							{
								if (ChildActor && ChildActor->IsA(ZLNodeClass))
								{
									FProperty* ChildSeqProp = ChildActor->GetClass()->FindPropertyByName(TEXT("Sequence"));

									if (ChildSeqProp)
									{
										FObjectProperty* ChildObjProp = CastField<FObjectProperty>(ChildSeqProp);
										if (ChildObjProp)
										{
											UObject* ChildSeqObj = ChildObjProp->GetObjectPropertyValue_InContainer(ChildActor);

											if (ChildSeqObj)
											{
												NodeHasSequence = true;
												break;
											}
										}
									}
								}
							}
						}

						if (NodeName != NAME_None)
						{
							FName KeyStr;
							if (Actor->GetClass() == ZLNodeClass)
								KeyStr = FName("triggers." + NodeName.ToString());
							else if (Actor->GetClass() == ZLLightNodeClass)
								KeyStr = FName("lights." + NodeName.ToString());

							ZLNodeOptions.Add(KeyStr, TArray<FName>{"OFF", "ON"});

							if (NodeHasSequence)
							{
								ZLNodeOptions[KeyStr].Add("PLAYING_TO_OFF");
								ZLNodeOptions[KeyStr].Add("PLAYING_TO_ON");
							}
						}
					}
				}
			}
		}
	}

	return ZLNodeOptions;
}

TArray<FName> GetAllCameraNamesInEditor()
{
	TArray<FName> CameraNames;

	if (GEditor)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World)
		{
			UClass* CameraBaseClass = StaticLoadClass(
				AActor::StaticClass(), nullptr,
				TEXT("/Script/ZLCameras.ZLCameraPawnBase")
			);

			if (CameraBaseClass)
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					AActor* Actor = *It;
					if (Actor->IsA(CameraBaseClass))
					{
						FName CamName = NAME_None;
						FProperty* NameProp = CameraBaseClass->FindPropertyByName(TEXT("DisplayName"));
						if (NameProp)
						{
							void* ValuePtr = NameProp->ContainerPtrToValuePtr<void>(Actor);
							CamName = *NameProp->ContainerPtrToValuePtr<FName>(Actor);
						}

						CameraNames.Add(CamName);
					}
				}

				// Always add FlyCamera, it does not live in the scene but is made on startup of the viewer.
				// Might have to check if the real ZLFlyCamera class exists.
				CameraNames.Add("FlyCamera");
			}
		}
	}

	return CameraNames;
}

//TArray<TPair<FString, TArray<FString>>> GetAllAnimationKeyStatesInEditor()
//{
//	TArray<TPair<FString, TArray<FString>>> AnimationKeyStates;
//
//	// Script / CoreUObject.Class'/Script/DIMEconfigurator.AnimationTriggerComponent'
//
//	if (GEditor)
//	{
//		UWorld* World = GEditor->GetEditorWorldContext().World();
//		if (World)
//		{
//			UClass* AnimTriggerComponentClass = StaticLoadClass(
//				UObject::StaticClass(), nullptr,
//				TEXT("/Script/DIMEconfigurator.AnimationTriggerComponent")
//			);
//
//			UScriptStruct* AnimClipDataStruct = Cast<UScriptStruct>(StaticLoadObject(
//				UScriptStruct::StaticClass(), nullptr, 
//				TEXT("/Script/DIMEconfigurator.AnimClipData"))
//			);
//
//			if (AnimTriggerComponentClass && AnimClipDataStruct)
//			{				
//				FArrayProperty* AnimationsProp = CastField<FArrayProperty>(AnimTriggerComponentClass->FindPropertyByName(TEXT("Animations")));
//				FProperty* AnimNameProp = AnimClipDataStruct->FindPropertyByName(TEXT("Name"));
//
//				if (AnimationsProp && AnimNameProp)
//				{
//					for (TActorIterator<AActor> It(World); It; ++It)
//					{
//						AActor* Actor = *It;
//						if (!Actor) continue;
//
//						// 5. For each actor, find the component of the type we're looking for.
//						UActorComponent* Component = Actor->FindComponentByClass(AnimTriggerComponentClass);
//						if (Component)
//						{
//							FScriptArrayHelper ArrayHelper(AnimationsProp, AnimationsProp->ContainerPtrToValuePtr<void>(Component));
//
//							for (int32 i = 0; i < ArrayHelper.Num(); ++i)
//							{
//								// Get a raw pointer to the struct data at the current array index.
//								uint8* StructPtr = ArrayHelper.GetRawPtr(i);
//
//								// From the struct pointer, get the value of the 'AnimationName' property.
//								FString AnimationName = *AnimNameProp->ContainerPtrToValuePtr<FString>(StructPtr);
//
//								if (!AnimationName.IsEmpty())
//								{
//									TArray<FString> States;
//									States.Add("Reverse");
//									States.Add("Play");
//									// Add the name to our list, ensuring it's not already there.
//									AnimationKeyStates.AddUnique(TPair<FString, TArray<FString>>(AnimationName, States));
//								}
//							}
//						}
//					}
//				}
//			}
//		}
//	}
//
//	return AnimationKeyStates;
//}


TArray<TPair<FString, TArray<FString>>> GetAllAnimationKeyStatesInEditor()
{
	TArray<TPair<FString, TArray<FString>>> AnimationKeyStates;

	if (!GEditor)
	{
		return AnimationKeyStates;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return AnimationKeyStates;
	}

	UClass* AnimTriggerComponentClass = StaticLoadClass(
		UObject::StaticClass(), nullptr,
		TEXT("/Script/DIMEconfigurator.AnimationTriggerComponent")
	);

	UScriptStruct* AnimClipDataStruct = Cast<UScriptStruct>(StaticLoadObject(
		UScriptStruct::StaticClass(), nullptr,
		TEXT("/Script/DIMEconfigurator.AnimClipData"))
	);

	if (AnimTriggerComponentClass && AnimClipDataStruct)
	{
		FArrayProperty* AnimationsProp = CastField<FArrayProperty>(AnimTriggerComponentClass->FindPropertyByName(TEXT("Animations")));
		FProperty* AnimNameProp = AnimClipDataStruct->FindPropertyByName(TEXT("Name"));

		if (AnimationsProp && AnimNameProp)
		{
			TArray<FString> AllAnimationNames;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!Actor) continue;

				UActorComponent* Component = Actor->FindComponentByClass(AnimTriggerComponentClass);
				if (Component)
				{
					FScriptArrayHelper ArrayHelper(AnimationsProp, AnimationsProp->ContainerPtrToValuePtr<void>(Component));
					for (int32 i = 0; i < ArrayHelper.Num(); ++i)
					{
						uint8* StructPtr = ArrayHelper.GetRawPtr(i);
						FString FullAnimationName = *AnimNameProp->ContainerPtrToValuePtr<FString>(StructPtr);
						if (!FullAnimationName.IsEmpty())
						{
							AllAnimationNames.AddUnique(FullAnimationName);
						}
					}
				}
			}

			TMap<FString, int32> BaseNameCounts;
			for (const FString& FullName : AllAnimationNames)
			{
				FString PotentialBaseName = FullName;
				int32 SplitIndex;
				if (FullName.FindLastChar(TEXT('_'), SplitIndex))
				{
					PotentialBaseName = FullName.Left(SplitIndex);
				}
				BaseNameCounts.FindOrAdd(PotentialBaseName)++;
			}

			TMap<FString, TArray<FString>> ParsedAnimations;
			for (const FString& FullName : AllAnimationNames)
			{
				FString BaseName = FullName;
				FString State;
				int32 SplitIndex;

				bool bHasState = FullName.FindLastChar(TEXT('_'), SplitIndex);
				if (bHasState)
				{
					BaseName = FullName.Left(SplitIndex);
					State = FullName.RightChop(SplitIndex + 1);
				}

				const int32* Count = BaseNameCounts.Find(BaseName);

				// A name is only split into a base and state if its base name appears more than once.
				if (bHasState && Count && *Count > 1)
				{
					TArray<FString>& States = ParsedAnimations.FindOrAdd(BaseName);
					States.AddUnique(State);
				}
				else
				{
					// Otherwise, the full name is treated as a unique key with no states.
					ParsedAnimations.FindOrAdd(FullName);
				}
			}

			for (const TPair<FString, TArray<FString>>& Pair : ParsedAnimations)
			{
				AnimationKeyStates.Add(Pair);
			}
		}
	}

	return AnimationKeyStates;
}

static TArray<TSharedPtr<FJsonValue>> CamTransitionList{ MakeShared<FJsonValueString>("cut"),
															MakeShared<FJsonValueString>("linear"),
															MakeShared<FJsonValueString>("cubic"),
															MakeShared<FJsonValueString>("ease-in"),
															MakeShared<FJsonValueString>("ease-out"),
															MakeShared<FJsonValueString>("ease-inout")
};

void FZLStateEditor::AutoPopulateSchema(TArray<EAutoPopulateType> PopFlags)
{
	bool UpdateJsonStr = false;

	FString keyStr = "";

	if (PopFlags.Contains(EAutoPopulateType::Locations))
	{
		keyStr = "modelLocation";
		TArray<FName> LocationNames = GetAllVehicleLocationNamesInEditor();

		if (LocationNames.Num() != 0)
		{
			LocationNames.Sort([](const FName& A, const FName& B) {
				return A.Compare(B) < 0;
			});

			FString DefaultLocation = LocationNames[0].ToString();
			//Add to jsonobject
			if (!ActiveJsonObject->HasField(keyStr))
				ActiveJsonObject->SetStringField(keyStr, DefaultLocation);
			else
				DefaultLocation = ActiveJsonObject->GetStringField(keyStr);

			//Add to keyinfos
			if (!keyInfos.Contains(keyStr))
				keyInfos.Add(keyStr, StateKeyInfo());

			TArray<TSharedPtr<FJsonValue>> LocationList;

			for (auto loc : LocationNames)
			{
				LocationList.Add(MakeShared<FJsonValueString>(loc.ToString()));
			}

			keyInfos[keyStr].dataType = "String";
			keyInfos[keyStr].defaultValue = MakeShared<FJsonValueString>(DefaultLocation);
			keyInfos[keyStr].limitValues = true;
			keyInfos[keyStr].acceptedValues = LocationList;

			AreaExpansionStates.Add(keyStr, TPair<bool, bool>(true, true));

			UpdateJsonStr = true;
		}
	}
	if (PopFlags.Contains(EAutoPopulateType::Cameras))
	{
		keyStr = "camera";
		TArray<FName> CameraNames = GetAllCameraNamesInEditor();

		if (CameraNames.Num() != 0)
		{
			CameraNames.Sort([](const FName& A, const FName& B) {
				return A.Compare(B) < 0;
			});

			FString DefaultCamera = CameraNames[0].ToString();
			if (!ActiveJsonObject->HasField(keyStr))
				ActiveJsonObject->SetStringField(keyStr, DefaultCamera);
			else
				DefaultCamera = ActiveJsonObject->GetStringField(keyStr);

			if (!keyInfos.Contains(keyStr))
				keyInfos.Add(keyStr, StateKeyInfo());

			TArray<TSharedPtr<FJsonValue>> CamList;

			for (auto loc : CameraNames)
			{
				CamList.Add(MakeShared<FJsonValueString>(loc.ToString()));
			}

			keyInfos[keyStr].dataType = "String";
			keyInfos[keyStr].defaultValue = MakeShared<FJsonValueString>(DefaultCamera);
			keyInfos[keyStr].limitValues = true;
			keyInfos[keyStr].acceptedValues = CamList;

			AreaExpansionStates.Add(keyStr, TPair<bool, bool>(true, true));

			//Cam Transition Structure

			TSharedPtr<FJsonObject> camTransObject = MakeShared<FJsonObject>();

			keyStr = "cameraTransition.type";
			if (!keyInfos.Contains(keyStr))
				keyInfos.Add(keyStr, StateKeyInfo());

			keyInfos[keyStr].dataType = "String";
			keyInfos[keyStr].defaultValue = MakeShared<FJsonValueString>("cut");
			keyInfos[keyStr].limitValues = true;
			keyInfos[keyStr].acceptedValues = CamTransitionList;

			camTransObject->SetStringField("type", "cut");

			AreaExpansionStates.Add(keyStr, TPair<bool, bool>(true, false));

			keyStr = "cameraTransition.time";
			if (!keyInfos.Contains(keyStr))
				keyInfos.Add(keyStr, StateKeyInfo());

			keyInfos[keyStr].dataType = "Number";
			keyInfos[keyStr].defaultValue = MakeShared<FJsonValueNumber>(0.0f);

			camTransObject->SetNumberField("time", 0.0f);

			AreaExpansionStates.Add(keyStr, TPair<bool, bool>(true, false));

			keyStr = "cameraTransition.scaleExp";
			if (!keyInfos.Contains(keyStr))
				keyInfos.Add(keyStr, StateKeyInfo());

			keyInfos[keyStr].dataType = "Number";
			keyInfos[keyStr].defaultValue = MakeShared<FJsonValueNumber>(0.0f);

			AreaExpansionStates.Add(keyStr, TPair<bool, bool>(true, false));

			camTransObject->SetNumberField("scaleExp", 0.0f);

			ActiveJsonObject->SetObjectField("cameraTransition", camTransObject);

			UpdateJsonStr = true;
		}
	}
	if (PopFlags.Contains(EAutoPopulateType::Animations))
	{
		TArray<TPair<FString, TArray<FString>>> AnimationKeyStates = GetAllAnimationKeyStatesInEditor();

		TSharedPtr<FJsonObject> animObject = MakeShared<FJsonObject>();

		if (AnimationKeyStates.Num() != 0)
		{
			AnimationKeyStates.Sort([](const TPair<FString, TArray<FString>>& A, const TPair<FString, TArray<FString>>& B)
			{
				return A.Key < B.Key;
			});

			for (const auto& anim : AnimationKeyStates)
			{
				keyStr = "animation." + anim.Key;

				if (!keyInfos.Contains(keyStr))
					keyInfos.Add(keyStr, StateKeyInfo());

				if (anim.Value.Num() > 0)
				{
					TArray<TSharedPtr<FJsonValue>> states;
					for (const FString& animState : anim.Value)
					{
						states.Add(MakeShared<FJsonValueString>(animState));
						states.Add(MakeShared<FJsonValueString>("PLAYING_TO_" + animState));
					}

					TSharedPtr<FJsonValue> defaultValue = states[0];

					bool bHasPlay = false;
					FString reverseValue;
					bool bHasOpen = false;
					FString closeValue;

					for (const FString& stateStr : anim.Value)
					{
						if (stateStr.Equals(TEXT("Forward"), ESearchCase::IgnoreCase))    bHasPlay = true;
						if (stateStr.Equals(TEXT("Play"), ESearchCase::IgnoreCase))    bHasPlay = true;
						if (stateStr.Equals(TEXT("Reverse"), ESearchCase::IgnoreCase)) reverseValue = stateStr;
						if (stateStr.Equals(TEXT("Open"), ESearchCase::IgnoreCase))     bHasOpen = true;
						if (stateStr.Equals(TEXT("Close"), ESearchCase::IgnoreCase))   closeValue = stateStr;
						if (stateStr.Equals(TEXT("On"), ESearchCase::IgnoreCase))     bHasOpen = true;
						if (stateStr.Equals(TEXT("Off"), ESearchCase::IgnoreCase))   closeValue = stateStr;
					}

					if (bHasPlay && !reverseValue.IsEmpty())
					{
						defaultValue = MakeShared<FJsonValueString>(reverseValue);
					}
					else if (bHasOpen && !closeValue.IsEmpty())
					{
						defaultValue = MakeShared<FJsonValueString>(closeValue);
					}

					keyInfos[keyStr].dataType = "String";
					keyInfos[keyStr].defaultValue = defaultValue;
					keyInfos[keyStr].limitValues = true;
					keyInfos[keyStr].acceptedValues = states;

					animObject->SetStringField(anim.Key, defaultValue->AsString());
				}
				else
				{
					keyInfos[keyStr].dataType = "Bool";
					keyInfos[keyStr].defaultValue = MakeShared<FJsonValueBoolean>(false);

					animObject->SetBoolField(anim.Key, false);
				}

				AreaExpansionStates.Add(keyStr, TPair<bool, bool>(true, true));
			}

			ActiveJsonObject->SetObjectField("animation", animObject);

			UpdateJsonStr = true;
		}
	}
	if (PopFlags.Contains(EAutoPopulateType::ZLNodes))
	{
		TMap<FName, TArray<FName>> ZLNodeStates = GetAllZLNodesInEditor();

		if (ZLNodeStates.Num() != 0)
		{
			ZLNodeStates.KeySort([](const FName& A, const FName& B) {
				return A.Compare(B) < 0;
			});

			TSharedPtr<FJsonObject> GenericNodesObject = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> LightsNodesObject = MakeShared<FJsonObject>();

			for (const auto& node : ZLNodeStates)
			{
				keyStr = node.Key.ToString();
				FString DefaultNodeState = node.Value[0].ToString();

				if (ActiveJsonObject->HasField(keyStr))
					DefaultNodeState = ActiveJsonObject->GetStringField(keyStr);

				//Add to keyinfos
				if (!keyInfos.Contains(keyStr))
					keyInfos.Add(keyStr, StateKeyInfo());

				TArray<TSharedPtr<FJsonValue>> NodeStateList;

				for (auto state : node.Value)
				{
					NodeStateList.Add(MakeShared<FJsonValueString>(state.ToString()));
				}

				keyInfos[keyStr].dataType = "String";
				keyInfos[keyStr].defaultValue = MakeShared<FJsonValueString>(DefaultNodeState);
				keyInfos[keyStr].limitValues = true;
				keyInfos[keyStr].acceptedValues = NodeStateList;

				AreaExpansionStates.Add(keyStr, TPair<bool, bool>(true, true));

				if (keyStr.StartsWith("lights."))
				{
					keyStr.RemoveFromStart("lights.");
					LightsNodesObject->SetStringField(keyStr, DefaultNodeState);
				}
				else
				{
					keyStr.RemoveFromStart("triggers.");
					GenericNodesObject->SetStringField(keyStr, DefaultNodeState);
				}

			}

			if (GenericNodesObject->Values.Num() != 0)
				ActiveJsonObject->SetObjectField("triggers", GenericNodesObject);

			if (LightsNodesObject->Values.Num() != 0)
				ActiveJsonObject->SetObjectField("lights", LightsNodesObject);

			UpdateJsonStr = true;
		}
	}

	if (UpdateJsonStr)
	{
		this->UpdateJsonStr();
		UpdateJsonData(s_currentJsonStr);
	}
}

#undef LOCTEXT_NAMESPACE

#endif
