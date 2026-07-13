// Copyright ZeroLight ltd. All Rights Reserved.

#if WITH_EDITOR

#include "ZLStateEditorV2.h"
#include "ZLCloudPluginVersion.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UnrealType.h"
#include "Features/IModularFeatures.h"
#include "IZLOmniStream_SchemaAutoPopulate.h"
#include "IPythonScriptPlugin.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Widgets/Images/SThrobber.h"

#if UNREAL_5_6_OR_NEWER
#include "UObject/SavePackage.h"
#endif
#include <EditorZLCloudPluginSettings.h>

#define LOCTEXT_NAMESPACE "FZLStateEditorV2"

DEFINE_LOG_CATEGORY(LogZLStateEditorV2);

TWeakPtr<FZLStateEditorV2> FZLStateEditorV2::s_LiveInstance;

namespace ZLStateEditorV2SchemaPersistence
{
	static const TCHAR* ConfigSection = TEXT("ZLStateEditorV2");
	static const TCHAR* ConfigFileName = TEXT("DefaultZLStateEditorV2.ini");
	static const TCHAR* LastSchemaPathKey = TEXT("LastOpenSchemaAssetPath");
	static const TCHAR* bAutoPopulateOptionsSavedKey = TEXT("bAutoPopulateOptionsSaved");
	static const TCHAR* SelectedAutoPopulateOptionsKey = TEXT("SelectedAutoPopulateOptions");

	FString GetConfigFilePath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectConfigDir(), ConfigFileName));
	}

	void EnsureConfigFileLoaded()
	{
		if (!GConfig)
		{
			return;
		}

		const FString ConfigPath = GetConfigFilePath();
		if (!FPaths::FileExists(ConfigPath))
		{
			const FString SectionHeader = FString::Printf(TEXT("[%s]\r\n"), ConfigSection);
			FFileHelper::SaveStringToFile(SectionHeader, *ConfigPath);
		}

		GConfig->LoadFile(ConfigPath);
	}

	FString ToRelativeProjectPath(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return Path;
		}

		FString Normalized = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Normalized);

		FString Relative = Normalized;
		if (FPaths::MakePathRelativeTo(Relative, *FPaths::ProjectDir()))
		{
			FPaths::NormalizeFilename(Relative);
			return Relative;
		}

		return Path;
	}

	FString ToAbsoluteProjectPath(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return Path;
		}

		if (FPaths::IsRelative(Path))
		{
			FString Absolute = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
			FPaths::NormalizeFilename(Absolute);
			return Absolute;
		}

		FString Normalized = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Normalized);
		return Normalized;
	}

	void MigrateFromLegacyEditorPerProjectIniIfNeeded()
	{
		if (!GConfig)
		{
			return;
		}

		EnsureConfigFileLoaded();
		const FString ConfigPath = GetConfigFilePath();

		bool bNewFileHasSavedOptions = false;
		GConfig->GetBool(
			ConfigSection,
			bAutoPopulateOptionsSavedKey,
			bNewFileHasSavedOptions,
			ConfigPath);

		FString NewSchemaPath;
		GConfig->GetString(
			ConfigSection,
			LastSchemaPathKey,
			NewSchemaPath,
			ConfigPath);

		bool bLegacyHasSavedOptions = false;
		GConfig->GetBool(
			ConfigSection,
			bAutoPopulateOptionsSavedKey,
			bLegacyHasSavedOptions,
			GEditorPerProjectIni);

		FString LegacySchemaPath;
		GConfig->GetString(
			ConfigSection,
			LastSchemaPathKey,
			LegacySchemaPath,
			GEditorPerProjectIni);

		if (LegacySchemaPath.IsEmpty())
		{
			GConfig->GetString(
				TEXT("ZLStateEditor"),
				LastSchemaPathKey,
				LegacySchemaPath,
				GEditorPerProjectIni);
		}

		TArray<FString> LegacyOptions;
		GConfig->GetArray(
			ConfigSection,
			SelectedAutoPopulateOptionsKey,
			LegacyOptions,
			GEditorPerProjectIni);

		if (bNewFileHasSavedOptions && !NewSchemaPath.IsEmpty())
		{
			return;
		}

		if (!bLegacyHasSavedOptions && LegacySchemaPath.IsEmpty())
		{
			return;
		}

		bool bWrote = false;

		if (NewSchemaPath.IsEmpty() && !LegacySchemaPath.IsEmpty())
		{
			GConfig->SetString(
				ConfigSection,
				LastSchemaPathKey,
				*ToRelativeProjectPath(LegacySchemaPath),
				ConfigPath);
			bWrote = true;
		}

		if (!bNewFileHasSavedOptions && bLegacyHasSavedOptions)
		{
			LegacyOptions.Sort();
			GConfig->SetBool(
				ConfigSection,
				bAutoPopulateOptionsSavedKey,
				true,
				ConfigPath);
			GConfig->SetArray(
				ConfigSection,
				SelectedAutoPopulateOptionsKey,
				LegacyOptions,
				ConfigPath);
			bWrote = true;
		}

		if (bWrote)
		{
			GConfig->Flush(false, ConfigPath);
		}
	}
}

namespace ZLStateEditorV2Internal
{
	TSharedPtr<FJsonValue> CloneJsonValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return nullptr;
		}

		switch (Value->Type)
		{
		case EJson::String:
			return MakeShared<FJsonValueString>(Value->AsString());
		case EJson::Number:
			return MakeShared<FJsonValueNumber>(Value->AsNumber());
		case EJson::Boolean:
			return MakeShared<FJsonValueBoolean>(Value->AsBool());
		case EJson::Null:
			return MakeShared<FJsonValueNull>();
		case EJson::Array:
		{
			TArray<TSharedPtr<FJsonValue>> ClonedArray;
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				ClonedArray.Add(CloneJsonValue(Item));
			}
			return MakeShared<FJsonValueArray>(ClonedArray);
		}
		case EJson::Object:
		{
			TSharedPtr<FJsonObject> ClonedObject = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject> SourceObject = Value->AsObject();
			if (SourceObject.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : SourceObject->Values)
				{
					ClonedObject->SetField(Pair.Key, CloneJsonValue(Pair.Value));
				}
			}
			return MakeShared<FJsonValueObject>(ClonedObject);
		}
		default:
			return nullptr;
		}
	}

	bool SplitKeyPath(const FString& FullKey, FString& OutParentPath, FString& OutLeafName)
	{
		const int32 LastDotIndex = FullKey.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastDotIndex == INDEX_NONE)
		{
			OutParentPath = TEXT("");
			OutLeafName = FullKey;
			return !OutLeafName.IsEmpty();
		}

		OutParentPath = FullKey.Left(LastDotIndex);
		OutLeafName = FullKey.Mid(LastDotIndex + 1);
		return !OutLeafName.IsEmpty();
	}

	TSharedPtr<FJsonObject> GetObjectAtPath(const TSharedPtr<FJsonObject>& RootObject, const FString& Path)
	{
		if (!RootObject.IsValid())
		{
			return nullptr;
		}

		if (Path.IsEmpty())
		{
			return RootObject;
		}

		TArray<FString> PathParts;
		Path.ParseIntoArray(PathParts, TEXT("."), true);

		TSharedPtr<FJsonObject> CurrentObject = RootObject;
		for (const FString& Part : PathParts)
		{
			if (!CurrentObject.IsValid())
			{
				return nullptr;
			}

			const TSharedPtr<FJsonValue> ExistingValue = CurrentObject->TryGetField(Part);
			if (!ExistingValue.IsValid() || ExistingValue->Type != EJson::Object)
			{
				return nullptr;
			}

			CurrentObject = ExistingValue->AsObject();
		}

		return CurrentObject;
	}

	TSharedPtr<FJsonObject> GetOrCreateObjectAtPath(const TSharedPtr<FJsonObject>& RootObject, const FString& Path)
	{
		if (!RootObject.IsValid())
		{
			return nullptr;
		}

		if (Path.IsEmpty())
		{
			return RootObject;
		}

		TArray<FString> PathParts;
		Path.ParseIntoArray(PathParts, TEXT("."), true);

		TSharedPtr<FJsonObject> CurrentObject = RootObject;
		for (const FString& Part : PathParts)
		{
			if (!CurrentObject.IsValid())
			{
				return nullptr;
			}

			const TSharedPtr<FJsonValue> ExistingValue = CurrentObject->TryGetField(Part);
			if (ExistingValue.IsValid() && ExistingValue->Type == EJson::Object)
			{
				CurrentObject = ExistingValue->AsObject();
				continue;
			}

			TSharedPtr<FJsonObject> NextObject = MakeShared<FJsonObject>();
			CurrentObject->SetObjectField(Part, NextObject);
			CurrentObject = NextObject;
		}

		return CurrentObject;
	}

	TSharedPtr<FJsonValue> GetJsonValueAtPath(const TSharedPtr<FJsonObject>& RootObject, const FString& FullKey)
	{
		FString ParentPath;
		FString LeafName;
		if (!SplitKeyPath(FullKey, ParentPath, LeafName))
		{
			return nullptr;
		}

		const TSharedPtr<FJsonObject> ParentObject = GetObjectAtPath(RootObject, ParentPath);
		return ParentObject.IsValid() ? ParentObject->TryGetField(LeafName) : nullptr;
	}

	bool SetJsonValueAtPath(const TSharedPtr<FJsonObject>& RootObject, const FString& FullKey, const TSharedPtr<FJsonValue>& Value)
	{
		FString ParentPath;
		FString LeafName;
		if (!SplitKeyPath(FullKey, ParentPath, LeafName) || !Value.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject> ParentObject = GetOrCreateObjectAtPath(RootObject, ParentPath);
		if (!ParentObject.IsValid())
		{
			return false;
		}

		ParentObject->SetField(LeafName, Value);
		return true;
	}

	bool RemoveJsonValueAtPath(const TSharedPtr<FJsonObject>& RootObject, const FString& FullKey)
	{
		FString ParentPath;
		FString LeafName;
		if (!SplitKeyPath(FullKey, ParentPath, LeafName))
		{
			return false;
		}

		TArray<FString> PathParts;
		FullKey.ParseIntoArray(PathParts, TEXT("."), true);
		if (PathParts.Num() == 0 || !RootObject.IsValid())
		{
			return false;
		}

		TArray<TSharedPtr<FJsonObject>> ParentStack;
		TArray<FString> ParentKeys;
		TSharedPtr<FJsonObject> CurrentObject = RootObject;

		for (int32 PartIdx = 0; PartIdx < PathParts.Num() - 1; ++PartIdx)
		{
			const FString& Segment = PathParts[PartIdx];
			const TSharedPtr<FJsonValue> Existing = CurrentObject->TryGetField(Segment);
			if (!Existing.IsValid() || Existing->Type != EJson::Object)
			{
				return false;
			}

			ParentStack.Add(CurrentObject);
			ParentKeys.Add(Segment);
			CurrentObject = Existing->AsObject();
			if (!CurrentObject.IsValid())
			{
				return false;
			}
		}

		const FString& LeafSegment = PathParts.Last();
		if (!CurrentObject->HasField(LeafSegment))
		{
			return false;
		}
		CurrentObject->RemoveField(LeafSegment);

		for (int32 ParentIdx = ParentStack.Num() - 1; ParentIdx >= 0; --ParentIdx)
		{
			if (CurrentObject.IsValid() && CurrentObject->Values.Num() == 0)
			{
				ParentStack[ParentIdx]->RemoveField(ParentKeys[ParentIdx]);
				CurrentObject = ParentStack[ParentIdx];
			}
			else
			{
				break;
			}
		}

		return true;
	}

	// Merges every top-level field of Source into Target. Object fields are merged recursively;
	// scalar/array fields from Source overwrite existing entries in Target.
	void MergeJsonObjectInto(const TSharedPtr<FJsonObject>& Target, const TSharedPtr<FJsonObject>& Source)
	{
		if (!Target.IsValid() || !Source.IsValid())
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Source->Values)
		{
			if (!Pair.Value.IsValid())
			{
				continue;
			}

			if (Pair.Value->Type == EJson::Object)
			{
				const TSharedPtr<FJsonValue> ExistingValue = Target->TryGetField(Pair.Key);
				if (ExistingValue.IsValid() && ExistingValue->Type == EJson::Object)
				{
					MergeJsonObjectInto(ExistingValue->AsObject(), Pair.Value->AsObject());
					continue;
				}
			}

			Target->SetField(Pair.Key, CloneJsonValue(Pair.Value));
		}
	}

	StateKeyInfoV2 CloneStateKeyInfo(const StateKeyInfoV2& Source)
	{
		StateKeyInfoV2 Clone;
		Clone.dataType = Source.dataType;
		Clone.defaultValue = CloneJsonValue(Source.defaultValue);
		Clone.limitValues = Source.limitValues;
		Clone.defaultValueArray.Empty();
		for (const TSharedPtr<FJsonValue>& Value : Source.defaultValueArray)
		{
			Clone.defaultValueArray.Add(CloneJsonValue(Value));
		}
		Clone.acceptedValues.Empty();
		for (const TSharedPtr<FJsonValue>& Value : Source.acceptedValues)
		{
			Clone.acceptedValues.Add(CloneJsonValue(Value));
		}
		Clone.ignoredInDataHash = Source.ignoredInDataHash;
		Clone.useMinMax = Source.useMinMax;
		Clone.minValue = Source.minValue;
		Clone.maxValue = Source.maxValue;
		Clone.allowDynamicArraySize = Source.allowDynamicArraySize;
		Clone.allowNullValue = Source.allowNullValue;
		Clone.defaultValueIsNull = Source.defaultValueIsNull;
		Clone.displayDescriptionAsOptions = Source.displayDescriptionAsOptions;
		return Clone;
	}

	bool SupportsNullDefaults(const StateKeyInfoV2& Info)
	{
		return Info.dataType == TEXT("String")
			|| Info.dataType == TEXT("Number")
			|| Info.dataType == TEXT("StringArray")
			|| Info.dataType == TEXT("NumberArray");
	}

	bool IsDefaultEffectivelyNull(const StateKeyInfoV2& Info)
	{
		if (!Info.allowNullValue || !SupportsNullDefaults(Info))
		{
			return false;
		}

		if (Info.defaultValueIsNull)
		{
			return true;
		}

		if (!Info.defaultValue.IsValid())
		{
			return true;
		}

		if (Info.defaultValue->Type == EJson::Null)
		{
			return true;
		}

		if (Info.dataType == TEXT("String"))
		{
			return Info.defaultValue->Type == EJson::String && Info.defaultValue->AsString().TrimStartAndEnd().IsEmpty();
		}

		if (Info.dataType == TEXT("StringArray") || Info.dataType == TEXT("NumberArray"))
		{
			return Info.defaultValue->Type == EJson::Array && Info.defaultValue->AsArray().Num() == 0;
		}

		return false;
	}

	void SyncDefaultValueForKey(
		const FString& KeyPath,
		StateKeyInfoV2& Info,
		const TSharedPtr<FJsonObject>& ActiveJsonObject)
	{
		if (!ActiveJsonObject.IsValid())
		{
			return;
		}

		if (IsDefaultEffectivelyNull(Info))
		{
			Info.defaultValue = MakeShared<FJsonValueNull>();
			Info.defaultValueArray.Empty();
			SetJsonValueAtPath(ActiveJsonObject, KeyPath, Info.defaultValue);
			return;
		}

		Info.defaultValueIsNull = false;
		if (Info.defaultValue.IsValid())
		{
			if (Info.defaultValue->Type == EJson::Array)
			{
				Info.defaultValueArray = Info.defaultValue->AsArray();
			}
			else
			{
				Info.defaultValueArray.Empty();
			}
			SetJsonValueAtPath(ActiveJsonObject, KeyPath, CloneJsonValue(Info.defaultValue));
		}
	}

	TMap<FString, StateKeyInfoV2> CloneStateKeyInfoMap(const TMap<FString, StateKeyInfoV2>& SourceMap)
	{
		TMap<FString, StateKeyInfoV2> ClonedMap;
		for (const TPair<FString, StateKeyInfoV2>& Pair : SourceMap)
		{
			ClonedMap.Add(Pair.Key, CloneStateKeyInfo(Pair.Value));
		}
		return ClonedMap;
	}

	TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& SourceObject)
	{
		if (!SourceObject.IsValid())
		{
			return MakeShared<FJsonObject>();
		}

		const TSharedPtr<FJsonValue> Wrapped = MakeShared<FJsonValueObject>(SourceObject);
		const TSharedPtr<FJsonValue> ClonedValue = CloneJsonValue(Wrapped);
		if (!ClonedValue.IsValid() || ClonedValue->Type != EJson::Object)
		{
			return MakeShared<FJsonObject>();
		}

		return ClonedValue->AsObject();
	}

	TArray<FDIMEModelMetadata> CloneDimeModelData(const TArray<FDIMEModelMetadata>& Source)
	{
		TArray<FDIMEModelMetadata> Cloned = Source;
		return Cloned;
	}

	void MergeDimeModelData(TArray<FDIMEModelMetadata>& InOutData, const TArray<FDIMEModelMetadata>& IncomingData, bool bMerge)
	{
		if (!bMerge)
		{
			InOutData = CloneDimeModelData(IncomingData);
			return;
		}

		for (const FDIMEModelMetadata& IncomingModel : IncomingData)
		{
			const FString IncomingModelName = IncomingModel.ModelName.TrimStartAndEnd();
			if (IncomingModelName.IsEmpty())
			{
				continue;
			}

			FDIMEModelMetadata* ExistingModel = InOutData.FindByPredicate([&IncomingModelName](const FDIMEModelMetadata& Existing)
			{
				return Existing.ModelName.Equals(IncomingModelName, ESearchCase::IgnoreCase);
			});

			if (!ExistingModel)
			{
				InOutData.Add(IncomingModel);
				continue;
			}

			ExistingModel->ModelName = IncomingModelName;
			ExistingModel->DescriptionLookupById = IncomingModel.DescriptionLookupById;
			ExistingModel->Codes = IncomingModel.Codes;
		}
	}

	double RoundEditorNumber(const double InValue)
	{
		return FMath::RoundToDouble(InValue * 1000.0) / 1000.0;
	}

	bool SplitModelGroupKey(const FString& FullKey, FString& OutModelName, FString& OutGroupName)
	{
		FString CleanKey = FullKey;
		const FString IndexToken = TEXT("_INDEX_");
		if (CleanKey.Contains(IndexToken))
		{
			const int32 IndexStart = CleanKey.Find(IndexToken, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
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

	FString ResolveCodeDescription(const TArray<FDIMEModelMetadata>& DimeModelData, const FString& ModelName, const FString& GroupName, const FString& Code)
	{
		for (const FDIMEModelMetadata& ModelMetadata : DimeModelData)
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

	void ApplyAutoPopulateFieldMetaToState(
		const FZLAutoPopulateFieldMeta& FieldMeta,
		TMap<FString, StateKeyInfoV2>& InOutKeyInfos,
		const TSharedPtr<FJsonObject>& InOutActiveJsonObject,
		const bool bMergeAcceptedValues,
		const bool bPreserveExistingDefault,
		const TSharedPtr<FJsonValue>& PreservedDefaultValue)
	{
		if (FieldMeta.KeyPath.IsEmpty())
		{
			return;
		}

		const bool bKeyAlreadyExisted = InOutKeyInfos.Contains(FieldMeta.KeyPath);
		StateKeyInfoV2& Info = InOutKeyInfos.FindOrAdd(FieldMeta.KeyPath);

		// In additive mode, respect a user who manually changed an existing key to an
		// array data type: keep that data type (and the other tickbox states on the key)
		// while still merging in updated accepted values and metadata below.
		const FString ExistingDataType = Info.dataType;
		const bool bExistingIsArray =
			ExistingDataType == TEXT("StringArray")
			|| ExistingDataType == TEXT("NumberArray")
			|| ExistingDataType == TEXT("BoolArray");
		const bool bPreserveExistingArrayType =
			bMergeAcceptedValues
			&& bKeyAlreadyExisted
			&& bExistingIsArray
			&& !ExistingDataType.Equals(FieldMeta.DataType);

		if (!bPreserveExistingArrayType && !FieldMeta.DataType.IsEmpty())
		{
			Info.dataType = FieldMeta.DataType;
		}
		if (!bPreserveExistingArrayType)
		{
			Info.allowNullValue = FieldMeta.bAllowNullValue;
			Info.defaultValueIsNull = FieldMeta.bDefaultValueIsNull;
		}

		if (bPreserveExistingDefault && PreservedDefaultValue.IsValid())
		{
			Info.defaultValue = CloneJsonValue(PreservedDefaultValue);
		}
		else if (bPreserveExistingArrayType)
		{
			// Keep the user's existing default; only synthesise one if it is missing
			// or no longer matches the preserved array data type.
			if (!Info.defaultValue.IsValid() || Info.defaultValue->Type != EJson::Array)
			{
				Info.ResetDefaultValue();
			}
		}
		else if (FieldMeta.DefaultValue.IsValid())
		{
			Info.defaultValue = CloneJsonValue(FieldMeta.DefaultValue);
		}
		else if (!Info.defaultValue.IsValid() && Info.dataType != TEXT("Select Type"))
		{
			Info.ResetDefaultValue();
		}

		if (!bPreserveExistingArrayType)
		{
			Info.limitValues = FieldMeta.bLimitValues;
		}
		if (!bMergeAcceptedValues)
		{
			Info.acceptedValues.Empty();
		}
		for (const TSharedPtr<FJsonValue>& AcceptedValue : FieldMeta.AcceptedValues)
		{
			if (!AcceptedValue.IsValid())
			{
				continue;
			}

			const bool bAlreadyPresent = Info.acceptedValues.ContainsByPredicate(
				[&AcceptedValue](const TSharedPtr<FJsonValue>& Existing)
			{
				if (!Existing.IsValid() || Existing->Type != AcceptedValue->Type)
				{
					return false;
				}

				switch (AcceptedValue->Type)
				{
				case EJson::String:
					return Existing->AsString() == AcceptedValue->AsString();
				case EJson::Number:
					return Existing->AsNumber() == AcceptedValue->AsNumber();
				case EJson::Boolean:
					return Existing->AsBool() == AcceptedValue->AsBool();
				case EJson::Null:
					return true;
				default:
					return Existing == AcceptedValue;
				}
			});
			if (!bAlreadyPresent)
			{
				Info.acceptedValues.Add(CloneJsonValue(AcceptedValue));
			}
		}
		if (!bPreserveExistingArrayType)
		{
			Info.ignoredInDataHash = FieldMeta.bIgnoredInDataHashes;
		}
		SyncDefaultValueForKey(FieldMeta.KeyPath, Info, InOutActiveJsonObject);
	}
}

TMap<FString, FStateKeyInfo> FZLStateEditorV2::ConvertToSerializableMap(const TMap<FString, StateKeyInfoV2>& StateKeyInfoMap)
{
	TMap<FString, FStateKeyInfo> SerializableMap;

	for (const auto& Pair : StateKeyInfoMap)
	{
		const FString& Key = Pair.Key;
		const StateKeyInfoV2& Info = Pair.Value;

		FStateKeyInfo Serializable;

		Serializable.DataType = Info.dataType;
		Serializable.bLimitValues = Info.limitValues;
		Serializable.bIgnoredInDataHashes = Info.ignoredInDataHash;
		Serializable.bUseMinMax = Info.useMinMax;
		Serializable.MinValue = Info.minValue;
		Serializable.MaxValue = Info.maxValue;
		Serializable.bAllowDynamicArraySize = Info.allowDynamicArraySize;
		Serializable.bAllowNullValue = Info.allowNullValue;
		Serializable.bDefaultValueIsNull = Info.defaultValueIsNull;
		Serializable.bDisplayDescriptionAsOptions = Info.displayDescriptionAsOptions;

		for (const TSharedPtr<FJsonValue>& Accepted : Info.acceptedValues)
		{
			if (Accepted.IsValid())
			{
				if (Info.dataType == "String" || Info.dataType == "StringArray" || Info.dataType == "BoolArray")
					Serializable.AcceptedStringValues.Add(Accepted->AsString());
				else if (Info.dataType == "Number" || Info.dataType == "NumberArray")
					Serializable.AcceptedNumberValues.Add(Accepted->AsNumber());
			}
		}

		if (Info.defaultValue.IsValid() && !(Info.allowNullValue && Info.defaultValueIsNull))
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

bool FZLStateEditorV2::SaveAssetToPath(const FString& SaveFileName)
{
	if (SaveFileName.IsEmpty())
	{
		return false;
	}

	FString PackageName;
	FString AssetName;
	FPackageName::TryConvertFilenameToLongPackageName(SaveFileName, PackageName);
	AssetName = FPaths::GetBaseFilename(SaveFileName);

	if (!FPackageName::IsValidObjectPath(PackageName + TEXT(".") + AssetName))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid package path: %s"), *PackageName);
		return false;
	}

	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();

	UStateKeyInfoAsset* NewAsset = NewObject<UStateKeyInfoAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	NewAsset->KeyInfos = ConvertToSerializableMap(keyInfos);
	NewAsset->DimeModelData = DimeModelData;

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

	lastOpenSchemaAssetPath = SaveFileName;
	SaveLastOpenedSchemaPathToConfig();

	const FString SchemaFileName = FPaths::GetPath(SaveFileName) / (AssetName + TEXT(".zlschema"));

	FString JsonOutputString;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonOutputString);

	if (FJsonSerializer::Serialize(NewAsset->SerializeStateKeyAsset_ZLSchemaFile(), JsonWriter))
	{
		if (FFileHelper::SaveStringToFile(JsonOutputString, *SchemaFileName))
		{
			UE_LOG(LogTemp, Log, TEXT("Schema JSON saved to: %s"), *SchemaFileName);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to save schema JSON file: %s"), *SchemaFileName);
			return false;
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to serialize JSON for schema file"));
		return false;
	}

	MarkDocumentClean();
	return true;
}

bool FZLStateEditorV2::SaveSchemaAfterAutoPopulate()
{
	if (!lastOpenSchemaAssetPath.IsEmpty())
	{
		return SaveAssetToPath(lastOpenSchemaAssetPath);
	}

	return SaveAssetFromMap();
}

bool FZLStateEditorV2::SaveAssetFromMap()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return false;
	}

	TArray<FString> SavePath;
	FString DefaultPath = FPaths::ProjectContentDir();
	FString DefaultFileName = TEXT("NewSchemaAsset");

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
		return SaveAssetToPath(FString(SavePath[0]));
	}

	return false;
}

void FZLStateEditorV2::LoadFromUAsset()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	const FString DefaultPath = lastOpenSchemaAssetPath.IsEmpty() ? FPaths::ProjectContentDir() : FPaths::GetPath(lastOpenSchemaAssetPath);
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
		LoadSchemaFromFilePath(OutFiles[0], true);
	}
}

static TSharedPtr<FJsonValue> ParseSchemaAndBuildDataRecursiveV2(
	const TSharedPtr<FJsonObject>& SchemaNode,
	FString CurrentPath,
	TMap<FString, StateKeyInfoV2>& OutMap)
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

				TSharedPtr<FJsonValue> ChildValue = ParseSchemaAndBuildDataRecursiveV2(Pair.Value->AsObject(), NewPath, OutMap);

				if (ChildValue.IsValid())
				{
					DataObject->SetField(KeyName, ChildValue);
				}
			}
		}

		return MakeShared<FJsonValueObject>(DataObject);
	}

	StateKeyInfoV2 NewInfo;

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

			if ((ItemType == "number" || ItemType == "integer")
				&& (*ItemsObj)->HasField("minimum")
				&& (*ItemsObj)->HasField("maximum"))
			{
				NewInfo.useMinMax = true;
				NewInfo.minValue = (*ItemsObj)->GetNumberField("minimum");
				NewInfo.maxValue = (*ItemsObj)->GetNumberField("maximum");
			}
		}

		if (SchemaNode->HasField("x-zl-allowDynamicArraySize"))
		{
			NewInfo.allowDynamicArraySize = SchemaNode->GetBoolField("x-zl-allowDynamicArraySize");
		}
		else if (SchemaNode->HasField("minItems") && SchemaNode->HasField("maxItems"))
		{
			const int32 MinItems = static_cast<int32>(SchemaNode->GetNumberField("minItems"));
			const int32 MaxItems = static_cast<int32>(SchemaNode->GetNumberField("maxItems"));
			NewInfo.allowDynamicArraySize = MinItems != MaxItems;
		}
	}

	if ((JsonType == "number" || JsonType == "integer")
		&& SchemaNode->HasField("minimum")
		&& SchemaNode->HasField("maximum"))
	{
		NewInfo.useMinMax = true;
		NewInfo.minValue = SchemaNode->GetNumberField("minimum");
		NewInfo.maxValue = SchemaNode->GetNumberField("maximum");
	}

	if (SchemaNode->HasField("x-zl-allowNullValue"))
	{
		NewInfo.allowNullValue = SchemaNode->GetBoolField("x-zl-allowNullValue");
	}
	if (SchemaNode->HasField("x-zl-defaultValueIsNull"))
	{
		NewInfo.defaultValueIsNull = SchemaNode->GetBoolField("x-zl-defaultValueIsNull");
	}
	if (SchemaNode->HasField("x-zl-displayDescriptionAsOptions"))
	{
		NewInfo.displayDescriptionAsOptions = SchemaNode->GetBoolField("x-zl-displayDescriptionAsOptions");
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
		NewInfo.defaultValueIsNull = DefVal.IsValid() && DefVal->Type == EJson::Null;

		if (NewInfo.IsArray())
		{
			NewInfo.defaultValueArray = DefVal->AsArray();
		}
	}
	else
	{
		if (NewInfo.allowNullValue && NewInfo.defaultValueIsNull)
		{
			NewInfo.defaultValue = MakeShared<FJsonValueNull>();
			NewInfo.defaultValueArray.Empty();
		}
		else
		{
			NewInfo.ResetDefaultValue();
		}
	}

	OutMap.Add(CurrentPath, NewInfo);

	return NewInfo.defaultValue;
}

void FZLStateEditorV2::LoadFromJsonSchema(TSharedPtr<FJsonObject> Schema)
{
	if (!Schema.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("LoadFromJsonSchema: Invalid Schema Object"));
		return;
	}

	keyInfos.Empty();
	ActiveJsonObject.Reset();
	TSharedPtr<FJsonValue> ResultValue = ParseSchemaAndBuildDataRecursiveV2(Schema, "", keyInfos);

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

TMap<FString, StateKeyInfoV2> FZLStateEditorV2::ConvertToEditorMap(const TMap<FString, FStateKeyInfo>& SavedAsset, TSharedPtr<FJsonObject>& OutJsonObject)
{
	OutJsonObject = MakeShared<FJsonObject>();

	TMap<FString, StateKeyInfoV2> RuntimeMap;

	for (const auto& Pair : SavedAsset)
	{
		const FString& FullKey = Pair.Key;
		const FStateKeyInfo& Info = Pair.Value;

		if (FullKey.EndsWith("."))
		{
			UE_LOG(LogZLStateEditorV2, Warning, TEXT("Invalid formatted subkey %s skipped"), *FullKey)
				continue;
		}

		StateKeyInfoV2 RuntimeInfo;
		RuntimeInfo.dataType = Info.DataType;
		RuntimeInfo.limitValues = Info.bLimitValues;
		RuntimeInfo.ignoredInDataHash = Info.bIgnoredInDataHashes;
		RuntimeInfo.useMinMax = Info.bUseMinMax;
		RuntimeInfo.minValue = Info.MinValue;
		RuntimeInfo.maxValue = Info.MaxValue;
		RuntimeInfo.allowDynamicArraySize = Info.bAllowDynamicArraySize;
		RuntimeInfo.allowNullValue = Info.bAllowNullValue;
		RuntimeInfo.defaultValueIsNull = Info.bDefaultValueIsNull;
		RuntimeInfo.displayDescriptionAsOptions = Info.bDisplayDescriptionAsOptions;

		TSharedPtr<FJsonValue> JsonValue;

		if (RuntimeInfo.allowNullValue && RuntimeInfo.defaultValueIsNull)
		{
			RuntimeInfo.defaultValue = MakeShared<FJsonValueNull>();
			JsonValue = RuntimeInfo.defaultValue;
		}
		else if (Info.DataType == "String")
		{
			RuntimeInfo.defaultValue = MakeShared<FJsonValueString>(Info.DefaultStringValue);
			JsonValue = RuntimeInfo.defaultValue;
		}
		else if (Info.DataType == "Number")
		{
			RuntimeInfo.defaultValue = MakeShared<FJsonValueNumber>(Info.DefaultNumberValue);
			JsonValue = RuntimeInfo.defaultValue;
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
		}
		else if (Info.DataType == "NumberArray")
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (double Val : Info.DefaultNumberArray)
				Arr.Add(MakeShared<FJsonValueNumber>(Val));
			RuntimeInfo.defaultValue = MakeShared<FJsonValueArray>(Arr);
			JsonValue = RuntimeInfo.defaultValue;
		}
		else if (Info.DataType == "BoolArray")
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (bool Val : Info.DefaultBoolArray)
				Arr.Add(MakeShared<FJsonValueBoolean>(Val));
			RuntimeInfo.defaultValue = MakeShared<FJsonValueArray>(Arr);
			JsonValue = RuntimeInfo.defaultValue;
		}

		// Accepted values are independent of whether the default is null.
		if (Info.DataType == "String" || Info.DataType == "StringArray" || Info.DataType == "BoolArray")
		{
			for (const FString& Val : Info.AcceptedStringValues)
			{
				RuntimeInfo.acceptedValues.Add(MakeShared<FJsonValueString>(Val));
			}
		}
		else if (Info.DataType == "Number" || Info.DataType == "NumberArray")
		{
			for (const double Val : Info.AcceptedNumberValues)
			{
				RuntimeInfo.acceptedValues.Add(MakeShared<FJsonValueNumber>(Val));
			}
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

void FZLStateEditorV2::RefreshEditorFromState()
{
	UpdateJsonStr();
	UpdateJsonData(s_currentJsonStr);
}

void FZLStateEditorV2::SaveLastOpenedSchemaPathToConfig() const
{
	if (!GConfig)
	{
		return;
	}

	ZLStateEditorV2SchemaPersistence::EnsureConfigFileLoaded();
	const FString ConfigPath = ZLStateEditorV2SchemaPersistence::GetConfigFilePath();
	const FString RelativePath = ZLStateEditorV2SchemaPersistence::ToRelativeProjectPath(lastOpenSchemaAssetPath);

	GConfig->SetString(
		ZLStateEditorV2SchemaPersistence::ConfigSection,
		ZLStateEditorV2SchemaPersistence::LastSchemaPathKey,
		*RelativePath,
		ConfigPath);
	GConfig->Flush(false, ConfigPath);
}

void FZLStateEditorV2::LoadLastOpenedSchemaPathFromConfig()
{
	if (!GConfig)
	{
		return;
	}

	ZLStateEditorV2SchemaPersistence::MigrateFromLegacyEditorPerProjectIniIfNeeded();
	ZLStateEditorV2SchemaPersistence::EnsureConfigFileLoaded();
	const FString ConfigPath = ZLStateEditorV2SchemaPersistence::GetConfigFilePath();

	FString LoadedPath;
	GConfig->GetString(
		ZLStateEditorV2SchemaPersistence::ConfigSection,
		ZLStateEditorV2SchemaPersistence::LastSchemaPathKey,
		LoadedPath,
		ConfigPath);

	lastOpenSchemaAssetPath = ZLStateEditorV2SchemaPersistence::ToAbsoluteProjectPath(LoadedPath);
}

void FZLStateEditorV2::ApplyDefaultAutoPopulateOptionSelections()
{
	SelectedAutoPopulateOptions.Empty();

	for (const TSharedPtr<FString>& OptionPtr : AutoPopulateOptionLabels)
	{
		if (!OptionPtr.IsValid())
		{
			continue;
		}

		if (!OptionPtr->Equals(TEXT("Model Configuration"), ESearchCase::CaseSensitive)
			&& !OptionPtr->Equals(TEXT("Camera State"), ESearchCase::CaseSensitive))
		{
			SelectedAutoPopulateOptions.Add(*OptionPtr);
		}
	}
}

void FZLStateEditorV2::SaveSelectedAutoPopulateOptionsToConfig() const
{
	if (!GConfig)
	{
		return;
	}

	ZLStateEditorV2SchemaPersistence::EnsureConfigFileLoaded();
	const FString ConfigPath = ZLStateEditorV2SchemaPersistence::GetConfigFilePath();

	TArray<FString> OptionsArray = SelectedAutoPopulateOptions.Array();
	OptionsArray.Sort();

	GConfig->SetBool(
		ZLStateEditorV2SchemaPersistence::ConfigSection,
		ZLStateEditorV2SchemaPersistence::bAutoPopulateOptionsSavedKey,
		true,
		ConfigPath);
	GConfig->SetArray(
		ZLStateEditorV2SchemaPersistence::ConfigSection,
		ZLStateEditorV2SchemaPersistence::SelectedAutoPopulateOptionsKey,
		OptionsArray,
		ConfigPath);
	GConfig->Flush(false, ConfigPath);
}

void FZLStateEditorV2::SetSelectedAutoPopulateOptions(const TArray<FString>& Options)
{
	RefreshAutoPopulateOptions();

	TSet<FString> AvailableOptions;
	for (const TSharedPtr<FString>& OptionPtr : AutoPopulateOptionLabels)
	{
		if (OptionPtr.IsValid())
		{
			AvailableOptions.Add(*OptionPtr);
		}
	}

	SelectedAutoPopulateOptions.Empty();
	for (const FString& Option : Options)
	{
		if (AvailableOptions.Contains(Option))
		{
			SelectedAutoPopulateOptions.Add(Option);
		}
	}

	SaveSelectedAutoPopulateOptionsToConfig();
}

void FZLStateEditorV2::LoadSelectedAutoPopulateOptionsFromConfig()
{
	if (!GConfig)
	{
		ApplyDefaultAutoPopulateOptionSelections();
		return;
	}

	ZLStateEditorV2SchemaPersistence::MigrateFromLegacyEditorPerProjectIniIfNeeded();
	ZLStateEditorV2SchemaPersistence::EnsureConfigFileLoaded();
	const FString ConfigPath = ZLStateEditorV2SchemaPersistence::GetConfigFilePath();

	bool bHasSavedOptions = false;
	if (!GConfig->GetBool(
		ZLStateEditorV2SchemaPersistence::ConfigSection,
		ZLStateEditorV2SchemaPersistence::bAutoPopulateOptionsSavedKey,
		bHasSavedOptions,
		ConfigPath)
		|| !bHasSavedOptions)
	{
		ApplyDefaultAutoPopulateOptionSelections();
		return;
	}

	TSet<FString> AvailableOptions;
	for (const TSharedPtr<FString>& OptionPtr : AutoPopulateOptionLabels)
	{
		if (OptionPtr.IsValid())
		{
			AvailableOptions.Add(*OptionPtr);
		}
	}

	TArray<FString> SavedOptions;
	GConfig->GetArray(
		ZLStateEditorV2SchemaPersistence::ConfigSection,
		ZLStateEditorV2SchemaPersistence::SelectedAutoPopulateOptionsKey,
		SavedOptions,
		ConfigPath);

	SelectedAutoPopulateOptions.Empty();
	for (const FString& Option : SavedOptions)
	{
		if (AvailableOptions.Contains(Option))
		{
			SelectedAutoPopulateOptions.Add(Option);
		}
	}
}

bool FZLStateEditorV2::LoadSchemaFromFilePath(const FString& FilePath, bool bPersistPath)
{
	const FString FileExtension = FPaths::GetExtension(FilePath).ToLower();

	if (FileExtension == TEXT("zlschema"))
	{
		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to read schema JSON file: %s"), *FilePath);
			return false;
		}

		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to parse JSON from schema file: %s"), *FilePath);
			return false;
		}

		TSharedPtr<FJsonObject> SchemaObjectToLoad = JsonObject;
		DimeModelData.Empty();

		const TSharedPtr<FJsonObject>* WrappedSchemaObject = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("jsonSchema"), WrappedSchemaObject) && WrappedSchemaObject && WrappedSchemaObject->IsValid())
		{
			SchemaObjectToLoad = *WrappedSchemaObject;
			const TSharedPtr<FJsonValue> DimeModelDataValue = JsonObject->TryGetField(TEXT("dimeModelData"));
			UStateKeyInfoAsset::DeserializeDimeModelDataFromJsonValue(DimeModelDataValue, DimeModelData);
		}

		LoadFromJsonSchema(SchemaObjectToLoad);
		lastOpenSchemaAssetPath = FilePath;
		if (bPersistPath)
		{
			SaveLastOpenedSchemaPathToConfig();
		}

		MarkDocumentClean();
		UE_LOG(LogTemp, Log, TEXT("Loaded schema from JSON file: %s"), *FilePath);
		return true;
	}

	if (FileExtension == TEXT("uasset"))
	{
		FString PackageName;
		if (!FPackageName::TryConvertFilenameToLongPackageName(FilePath, PackageName))
		{
			UE_LOG(LogTemp, Error, TEXT("Could not convert filename to package: %s"), *FilePath);
			return false;
		}

		const FString AssetName = FPaths::GetBaseFilename(FilePath);

		UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		if (!Package)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to load package: %s"), *PackageName);
			return false;
		}

		UStateKeyInfoAsset* LoadedAsset = FindObject<UStateKeyInfoAsset>(Package, *AssetName);
		if (!LoadedAsset)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to find asset in package: %s (make sure this is a schema .uasset or .zlschema file)"), *AssetName);
			return false;
		}

		keyInfos = ConvertToEditorMap(LoadedAsset->KeyInfos, ActiveJsonObject);
		DimeModelData = LoadedAsset->DimeModelData;
		RefreshEditorFromState();
		lastOpenSchemaAssetPath = FilePath;
		if (bPersistPath)
		{
			SaveLastOpenedSchemaPathToConfig();
		}

		MarkDocumentClean();
		UE_LOG(LogTemp, Log, TEXT("Loaded %d keys from asset."), keyInfos.Num());
		return true;
	}

	UE_LOG(LogTemp, Error, TEXT("Unsupported file type: %s (expected .uasset or .zlschema)"), *FileExtension);
	return false;
}

FString FZLStateEditorV2::MakeUniqueDuplicateKey(const FString& SourceKey) const
{
	FString ParentPath;
	FString LeafName;
	if (!ZLStateEditorV2Internal::SplitKeyPath(SourceKey, ParentPath, LeafName))
	{
		return FString();
	}

	int32 Suffix = 1;
	FString CandidateKey;
	do
	{
		const FString CandidateLeaf = FString::Printf(TEXT("%s_%d"), *LeafName, Suffix++);
		CandidateKey = ParentPath.IsEmpty() ? CandidateLeaf : ParentPath + TEXT(".") + CandidateLeaf;
	}
	while (keyInfos.Contains(CandidateKey));

	return CandidateKey;
}

void FZLStateEditorV2::DuplicateKey(const FString& SourceKey)
{
	if (!keyInfos.Contains(SourceKey))
	{
		return;
	}

	const FString DuplicateKeyName = MakeUniqueDuplicateKey(SourceKey);
	if (DuplicateKeyName.IsEmpty())
	{
		return;
	}

	keyInfos.Add(DuplicateKeyName, keyInfos[SourceKey]);

	const TSharedPtr<FJsonValue> SourceValue = ZLStateEditorV2Internal::GetJsonValueAtPath(ActiveJsonObject, SourceKey);
	if (SourceValue.IsValid())
	{
		ZLStateEditorV2Internal::SetJsonValueAtPath(ActiveJsonObject, DuplicateKeyName, ZLStateEditorV2Internal::CloneJsonValue(SourceValue));
	}

	if (AreaExpansionStates.Contains(SourceKey))
	{
		AreaExpansionStates.Add(DuplicateKeyName, AreaExpansionStates[SourceKey]);
	}
	else
	{
		AreaExpansionStates.Add(DuplicateKeyName, TPair<bool, bool>(true, true));
	}

	RefreshEditorFromState();
}

void FZLStateEditorV2::DeleteKey(const FString& Key)
{
	if (!keyInfos.Contains(Key))
	{
		return;
	}

	keyInfos.Remove(Key);
	AreaExpansionStates.Remove(Key);
	ZLStateEditorV2Internal::RemoveJsonValueAtPath(ActiveJsonObject, Key);

	if (keyBeingRenamed == Key)
	{
		keyBeingRenamed = TEXT("");
		pendingRenameText = TEXT("");
	}

	RefreshEditorFromState();
}

void FZLStateEditorV2::BeginRenameKey(const FString& Key)
{
	if (!keyInfos.Contains(Key))
	{
		return;
	}

	keyBeingRenamed = Key;
	pendingRenameText = Key;
	UpdateJsonData(s_currentJsonStr);
}

void FZLStateEditorV2::CommitRenameKey(const FString& OriginalKey, const FString& ProposedKey)
{
	if (!keyInfos.Contains(OriginalKey))
	{
		keyBeingRenamed = TEXT("");
		pendingRenameText = TEXT("");
		return;
	}

	FString SanitizedKey = ProposedKey;
	SanitizedKey.TrimStartAndEndInline();

	if (SanitizedKey.IsEmpty() || SanitizedKey.EndsWith(TEXT(".")))
	{
		UE_LOG(LogZLStateEditorV2, Warning, TEXT("Cannot rename key '%s' to invalid key '%s'"), *OriginalKey, *SanitizedKey);
		return;
	}

	if (SanitizedKey == OriginalKey)
	{
		keyBeingRenamed = TEXT("");
		pendingRenameText = TEXT("");
		UpdateJsonData(s_currentJsonStr);
		return;
	}

	if (keyInfos.Contains(SanitizedKey))
	{
		UE_LOG(LogZLStateEditorV2, Warning, TEXT("Cannot rename key '%s' because '%s' already exists"), *OriginalKey, *SanitizedKey);
		return;
	}

	const TSharedPtr<FJsonValue> SourceValue = ZLStateEditorV2Internal::GetJsonValueAtPath(ActiveJsonObject, OriginalKey);
	if (!SourceValue.IsValid())
	{
		UE_LOG(LogZLStateEditorV2, Warning, TEXT("Could not locate JSON value for key '%s' while renaming"), *OriginalKey);
		return;
	}

	if (!ZLStateEditorV2Internal::SetJsonValueAtPath(ActiveJsonObject, SanitizedKey, ZLStateEditorV2Internal::CloneJsonValue(SourceValue)))
	{
		UE_LOG(LogZLStateEditorV2, Warning, TEXT("Failed to set JSON field for renamed key '%s'"), *SanitizedKey);
		return;
	}

	ZLStateEditorV2Internal::RemoveJsonValueAtPath(ActiveJsonObject, OriginalKey);
	keyInfos.Add(SanitizedKey, keyInfos[OriginalKey]);
	keyInfos.Remove(OriginalKey);

	if (AreaExpansionStates.Contains(OriginalKey))
	{
		AreaExpansionStates.Add(SanitizedKey, AreaExpansionStates[OriginalKey]);
		AreaExpansionStates.Remove(OriginalKey);
	}

	keyBeingRenamed = TEXT("");
	pendingRenameText = TEXT("");
	RefreshEditorFromState();
}

void FZLStateEditorV2::RefreshAutoPopulateOptions()
{
	AutoPopulateOptionLabels.Empty();

	const FName FeatureName = IZLOmniStream_SchemaAutoPopulate::GetModularFeatureName();
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	const int32 NumImpls = ModularFeatures.GetModularFeatureImplementationCount(FeatureName);

	TSet<FString> SeenLabels;
	for (int32 i = 0; i < NumImpls; ++i)
	{
		IModularFeature* RawImpl = ModularFeatures.GetModularFeatureImplementation(FeatureName, i);
		IZLOmniStream_SchemaAutoPopulate* Impl = static_cast<IZLOmniStream_SchemaAutoPopulate*>(RawImpl);
		if (!Impl)
		{
			continue;
		}

		for (const FString& Label : Impl->GetAutoPopulateOptionNames())
		{
			if (Label.IsEmpty() || SeenLabels.Contains(Label))
			{
				continue;
			}
			SeenLabels.Add(Label);
			AutoPopulateOptionLabels.Add(MakeShared<FString>(Label));
		}
	}

	AutoPopulateOptionLabels.Sort([](const TSharedPtr<FString>& A, const TSharedPtr<FString>& B)
	{
		return (A.IsValid() && B.IsValid()) ? (*A < *B) : false;
	});

	// Prune any previously selected options that are no longer reported by any implementation.
	for (auto It = SelectedAutoPopulateOptions.CreateIterator(); It; ++It)
	{
		if (!SeenLabels.Contains(*It))
		{
			It.RemoveCurrent();
		}
	}
}

bool FZLStateEditorV2::KeyHasDescribedAcceptedValues(const FString& Key) const
{
	const StateKeyInfoV2* Info = keyInfos.Find(Key);
	if (!Info)
	{
		return false;
	}

	FString ModelName;
	FString GroupName;
	if (!ZLStateEditorV2Internal::SplitModelGroupKey(Key, ModelName, GroupName))
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& AcceptedValue : Info->acceptedValues)
	{
		if (!AcceptedValue.IsValid())
		{
			continue;
		}

		FString Code;
		if (AcceptedValue->Type == EJson::String)
		{
			Code = AcceptedValue->AsString();
		}
		else if (AcceptedValue->Type == EJson::Number)
		{
			Code = FString::SanitizeFloat(AcceptedValue->AsNumber());
		}

		if (Code.TrimStartAndEnd().IsEmpty())
		{
			continue;
		}

		if (!ZLStateEditorV2Internal::ResolveCodeDescription(DimeModelData, ModelName, GroupName, Code).IsEmpty())
		{
			return true;
		}
	}

	// Fall back to DIME group metadata so the toggle appears even when accepted
	// values have not been edited in the schema UI yet (common for StringArray keys).
	for (const FDIMEModelMetadata& ModelMetadata : DimeModelData)
	{
		if (!ModelMetadata.ModelName.Equals(ModelName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		for (const FDIMEModelCodeMetadata& CodeMetadata : ModelMetadata.Codes)
		{
			if (!CodeMetadata.Group.Equals(GroupName, ESearchCase::IgnoreCase) || CodeMetadata.DescriptionId == INDEX_NONE)
			{
				continue;
			}

			if (const FString* Description = ModelMetadata.DescriptionLookupById.Find(CodeMetadata.DescriptionId))
			{
				if (!Description->TrimStartAndEnd().IsEmpty())
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool FZLStateEditorV2::ShouldShowDisplayDescriptionAsOptionsCheckbox(const FString& Key) const
{
	const StateKeyInfoV2* Info = keyInfos.Find(Key);
	if (!Info || !KeyHasDescribedAcceptedValues(Key))
	{
		return false;
	}

	if (Info->dataType == TEXT("BoolArray"))
	{
		return Info->acceptedValues.Num() > 0;
	}

	if (Info->dataType == TEXT("String")
		|| Info->dataType == TEXT("Number")
		|| Info->dataType == TEXT("StringArray")
		|| Info->dataType == TEXT("NumberArray"))
	{
		return Info->limitValues || Info->displayDescriptionAsOptions;
	}

	return false;
}

void FZLStateEditorV2::AutoPopulateSchema(
	const TArray<FString>& SelectedOptions,
	const TArray<UWorld*>& Levels,
	bool bAdditive,
	bool bDiscardPendingPreview,
	const FZLAutoPopulateAdvancedSettingsMap& AdvancedSettingsByOption)
{
	if (SelectedOptions.Num() == 0)
	{
		return;
	}

	if (bDiscardPendingPreview)
	{
		DiscardAutoPopulatePreview();
	}

	const FName FeatureName = IZLOmniStream_SchemaAutoPopulate::GetModularFeatureName();
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	const int32 NumImpls = ModularFeatures.GetModularFeatureImplementationCount(FeatureName);

	if (!bAdditive)
	{
		ActiveJsonObject = MakeShared<FJsonObject>();
		keyInfos.Empty();
		DimeModelData.Empty();
	}

	TMap<FString, TSharedPtr<FJsonValue>> ExistingDefaultsByKey;
	if (bAdditive)
	{
		for (const TPair<FString, StateKeyInfoV2>& Pair : keyInfos)
		{
			if (Pair.Value.defaultValue.IsValid())
			{
				ExistingDefaultsByKey.Add(Pair.Key, ZLStateEditorV2Internal::CloneJsonValue(Pair.Value.defaultValue));
			}
		}
	}

	bool bAnyChange = !bAdditive;
	for (int32 i = 0; i < NumImpls; ++i)
	{
		IModularFeature* RawImpl = ModularFeatures.GetModularFeatureImplementation(FeatureName, i);
		IZLOmniStream_SchemaAutoPopulate* Impl = static_cast<IZLOmniStream_SchemaAutoPopulate*>(RawImpl);
		if (!Impl)
		{
			continue;
		}

		TArray<FString> MatchedOptions;
		for (const FString& Label : Impl->GetAutoPopulateOptionNames())
		{
			if (SelectedOptions.Contains(Label))
			{
				MatchedOptions.Add(Label);
			}
		}

		if (MatchedOptions.Num() == 0)
		{
			continue;
		}

		const FZLAutoPopulateResult AutoPopulateResult = Impl->AutoPopulateWithAdvancedSettings(Levels, MatchedOptions, AdvancedSettingsByOption);
		if (AutoPopulateResult.JsonContribution.IsValid() && AutoPopulateResult.JsonContribution->Values.Num() > 0)
		{
			ZLStateEditorV2Internal::MergeJsonObjectInto(ActiveJsonObject, AutoPopulateResult.JsonContribution);
			bAnyChange = true;
		}

		if (AutoPopulateResult.FieldMetaByKey.Num() > 0)
		{
			for (const TPair<FString, FZLAutoPopulateFieldMeta>& FieldMetaPair : AutoPopulateResult.FieldMetaByKey)
			{
				const TSharedPtr<FJsonValue>* ExistingDefaultPtr = ExistingDefaultsByKey.Find(FieldMetaPair.Key);
				const bool bPreserveDefault = bAdditive && ExistingDefaultPtr && ExistingDefaultPtr->IsValid();
				const TSharedPtr<FJsonValue> PreservedDefault = bPreserveDefault ? *ExistingDefaultPtr : nullptr;
				ZLStateEditorV2Internal::ApplyAutoPopulateFieldMetaToState(
					FieldMetaPair.Value,
					keyInfos,
					ActiveJsonObject,
					bAdditive,
					bPreserveDefault,
					PreservedDefault
				);
			}
			bAnyChange = true;
		}

		if (AutoPopulateResult.DimeModelData.Num() > 0)
		{
			ZLStateEditorV2Internal::MergeDimeModelData(DimeModelData, AutoPopulateResult.DimeModelData, bAdditive);
			bAnyChange = true;
		}
	}

	if (bAnyChange)
	{
		RefreshEditorFromState();
	}

	if (bDiscardPendingPreview)
	{
		SelectedAutoPopulateOptions.Empty();
		for (const FString& Option : SelectedOptions)
		{
			SelectedAutoPopulateOptions.Add(Option);
		}
		SaveSelectedAutoPopulateOptionsToConfig();

		if (bAnyChange)
		{
			SaveSchemaAfterAutoPopulate();
		}
	}
}

bool FZLStateEditorV2::BuildAutoPopulatePreview(
	const TArray<FString>& SelectedOptions,
	const TArray<UWorld*>& Levels,
	bool bAdditive,
	FString& OutProposedSchemaText,
	const FZLAutoPopulateAdvancedSettingsMap& AdvancedSettingsByOption)
{
	OutProposedSchemaText = TEXT("");
	if (SelectedOptions.Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> SavedActiveJsonObject = ZLStateEditorV2Internal::CloneJsonObject(ActiveJsonObject);
	const TMap<FString, StateKeyInfoV2> SavedKeyInfos = ZLStateEditorV2Internal::CloneStateKeyInfoMap(keyInfos);
	const TArray<FDIMEModelMetadata> SavedDimeModelData = ZLStateEditorV2Internal::CloneDimeModelData(DimeModelData);
	const TMap<FString, TPair<bool, bool>> SavedAreaExpansionStates = AreaExpansionStates;
	const FString SavedCurrentJsonStr = s_currentJsonStr;
	const FString SavedKeyBeingRenamed = keyBeingRenamed;
	const FString SavedPendingRenameText = pendingRenameText;

	// Build previews incrementally from any pending preview state so Python can
	// process option/level work items one step at a time while keeping progress updates granular.
	const TSharedPtr<FJsonObject> SourcePreviewJsonObject =
		bHasPendingAutoPopulatePreview ? PendingAutoPopulatePreviewJsonObject : SavedActiveJsonObject;
	const TMap<FString, StateKeyInfoV2> SourcePreviewKeyInfos =
		bHasPendingAutoPopulatePreview ? PendingAutoPopulatePreviewKeyInfos : SavedKeyInfos;
	const TArray<FDIMEModelMetadata> SourcePreviewDimeModelData =
		bHasPendingAutoPopulatePreview ? PendingAutoPopulatePreviewDimeModelData : SavedDimeModelData;
	ActiveJsonObject = ZLStateEditorV2Internal::CloneJsonObject(SourcePreviewJsonObject);
	keyInfos = ZLStateEditorV2Internal::CloneStateKeyInfoMap(SourcePreviewKeyInfos);
	DimeModelData = ZLStateEditorV2Internal::CloneDimeModelData(SourcePreviewDimeModelData);

	AutoPopulateSchema(SelectedOptions, Levels, bAdditive, false, AdvancedSettingsByOption);

	PendingAutoPopulatePreviewJsonObject = ZLStateEditorV2Internal::CloneJsonObject(ActiveJsonObject);
	PendingAutoPopulatePreviewKeyInfos = ZLStateEditorV2Internal::CloneStateKeyInfoMap(keyInfos);
	PendingAutoPopulatePreviewDimeModelData = ZLStateEditorV2Internal::CloneDimeModelData(DimeModelData);
	bHasPendingAutoPopulatePreview = true;

	const FString SchemaTitle = !lastOpenSchemaAssetPath.IsEmpty()
		? FPaths::GetBaseFilename(lastOpenSchemaAssetPath)
		: TEXT("Schema");
	const TMap<FString, FStateKeyInfo> SerializablePreviewMap = ConvertToSerializableMap(PendingAutoPopulatePreviewKeyInfos);
	const TSharedRef<FJsonObject> PreviewSchemaObject = UStateKeyInfoAsset::BuildZLSchemaFileObject(
		SerializablePreviewMap,
		SchemaTitle,
		PendingAutoPopulatePreviewDimeModelData);

	PendingAutoPopulatePreviewSchemaText.Empty();
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&PendingAutoPopulatePreviewSchemaText);
	if (!FJsonSerializer::Serialize(PreviewSchemaObject, Writer))
	{
		DiscardAutoPopulatePreview();
		ActiveJsonObject = SavedActiveJsonObject;
		keyInfos = SavedKeyInfos;
		DimeModelData = SavedDimeModelData;
		AreaExpansionStates = SavedAreaExpansionStates;
		keyBeingRenamed = SavedKeyBeingRenamed;
		pendingRenameText = SavedPendingRenameText;
		s_currentJsonStr = SavedCurrentJsonStr;
		RefreshEditorFromState();
		return false;
	}
	OutProposedSchemaText = PendingAutoPopulatePreviewSchemaText;

	ActiveJsonObject = SavedActiveJsonObject;
	keyInfos = SavedKeyInfos;
	DimeModelData = SavedDimeModelData;
	AreaExpansionStates = SavedAreaExpansionStates;
	keyBeingRenamed = SavedKeyBeingRenamed;
	pendingRenameText = SavedPendingRenameText;
	s_currentJsonStr = SavedCurrentJsonStr;
	RefreshEditorFromState();

	return true;
}

bool FZLStateEditorV2::ApplyAutoPopulatePreview()
{
	if (!bHasPendingAutoPopulatePreview || !PendingAutoPopulatePreviewJsonObject.IsValid())
	{
		return false;
	}

	ActiveJsonObject = ZLStateEditorV2Internal::CloneJsonObject(PendingAutoPopulatePreviewJsonObject);
	keyInfos = ZLStateEditorV2Internal::CloneStateKeyInfoMap(PendingAutoPopulatePreviewKeyInfos);
	DimeModelData = ZLStateEditorV2Internal::CloneDimeModelData(PendingAutoPopulatePreviewDimeModelData);
	RefreshEditorFromState();
	DiscardAutoPopulatePreview();
	return SaveSchemaAfterAutoPopulate();
}

void FZLStateEditorV2::DiscardAutoPopulatePreview()
{
	bHasPendingAutoPopulatePreview = false;
	PendingAutoPopulatePreviewJsonObject = nullptr;
	PendingAutoPopulatePreviewKeyInfos.Empty();
	PendingAutoPopulatePreviewDimeModelData.Empty();
	PendingAutoPopulatePreviewSchemaText.Empty();
}

void FZLStateEditorV2::OpenAdvancedAutoPopulateWindow()
{
	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
	{
		UE_LOG(LogZLStateEditorV2, Warning, TEXT("OpenAdvancedAutoPopulateWindow: Python is not available."));
		return;
	}

	TSharedPtr<IPlugin> CloudPlugin = IPluginManager::Get().FindPlugin(TEXT("ZLCloudPlugin"));
	if (!CloudPlugin.IsValid())
	{
		UE_LOG(LogZLStateEditorV2, Warning, TEXT("OpenAdvancedAutoPopulateWindow: ZLCloudPlugin not found."));
		return;
	}

	const FString ScriptPath = FPaths::Combine(CloudPlugin->GetBaseDir(), TEXT("Resources/Python/advancedAutoPopulate.py"));
	if (!FPaths::FileExists(ScriptPath))
	{
		UE_LOG(LogZLStateEditorV2, Warning, TEXT("OpenAdvancedAutoPopulateWindow: Script not found at %s"), *ScriptPath);
		return;
	}

	FPythonCommandEx CommandEx;
	CommandEx.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	CommandEx.Command = ScriptPath;
	PythonPlugin->ExecPythonCommandEx(CommandEx);
}

void FZLStateEditorV2::BeginOpenAdvancedAutoPopulateWindow()
{
	if (bIsOpeningAdvancedAutoPopulateWindow)
	{
		return;
	}

	bIsOpeningAdvancedAutoPopulateWindow = true;
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);

	// Defer the heavy open path long enough for Slate to paint the loading UI first.
	// A tiny delay here avoids the spinner/label appearing only at the end of the blocking call.
	constexpr float PreOpenDisplayDelaySeconds = 0.05f;
	RegisterActiveTimer(PreOpenDisplayDelaySeconds, FWidgetActiveTimerDelegate::CreateSP(this, &FZLStateEditorV2::HandleDeferredOpenAdvancedAutoPopulate));
}

EActiveTimerReturnType FZLStateEditorV2::HandleDeferredOpenAdvancedAutoPopulate(double InCurrentTime, float InDeltaTime)
{
	OpenAdvancedAutoPopulateWindow();
	bIsOpeningAdvancedAutoPopulateWindow = false;
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	return EActiveTimerReturnType::Stop;
}

FZLStateEditorV2::FZLStateEditorV2()
{
}

FZLStateEditorV2::~FZLStateEditorV2()
{
	if (s_LiveInstance.HasSameObject(this))
	{
		s_LiveInstance.Reset();
	}
}

void FZLStateEditorV2::Construct(const FArguments& InArgs)
{
	s_LiveInstance = StaticCastSharedRef<FZLStateEditorV2>(AsShared());

	RefreshAutoPopulateOptions();
	LoadSelectedAutoPopulateOptionsFromConfig();

	TextEditorViewOptions.Empty();
	TextEditorViewOptions.Add(MakeShared<FString>(TEXT("Default State")));
	TextEditorViewOptions.Add(MakeShared<FString>(TEXT("Full Json Schema + Metadata")));
	SelectedTextEditorViewOption = TextEditorViewOptions[0];

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
								.Value(0.7f)
								[
									SNew(SVerticalBox)
										+ SVerticalBox::Slot()
										.AutoHeight()
										.Padding(0, 0, 0, 4)
										[
											SNew(SHorizontalBox)
												+ SHorizontalBox::Slot()
												.AutoWidth()
												.VAlign(VAlign_Center)
												.Padding(0, 0, 8, 0)
												[
													SNew(STextBlock)
														.Text(FText::FromString(TEXT("Text Editor View")))
												]
												+ SHorizontalBox::Slot()
												.FillWidth(1.0f)
												[
													SNew(SComboBox<TSharedPtr<FString>>)
														.OptionsSource(&TextEditorViewOptions)
														.InitiallySelectedItem(SelectedTextEditorViewOption)
														.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
														{
															return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
														})
														.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewSelection, ESelectInfo::Type)
														{
															if (!NewSelection.IsValid())
															{
																return;
															}
															SelectedTextEditorViewOption = NewSelection;
															RefreshJsonTextEditorDisplay();
														})
														[
															SNew(STextBlock)
																.Text_Lambda([this]()
																{
																	return FText::FromString(SelectedTextEditorViewOption.IsValid() ? *SelectedTextEditorViewOption : TEXT("Default State"));
																})
														]
												]
										]
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
																.IsReadOnly_Lambda([this]()
																{
																	return IsFullJsonSchemaAndMetadataView();
																})
																.OnTextChanged(this, &FZLStateEditorV2::OnJsonTextChanged)
														]
												]
										]
								]
								+ SSplitter::Slot()
								.Resizable(false)
								.Value(0.25f)
								[
									SNew(SVerticalBox)
										+ SVerticalBox::Slot()
										.FillHeight(0.5f)
										[
											SNew(SVerticalBox)
												+ SVerticalBox::Slot().Padding(0, 0, 0, 4).AutoHeight()[SNew(SButton)
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

												+ SVerticalBox::Slot().Padding(0, 0, 0, 4).AutoHeight()
												[
													SNew(SVerticalBox)
														+ SVerticalBox::Slot().AutoHeight()
														[
															SNew(SButton)
																.OnClicked_Lambda([this]
															{
																this->BeginOpenAdvancedAutoPopulateWindow();
																return FReply::Handled();
															})
																.IsEnabled_Lambda([this]()
															{
																return !bIsOpeningAdvancedAutoPopulateWindow;
															})
																.Text(FText::FromString("Auto Populate Schema"))
														]
														+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
														[
															SNew(SHorizontalBox)
																.Visibility_Lambda([this]()
															{
																return bIsOpeningAdvancedAutoPopulateWindow ? EVisibility::Visible : EVisibility::Collapsed;
															})
																+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
																[
																	SNew(SThrobber)
																]
																+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0).VAlign(VAlign_Center)
																[
																	SNew(STextBlock)
																		.Text(FText::FromString("Scanning level assets..."))
																]
														]
												]
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
															while (!newKeyStr.IsEmpty() && FString(TEXT("!\"�$%&*")).Contains(newKeyStr.Left(1))) newKeyStr.RemoveAt(0);

															if (newKeyStr.EndsWith("."))
															{
																UE_LOG(LogZLStateEditorV2, Warning, TEXT("Nested key %s must end with valid sub key to be added"), *newKeyStr)
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

	LoadLastOpenedSchemaPathFromConfig();

	bool bAutoLoadedSchema = false;
	if (!lastOpenSchemaAssetPath.IsEmpty() && FPaths::FileExists(lastOpenSchemaAssetPath))
	{
		bAutoLoadedSchema = LoadSchemaFromFilePath(lastOpenSchemaAssetPath, false);
		if (!bAutoLoadedSchema)
		{
			lastOpenSchemaAssetPath = TEXT("");
			SaveLastOpenedSchemaPathToConfig();
		}
	}
	else if (!lastOpenSchemaAssetPath.IsEmpty())
	{
		lastOpenSchemaAssetPath = TEXT("");
		SaveLastOpenedSchemaPathToConfig();
	}

	if (!bAutoLoadedSchema && s_currentJsonStr != "")
	{
		UpdateJsonData(s_currentJsonStr);
	}

	RefreshJsonTextEditorDisplay();
	MarkDocumentClean();
}

FString FZLStateEditorV2::BuildDocumentSnapshot() const
{
	return BuildFullJsonSchemaAndMetadataString();
}

void FZLStateEditorV2::MarkDocumentClean()
{
	SavedDocumentSnapshot = BuildDocumentSnapshot();
}

bool FZLStateEditorV2::HasUnsavedChanges() const
{
	if (bHasPendingAutoPopulatePreview)
	{
		return true;
	}

	return !SavedDocumentSnapshot.Equals(BuildDocumentSnapshot(), ESearchCase::CaseSensitive);
}

bool FZLStateEditorV2::PromptSaveAndClose()
{
	return SaveAssetFromMap();
}

void FZLStateEditorV2::OnJsonTextChanged(const FText& NewText)
{
	if (bSuppressJsonTextChanged || IsFullJsonSchemaAndMetadataView())
	{
		return;
	}

	FString JsonString = NewText.ToString();
	s_currentJsonStr = JsonString;
	UpdateJsonData(JsonString);
	RemoveInvalidKeyInfoEntries();
}

bool FZLStateEditorV2::IsFullJsonSchemaAndMetadataView() const
{
	return SelectedTextEditorViewOption.IsValid() && SelectedTextEditorViewOption->Equals(TEXT("Full Json Schema + Metadata"), ESearchCase::CaseSensitive);
}

FString FZLStateEditorV2::BuildFullJsonSchemaAndMetadataString() const
{
	const FString SchemaTitle = !lastOpenSchemaAssetPath.IsEmpty()
		? FPaths::GetBaseFilename(lastOpenSchemaAssetPath)
		: TEXT("Schema");
	const TMap<FString, FStateKeyInfo> SerializableMap = const_cast<FZLStateEditorV2*>(this)->ConvertToSerializableMap(keyInfos);
	const TSharedRef<FJsonObject> ZLSchemaObject = UStateKeyInfoAsset::BuildZLSchemaFileObject(SerializableMap, SchemaTitle, DimeModelData);

	FString FullSchemaString;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&FullSchemaString);
	FJsonSerializer::Serialize(ZLSchemaObject, Writer);
	return FullSchemaString;
}

void FZLStateEditorV2::RefreshJsonTextEditorDisplay()
{
	if (!JsonTextBox.IsValid())
	{
		return;
	}

	FString DisplayText;
	if (IsFullJsonSchemaAndMetadataView())
	{
		DisplayText = BuildFullJsonSchemaAndMetadataString();
	}
	else
	{
		TSharedPtr<FJsonObject> FilteredObject = ZLStateEditorV2Internal::CloneJsonObject(ActiveJsonObject);
		for (TPair<FString, StateKeyInfoV2>& Pair : keyInfos)
		{
			if (ZLStateEditorV2Internal::IsDefaultEffectivelyNull(Pair.Value))
			{
				ZLStateEditorV2Internal::RemoveJsonValueAtPath(FilteredObject, Pair.Key);
			}
		}

		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&DisplayText);
		FJsonSerializer::Serialize(FilteredObject.ToSharedRef(), Writer);
	}

	TGuardValue<bool> Guard(bSuppressJsonTextChanged, true);
	JsonTextBox->SetText(FText::FromString(DisplayText));
}

void FZLStateEditorV2::RemoveInvalidKeyInfoEntries()
{
	TArray<FString> keysToTrim;
	for (const auto& Pair : keyInfos)
	{
		const FString& FullKey = Pair.Key;
		const StateKeyInfoV2& Info = Pair.Value;

		TArray<FString> Parts;
		FullKey.ParseIntoArray(Parts, TEXT("."), true);

		TSharedPtr<FJsonObject> CurrentObject = ActiveJsonObject;

		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const FString& Part = Parts[i];

			if (!CurrentObject->HasField(Part))
			{
				if (!Info.allowNullValue)
				{
					keysToTrim.Add(FullKey);
				}
			}

			if (i == Parts.Num() - 1)
			{
				continue;
			}

			TSharedPtr<FJsonObject> NextObject = CurrentObject->GetObjectField(Part);
			if (!NextObject.IsValid())
			{
				if (!Info.allowNullValue)
				{
					keysToTrim.Add(FullKey);
				}
			}

			CurrentObject = NextObject;
		}
	}

	for (int i = 0; i < keysToTrim.Num(); i++)
		keyInfos.Remove(keysToTrim[i]);
}

void FZLStateEditorV2::UpdateJsonStr()
{
	FString JsonString_UpdatedSchema;

	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString_UpdatedSchema);
	FJsonSerializer::Serialize(ActiveJsonObject.ToSharedRef(), Writer);
	s_currentJsonStr = JsonString_UpdatedSchema;
	RefreshJsonTextEditorDisplay();

	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

void FZLStateEditorV2::UpdateJsonData(const FString& JsonString)
{
	ButtonsContainer->ClearChildren();
	TSharedPtr<FJsonObject> JsonDataObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (FJsonSerializer::Deserialize(Reader, JsonDataObject) && JsonDataObject.IsValid())
	{
		GenerateUIFromJson(JsonDataObject, "");
	}
}

void FZLStateEditorV2::GenerateUIFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& Prefix, const bool SetActiveObject)
{
	if (SetActiveObject)
		this->ActiveJsonObject = JsonObject;

	TArray<FString> SortedFieldNames;
	JsonObject->Values.GetKeys(SortedFieldNames);
	SortedFieldNames.Sort();

	for (const FString& FieldName : SortedFieldNames)
	{
		const TSharedPtr<FJsonValue>* ValuePtr = JsonObject->Values.Find(FieldName);
		if (!ValuePtr || !ValuePtr->IsValid())
		{
			continue;
		}

		const TPair<FString, TSharedPtr<FJsonValue>> Pair(FieldName, *ValuePtr);
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
						UE_LOG(LogZLStateEditorV2, Log, TEXT("Unknown data type in %s schema key"), *Key)
							continue;
					}
				}
			}
			else
			{
				// Keep foldout default controls in sync when JSON is edited directly.
				keyInfos[Key].defaultValue = ZLStateEditorV2Internal::CloneJsonValue(Pair.Value);
				if (Pair.Value->Type == EJson::Array)
				{
					keyInfos[Key].defaultValueArray = Pair.Value->AsArray();
				}
				else
				{
					keyInfos[Key].defaultValueArray.Empty();
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
							SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("NoBorder"))
								.Padding(2.0f)
								.OnMouseButtonDown_Lambda([this, Key](const FGeometry&, const FPointerEvent& MouseEvent)
							{
								if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton)
								{
									return FReply::Unhandled();
								}

								FMenuBuilder MenuBuilder(true, nullptr);
								MenuBuilder.AddMenuEntry(
									FText::FromString("Duplicate"),
									FText::FromString("Duplicate this key using a unique _N suffix"),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([this, Key]()
								{
									DuplicateKey(Key);
								}))
								);
								MenuBuilder.AddMenuEntry(
									FText::FromString("Rename"),
									FText::FromString("Rename this key"),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([this, Key]()
								{
									BeginRenameKey(Key);
								}))
								);
								MenuBuilder.AddMenuEntry(
									FText::FromString("Delete"),
									FText::FromString("Delete this key"),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([this, Key]()
								{
									DeleteKey(Key);
								}))
								);

								FSlateApplication::Get().PushMenu(
									AsShared(),
									FWidgetPath(),
									MenuBuilder.MakeWidget(),
									MouseEvent.GetScreenSpacePosition(),
									FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
								);

								return FReply::Handled();
							})
								[
									SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.FillWidth(1.0f)
										.Padding(0, 0, 0, 0)
										.VAlign(VAlign_Center)
										[
											SNew(SBox)
												.MaxDesiredWidth(600.0f)
												[
													(keyBeingRenamed == Key)
													? StaticCastSharedRef<SWidget>(SNew(SEditableTextBox)
														.Text_Lambda([this, Key]()
													{
														return FText::FromString((keyBeingRenamed == Key) ? pendingRenameText : Key);
													})
														.SelectAllTextWhenFocused(true)
														.OnTextChanged_Lambda([this](const FText& NewText)
													{
														pendingRenameText = NewText.ToString();
													})
														.OnTextCommitted_Lambda([this, Key](const FText& NewText, ETextCommit::Type CommitType)
													{
														if (CommitType == ETextCommit::OnEnter || CommitType == ETextCommit::OnUserMovedFocus)
														{
															CommitRenameKey(Key, NewText.ToString());
														}
													}))
													: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::FromString(Key)))
												]
										]
								]
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
												const FString PrevType = keyInfos[Key].dataType;
												const FString NewType = *NewSelection;
												const TArray<TSharedPtr<FJsonValue>> PrevAcceptedValues = keyInfos[Key].acceptedValues;

												keyInfos[Key].dataType = NewType;
												keyInfos[Key].ResetDefaultValue();
												keyInfos[Key].defaultValueIsNull = false;

												// When converting String -> BoolArray, seed the array entries (and their labels)
												// from the accepted values that were on the String key. This is the convention
												// used by the model-configuration sync to pair each bool with a PR code.
												if (PrevType == "String" && NewType == "BoolArray" && PrevAcceptedValues.Num() > 0)
												{
													TArray<TSharedPtr<FJsonValue>> SeededArray;
													SeededArray.Reserve(PrevAcceptedValues.Num());
													for (int32 SeedIdx = 0; SeedIdx < PrevAcceptedValues.Num(); ++SeedIdx)
													{
														SeededArray.Add(MakeShared<FJsonValueBoolean>(false));
													}

													keyInfos[Key].defaultValueArray = SeededArray;
													keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(SeededArray);

													keyInfos[Key].acceptedValues = PrevAcceptedValues;
													// BoolArray labels are optional metadata, not "limited values".
													keyInfos[Key].limitValues = false;
												}

												JsonObject->SetField(Pair.Key, keyInfos[Key].defaultValue);
												UpdateJsonStr();
												this->UpdateJsonData(s_currentJsonStr);

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
															keyInfos[Key].defaultValueIsNull = false;
															ZLStateEditorV2Internal::SyncDefaultValueForKey(Key, keyInfos[Key], this->ActiveJsonObject);
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
																double RoundedValue = ZLStateEditorV2Internal::RoundEditorNumber(NewValue);
																keyInfos[Key].defaultValue = MakeShared<FJsonValueNumber>(RoundedValue);
															keyInfos[Key].defaultValueIsNull = false;
															ZLStateEditorV2Internal::SyncDefaultValueForKey(Key, keyInfos[Key], this->ActiveJsonObject);
																UpdateJsonStr();
															})
																.Value_Lambda([this, Key] {
																if (keyInfos[Key].dataType != "Number")
																	return double(0.0f);
																if (!keyInfos[Key].defaultValue.IsValid() || keyInfos[Key].defaultValue->Type != EJson::Number)
																{
																	return double(0.0f);
																}
																return keyInfos[Key].defaultValue->AsNumber();
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
															keyInfos[Key].defaultValueIsNull = false;
															ZLStateEditorV2Internal::SyncDefaultValueForKey(Key, keyInfos[Key], this->ActiveJsonObject);
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
												.Padding(0, 2, 0, 0)
												[
													SNew(SCheckBox)
														.Visibility_Lambda([this, Key] {
														return ZLStateEditorV2Internal::SupportsNullDefaults(keyInfos[Key]) ? EVisibility::Visible : EVisibility::Collapsed;
													})
														.OnCheckStateChanged_Lambda([this, Key](ECheckBoxState NewState)
													{
														keyInfos[Key].allowNullValue = (NewState == ECheckBoxState::Checked);
														if (!keyInfos[Key].allowNullValue)
														{
															keyInfos[Key].defaultValueIsNull = false;
															if (!keyInfos[Key].defaultValue.IsValid() || keyInfos[Key].defaultValue->Type == EJson::Null)
															{
																keyInfos[Key].ResetDefaultValue();
															}
														}
														ZLStateEditorV2Internal::SyncDefaultValueForKey(Key, keyInfos[Key], this->ActiveJsonObject);
														UpdateJsonStr();
														this->UpdateJsonData(s_currentJsonStr);
													})
														.IsChecked_Lambda([this, Key] {
														return keyInfos[Key].allowNullValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
													})
														.Content()
														[
															SNew(STextBlock).Text(FText::FromString("Allow Null Value"))
														]
												]
												+ SVerticalBox::Slot().AutoHeight()
												.Padding(0, 2, 0, 0)
												[
													SNew(SCheckBox)
														.Visibility_Lambda([this, Key] {
														return (keyInfos[Key].allowNullValue && ZLStateEditorV2Internal::SupportsNullDefaults(keyInfos[Key]))
															? EVisibility::Visible
															: EVisibility::Collapsed;
													})
														.OnCheckStateChanged_Lambda([this, Key](ECheckBoxState NewState)
													{
														keyInfos[Key].defaultValueIsNull = (NewState == ECheckBoxState::Checked);
														if (!keyInfos[Key].defaultValueIsNull && (!keyInfos[Key].defaultValue.IsValid() || keyInfos[Key].defaultValue->Type == EJson::Null))
														{
															keyInfos[Key].ResetDefaultValue();
														}
														ZLStateEditorV2Internal::SyncDefaultValueForKey(Key, keyInfos[Key], this->ActiveJsonObject);
														UpdateJsonStr();
														this->UpdateJsonData(s_currentJsonStr);
													})
														.IsChecked_Lambda([this, Key] {
														return keyInfos[Key].defaultValueIsNull ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
													})
														.Content()
														[
															SNew(STextBlock).Text(FText::FromString("Default Value Is Null"))
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
												.Padding(0, 2, 0, 0)
												[
													SNew(SCheckBox)
														.Visibility_Lambda([this, Key] {
														return this->ShouldShowDisplayDescriptionAsOptionsCheckbox(Key) ? EVisibility::Visible : EVisibility::Collapsed;
													})
														.OnCheckStateChanged_Lambda([this, Key](ECheckBoxState NewState)
													{
														keyInfos[Key].displayDescriptionAsOptions = (NewState == ECheckBoxState::Checked);
														this->UpdateJsonData(s_currentJsonStr);
													})
														.ToolTipText(FText::FromString("Display the description for each accepted value (instead of the raw value) in the Debug UI dropdowns. The configuration still stores the true value."))
														.IsChecked_Lambda([this, Key] {
														return keyInfos[Key].displayDescriptionAsOptions ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
													})
														.Content()
														[
															SNew(STextBlock).Text(FText::FromString("Display Description as options"))
														]
												]
												+ SVerticalBox::Slot().AutoHeight()
												.Padding(0, 2, 0, 0)
												[
													SNew(SCheckBox)
														.Visibility_Lambda([this, Key] {
														return (keyInfos[Key].dataType == "Number" || keyInfos[Key].dataType == "NumberArray")
															? EVisibility::Visible
															: EVisibility::Collapsed;
													})
														.OnCheckStateChanged_Lambda([this, Key](ECheckBoxState NewState)
													{
														keyInfos[Key].useMinMax = (NewState == ECheckBoxState::Checked);
														this->UpdateJsonData(s_currentJsonStr);
													})
														.IsChecked_Lambda([this, Key] {
														return keyInfos[Key].useMinMax ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
													})
														.Content()
														[
															SNew(STextBlock).Text(FText::FromString("Use Min/Max"))
														]
												]
												+ SVerticalBox::Slot().AutoHeight()
												.Padding(0, 2, 0, 0)
												[
													SNew(SHorizontalBox)
														.Visibility_Lambda([this, Key] {
														return ((keyInfos[Key].dataType == "Number" || keyInfos[Key].dataType == "NumberArray") && keyInfos[Key].useMinMax)
															? EVisibility::Visible
															: EVisibility::Collapsed;
													})
														+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
														[
															SNew(STextBlock).Text(FText::FromString("Min"))
														]
														+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
														[
															SNew(SNumericEntryBox<double>)
																.Value_Lambda([this, Key] { return keyInfos[Key].minValue; })
																.OnValueCommitted_Lambda([this, Key](double NewValue, ETextCommit::Type)
															{
																keyInfos[Key].minValue = NewValue;
																this->UpdateJsonData(s_currentJsonStr);
															})
														]
														+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
														[
															SNew(STextBlock).Text(FText::FromString("Max"))
														]
														+ SHorizontalBox::Slot().FillWidth(1.0f)
														[
															SNew(SNumericEntryBox<double>)
																.Value_Lambda([this, Key] { return keyInfos[Key].maxValue; })
																.OnValueCommitted_Lambda([this, Key](double NewValue, ETextCommit::Type)
															{
																keyInfos[Key].maxValue = NewValue;
																this->UpdateJsonData(s_currentJsonStr);
															})
														]
												]
												+ SVerticalBox::Slot().AutoHeight()
												.Padding(0, 2, 0, 0)
												[
													SNew(SCheckBox)
														.Visibility_Lambda([this, Key] {
														return keyInfos[Key].dataType == "StringArray" ? EVisibility::Visible : EVisibility::Collapsed;
													})
														.OnCheckStateChanged_Lambda([this, Key](ECheckBoxState NewState)
													{
														keyInfos[Key].allowDynamicArraySize = (NewState == ECheckBoxState::Checked);
														this->UpdateJsonData(s_currentJsonStr);
													})
														.IsChecked_Lambda([this, Key] {
														return keyInfos[Key].allowDynamicArraySize ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
													})
														.Content()
														[
															SNew(STextBlock).Text(FText::FromString("Allow Dynamic Array Size"))
														]
												]
												+ SVerticalBox::Slot().AutoHeight()
												[
													SAssignNew(AcceptedValuesArrayBox, SVerticalBox)
														.Visibility_Lambda([this, Key] {
														const bool bShowAcceptedValues =
															(keyInfos[Key].dataType == "BoolArray")
															|| keyInfos[Key].limitValues;
														return bShowAcceptedValues ? EVisibility::Visible : EVisibility::Hidden;
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
						TSharedPtr<FJsonValue> FieldValuePtr = (keyInfos[Key].defaultValueArray)[i];

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
												.Text(FText::FromString(FieldValuePtr->AsString()))
												.OnTextCommitted_Lambda([this, JsonObject, Pair, i, Key](const FText& NewText, ETextCommit::Type)
											{
												keyInfos[Key].defaultValueArray[i] = MakeShared<FJsonValueString>(NewText.ToString());
												keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
												keyInfos[Key].defaultValueIsNull = false;
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
												.Visibility_Lambda([this, Key]()
											{
												return keyInfos[Key].allowDynamicArraySize ? EVisibility::Visible : EVisibility::Collapsed;
											})
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
												.Value(FieldValuePtr->AsNumber())
												.OnValueCommitted_Lambda([this, JsonObject, Pair, i, Key](float NewValue, ETextCommit::Type)
											{
												double RoundedValue = ZLStateEditorV2Internal::RoundEditorNumber(NewValue);
												keyInfos[Key].defaultValueArray[i] = MakeShared<FJsonValueNumber>(RoundedValue);
												keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
												keyInfos[Key].defaultValueIsNull = false;
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
							const FString PairedLabel = (keyInfos[Key].acceptedValues.IsValidIndex(i) && keyInfos[Key].acceptedValues[i].IsValid())
								? keyInfos[Key].acceptedValues[i]->AsString()
								: FString();

							ArrayBox->AddSlot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.AutoWidth()
										[
											SNew(SCheckBox)
												.IsChecked(FieldValuePtr->AsBool() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
												.OnCheckStateChanged_Lambda([this, JsonObject, Pair, i, Key](ECheckBoxState NewState)
											{
												keyInfos[Key].defaultValueArray[i] = MakeShared<FJsonValueBoolean>(NewState == ECheckBoxState::Checked);
												keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
												keyInfos[Key].defaultValueIsNull = false;
												JsonObject->SetArrayField(Pair.Key, keyInfos[Key].defaultValueArray);
												UpdateJsonStr();
											})
										]
										+ SHorizontalBox::Slot()
										.FillWidth(1.0f)
										.VAlign(VAlign_Center)
										.Padding(6, 0, 0, 0)
										[
											SNew(STextBlock)
												.Text(FText::FromString(PairedLabel))
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

													if (keyInfos[Key].acceptedValues.IsValidIndex(i))
													{
														keyInfos[Key].acceptedValues.RemoveAt(i);
													}

													UpdateJsonStr();
													this->UpdateJsonData(s_currentJsonStr);
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
							.Visibility_Lambda([this, Key]()
						{
							if (keyInfos[Key].dataType != "StringArray")
							{
								return EVisibility::Visible;
							}
							return keyInfos[Key].allowDynamicArraySize ? EVisibility::Visible : EVisibility::Collapsed;
						})
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

								// Keep optional label metadata aligned when labels are already present.
								if (keyInfos[Key].acceptedValues.Num() > 0)
								{
									keyInfos[Key].acceptedValues.Add(MakeShared<FJsonValueString>(""));
								}
							}

							keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
							keyInfos[Key].defaultValueIsNull = false;
							JsonObject->SetArrayField(Pair.Key, keyInfos[Key].defaultValueArray);

							UpdateJsonStr();
							this->UpdateJsonData(s_currentJsonStr);

							return FReply::Handled();
						})
					];
			}

			const bool bShowAcceptedValuesSection =
				(keyInfos[Key].dataType == "BoolArray")
				|| (keyInfos[Key].AllowsLimitValues() && keyInfos[Key].limitValues);

			if (bShowAcceptedValuesSection)
			{
				if (keyInfos[Key].acceptedValues.Num() > 0)
				{
					for (int32 i = 0; i < keyInfos[Key].acceptedValues.Num(); ++i)
					{
						TSharedPtr<FJsonValue> FieldValuePtr = (keyInfos[Key].acceptedValues)[i];

						if (keyInfos[Key].dataType == "StringArray" || keyInfos[Key].dataType == "String" || keyInfos[Key].dataType == "BoolArray")
						{
							AcceptedValuesArrayBox->AddSlot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.FillWidth(1.0f)
										[
											SNew(SEditableTextBox)
												.Text(FText::FromString(FieldValuePtr->AsString()))
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

													// Keep the BoolArray default values aligned with the labels.
													if (keyInfos[Key].dataType == "BoolArray" && keyInfos[Key].defaultValueArray.IsValidIndex(i))
													{
														keyInfos[Key].defaultValueArray.RemoveAt(i);
														keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
														JsonObject->SetArrayField(Pair.Key, keyInfos[Key].defaultValueArray);
													}

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
												.Value(FieldValuePtr->AsNumber())
												.OnValueCommitted_Lambda([this, JsonObject, Pair, i, Key](float NewValue, ETextCommit::Type)
											{
												double RoundedValue = ZLStateEditorV2Internal::RoundEditorNumber(NewValue);
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
							else if (keyInfos[Key].dataType == "BoolArray")
							{
								keyInfos[Key].acceptedValues.Add(MakeShared<FJsonValueString>(""));
								keyInfos[Key].defaultValueArray.Add(MakeShared<FJsonValueBoolean>(false));
								keyInfos[Key].defaultValue = MakeShared<FJsonValueArray>(keyInfos[Key].defaultValueArray);
								JsonObject->SetArrayField(Pair.Key, keyInfos[Key].defaultValueArray);
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
