// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLOmniStreamAutoPopulateLibrary.h"

#if WITH_EDITOR
#include "ZLStateEditorV2.h"
#include "IZLOmniStream_SchemaAutoPopulate.h"
#include "Features/IModularFeatures.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#endif

#if WITH_EDITOR
namespace ZLOmniStreamAutoPopulateLibraryInternal
{
	FZLAutoPopulateAdvancedOptionDesc ToBlueprintDesc(const FZLAutoPopulateAdvancedOptionDef& Source)
	{
		FZLAutoPopulateAdvancedOptionDesc Out;
		Out.OptionName = Source.OptionName;
		Out.FoldoutTitle = Source.FoldoutTitle;

		for (const FZLAutoPopulateAdvancedCheckboxDef& CheckboxDef : Source.Checkboxes)
		{
			FZLAutoPopulateAdvancedCheckboxDesc CheckboxOut;
			CheckboxOut.Id = CheckboxDef.Id;
			CheckboxOut.Label = CheckboxDef.Label;
			CheckboxOut.bDefaultChecked = CheckboxDef.bDefaultChecked;
			Out.Checkboxes.Add(MoveTemp(CheckboxOut));
		}

		for (const FZLAutoPopulateAdvancedLevelSelectorDef& SelectorDef : Source.LevelSelectors)
		{
			FZLAutoPopulateAdvancedLevelSelectorDesc SelectorOut;
			SelectorOut.Id = SelectorDef.Id;
			SelectorOut.Label = SelectorDef.Label;
			SelectorOut.RequiredActorClassPath = SelectorDef.RequiredActorClassPath;
			SelectorOut.RequiredActorComponentClassPath = SelectorDef.RequiredActorComponentClassPath;
			Out.LevelSelectors.Add(MoveTemp(SelectorOut));
		}

		return Out;
	}

	TArray<UWorld*> ResolveLevels(const TArray<FString>& LevelPackageNames)
	{
		TArray<UWorld*> Levels;
		if (LevelPackageNames.Num() == 0)
		{
			if (GEditor)
			{
				if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
				{
					Levels.Add(EditorWorld);
				}
			}
			return Levels;
		}

		for (const FString& PackageName : LevelPackageNames)
		{
			if (PackageName.IsEmpty())
			{
				continue;
			}

			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package)
			{
				Package = LoadPackage(nullptr, *PackageName, LOAD_None);
			}

			if (Package)
			{
				if (UWorld* World = UWorld::FindWorldInPackage(Package))
				{
					Levels.Add(World);
				}
			}
		}

		return Levels;
	}

	FZLAutoPopulateAdvancedSettingsMap ParseAdvancedSettingsJson(const FString& AdvancedSettingsJson)
	{
		FZLAutoPopulateAdvancedSettingsMap Parsed;
		if (AdvancedSettingsJson.IsEmpty())
		{
			return Parsed;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(AdvancedSettingsJson);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return Parsed;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& OptionPair : RootObject->Values)
		{
			if (!OptionPair.Value.IsValid() || OptionPair.Value->Type != EJson::Object)
			{
				continue;
			}

			FZLAutoPopulateAdvancedOptionSettings OptionSettings;
			const TSharedPtr<FJsonObject> OptionObject = OptionPair.Value->AsObject();
			if (!OptionObject.IsValid())
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* CheckboxesObject = nullptr;
			if (OptionObject->TryGetObjectField(TEXT("checkboxes"), CheckboxesObject) && CheckboxesObject && CheckboxesObject->IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& CheckboxPair : (*CheckboxesObject)->Values)
				{
					if (CheckboxPair.Value.IsValid() && CheckboxPair.Value->Type == EJson::Boolean)
					{
						OptionSettings.CheckboxValues.Add(CheckboxPair.Key, CheckboxPair.Value->AsBool());
					}
				}
			}

			const TSharedPtr<FJsonObject>* LevelSelectionsObject = nullptr;
			if (OptionObject->TryGetObjectField(TEXT("levelSelections"), LevelSelectionsObject) && LevelSelectionsObject && LevelSelectionsObject->IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& SelectorPair : (*LevelSelectionsObject)->Values)
				{
					if (!SelectorPair.Value.IsValid() || SelectorPair.Value->Type != EJson::Array)
					{
						continue;
					}

					TArray<FString> SelectedLevels;
					for (const TSharedPtr<FJsonValue>& LevelValue : SelectorPair.Value->AsArray())
					{
						if (LevelValue.IsValid() && LevelValue->Type == EJson::String)
						{
							SelectedLevels.AddUnique(LevelValue->AsString());
						}
					}
					OptionSettings.LevelSelectionsBySelectorId.Add(SelectorPair.Key, MoveTemp(SelectedLevels));
				}
			}

			const TSharedPtr<FJsonObject>* ActorSelectionsObject = nullptr;
			if (OptionObject->TryGetObjectField(TEXT("actorSelections"), ActorSelectionsObject) && ActorSelectionsObject && ActorSelectionsObject->IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& SelectorPair : (*ActorSelectionsObject)->Values)
				{
					if (!SelectorPair.Value.IsValid() || SelectorPair.Value->Type != EJson::Array)
					{
						continue;
					}

					TArray<FString> SelectedActors;
					for (const TSharedPtr<FJsonValue>& ActorValue : SelectorPair.Value->AsArray())
					{
						if (ActorValue.IsValid() && ActorValue->Type == EJson::String)
						{
							SelectedActors.AddUnique(ActorValue->AsString());
						}
					}
					OptionSettings.ActorSelectionsBySelectorId.Add(SelectorPair.Key, MoveTemp(SelectedActors));
				}
			}

			Parsed.Add(OptionPair.Key, MoveTemp(OptionSettings));
		}

		return Parsed;
	}

	bool DoesWorldMatchClassRequirements(UWorld* World, UClass* RequiredActorClass, UClass* RequiredComponentClass)
	{
		if (!IsValid(World))
		{
			return false;
		}

		for (ULevel* Level : World->GetLevels())
		{
			if (!IsValid(Level))
			{
				continue;
			}

			for (AActor* Actor : Level->Actors)
			{
				if (!IsValid(Actor))
				{
					continue;
				}

				if (RequiredActorClass && !Actor->IsA(RequiredActorClass))
				{
					continue;
				}

				if (RequiredComponentClass && !Actor->FindComponentByClass(RequiredComponentClass))
				{
					continue;
				}

				return true;
			}
		}

		return false;
	}
}
#endif

TArray<FZLAutoPopulateModuleDesc> UZLOmniStreamAutoPopulateLibrary::GetAutoPopulateModules()
{
	TArray<FZLAutoPopulateModuleDesc> Result;

#if WITH_EDITOR
	const FName FeatureName = IZLOmniStream_SchemaAutoPopulate::GetModularFeatureName();
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	const int32 NumImpls = ModularFeatures.GetModularFeatureImplementationCount(FeatureName);

	for (int32 i = 0; i < NumImpls; ++i)
	{
		IModularFeature* RawImpl = ModularFeatures.GetModularFeatureImplementation(FeatureName, i);
		IZLOmniStream_SchemaAutoPopulate* Impl = static_cast<IZLOmniStream_SchemaAutoPopulate*>(RawImpl);
		if (!Impl)
		{
			continue;
		}

		FZLAutoPopulateModuleDesc Desc;
		Desc.PluginName = Impl->GetPluginName();
		Desc.OptionNames = Impl->GetAutoPopulateOptionNames();
		for (const FZLAutoPopulateAdvancedOptionDef& AdvancedDef : Impl->GetAdvancedOptionDefs())
		{
			Desc.AdvancedOptionDescs.Add(ZLOmniStreamAutoPopulateLibraryInternal::ToBlueprintDesc(AdvancedDef));
		}
		Result.Add(MoveTemp(Desc));
	}

	Result.Sort([](const FZLAutoPopulateModuleDesc& A, const FZLAutoPopulateModuleDesc& B)
	{
		return A.PluginName.Compare(B.PluginName, ESearchCase::IgnoreCase) < 0;
	});
#endif

	return Result;
}

TArray<FString> UZLOmniStreamAutoPopulateLibrary::GetCurrentlySelectedOptions()
{
	TArray<FString> Result;

#if WITH_EDITOR
	if (TSharedPtr<FZLStateEditorV2> Live = FZLStateEditorV2::GetLiveInstance().Pin())
	{
		Result = Live->GetSelectedAutoPopulateOptions().Array();
	}
#endif

	return Result;
}

bool UZLOmniStreamAutoPopulateLibrary::SetSelectedAutoPopulateOptions(const TArray<FString>& SelectedOptions)
{
#if WITH_EDITOR
	if (TSharedPtr<FZLStateEditorV2> Live = FZLStateEditorV2::GetLiveInstance().Pin())
	{
		Live->SetSelectedAutoPopulateOptions(SelectedOptions);
		return true;
	}
#endif
	return false;
}

bool UZLOmniStreamAutoPopulateLibrary::IsSchemaEditorOpen()
{
#if WITH_EDITOR
	return FZLStateEditorV2::GetLiveInstance().IsValid();
#else
	return false;
#endif
}

bool UZLOmniStreamAutoPopulateLibrary::RunAutoPopulate(const TArray<FString>& SelectedOptions, const TArray<FString>& LevelPackageNames, bool bAdditive)
{
	return RunAutoPopulateWithSettings(SelectedOptions, LevelPackageNames, bAdditive, TEXT(""));
}

bool UZLOmniStreamAutoPopulateLibrary::RunAutoPopulateWithSettings(const TArray<FString>& SelectedOptions, const TArray<FString>& LevelPackageNames, bool bAdditive, const FString& AdvancedSettingsJson)
{
#if WITH_EDITOR
	TSharedPtr<FZLStateEditorV2> Live = FZLStateEditorV2::GetLiveInstance().Pin();
	if (!Live.IsValid())
	{
		return false;
	}

	const TArray<UWorld*> Levels = ZLOmniStreamAutoPopulateLibraryInternal::ResolveLevels(LevelPackageNames);
	const FZLAutoPopulateAdvancedSettingsMap AdvancedSettings =
		ZLOmniStreamAutoPopulateLibraryInternal::ParseAdvancedSettingsJson(AdvancedSettingsJson);

	Live->AutoPopulateSchema(SelectedOptions, Levels, bAdditive, true, AdvancedSettings);
	return true;
#else
	return false;
#endif
}

FString UZLOmniStreamAutoPopulateLibrary::GetLoadedSchemaPath()
{
#if WITH_EDITOR
	if (TSharedPtr<FZLStateEditorV2> Live = FZLStateEditorV2::GetLiveInstance().Pin())
	{
		return Live->GetLoadedSchemaPath();
	}
#endif
	return TEXT("");
}

bool UZLOmniStreamAutoPopulateLibrary::ClearAutoPopulatePreview()
{
#if WITH_EDITOR
	if (TSharedPtr<FZLStateEditorV2> Live = FZLStateEditorV2::GetLiveInstance().Pin())
	{
		Live->DiscardAutoPopulatePreview();
		return true;
	}
#endif
	return false;
}

bool UZLOmniStreamAutoPopulateLibrary::PreviewAutoPopulate(const TArray<FString>& SelectedOptions, const TArray<FString>& LevelPackageNames, bool bAdditive, FString& OutProposedSchemaText)
{
	return PreviewAutoPopulateWithSettings(SelectedOptions, LevelPackageNames, bAdditive, TEXT(""), OutProposedSchemaText);
}

bool UZLOmniStreamAutoPopulateLibrary::PreviewAutoPopulateWithSettings(const TArray<FString>& SelectedOptions, const TArray<FString>& LevelPackageNames, bool bAdditive, const FString& AdvancedSettingsJson, FString& OutProposedSchemaText)
{
	OutProposedSchemaText = TEXT("");
#if WITH_EDITOR
	TSharedPtr<FZLStateEditorV2> Live = FZLStateEditorV2::GetLiveInstance().Pin();
	if (!Live.IsValid())
	{
		return false;
	}

	const TArray<UWorld*> Levels = ZLOmniStreamAutoPopulateLibraryInternal::ResolveLevels(LevelPackageNames);
	const FZLAutoPopulateAdvancedSettingsMap AdvancedSettings =
		ZLOmniStreamAutoPopulateLibraryInternal::ParseAdvancedSettingsJson(AdvancedSettingsJson);
	return Live->BuildAutoPopulatePreview(SelectedOptions, Levels, bAdditive, OutProposedSchemaText, AdvancedSettings);
#else
	return false;
#endif
}

bool UZLOmniStreamAutoPopulateLibrary::ApplyAutoPopulatePreview()
{
#if WITH_EDITOR
	if (TSharedPtr<FZLStateEditorV2> Live = FZLStateEditorV2::GetLiveInstance().Pin())
	{
		return Live->ApplyAutoPopulatePreview();
	}
#endif
	return false;
}

FString UZLOmniStreamAutoPopulateLibrary::GetPendingPreviewSchemaText()
{
#if WITH_EDITOR
	if (TSharedPtr<FZLStateEditorV2> Live = FZLStateEditorV2::GetLiveInstance().Pin())
	{
		return Live->GetPendingAutoPopulatePreviewSchemaText();
	}
#endif
	return TEXT("");
}

bool UZLOmniStreamAutoPopulateLibrary::IsZLEditorToolsAvailable()
{
#if WITH_EDITOR && WITH_ZLEDITORTOOLS
	const TSharedPtr<IPlugin> EditorToolsPlugin = IPluginManager::Get().FindPlugin(TEXT("ZLEditorTools"));
	return EditorToolsPlugin.IsValid();
#else
	return false;
#endif
}

TArray<FString> UZLOmniStreamAutoPopulateLibrary::FilterLevelsByClassRequirements(
	const TArray<FString>& CandidateLevelPackageNames,
	const FString& RequiredActorClassPath,
	const FString& RequiredActorComponentClassPath)
{
	TArray<FString> MatchingLevels;
#if WITH_EDITOR
	UClass* RequiredActorClass = nullptr;
	if (!RequiredActorClassPath.IsEmpty())
	{
		RequiredActorClass = StaticLoadClass(AActor::StaticClass(), nullptr, *RequiredActorClassPath);
	}

	UClass* RequiredComponentClass = nullptr;
	if (!RequiredActorComponentClassPath.IsEmpty())
	{
		RequiredComponentClass = StaticLoadClass(UActorComponent::StaticClass(), nullptr, *RequiredActorComponentClassPath);
	}

	for (const FString& PackageName : CandidateLevelPackageNames)
	{
		if (PackageName.IsEmpty())
		{
			continue;
		}

		TArray<FString> SinglePackage;
		SinglePackage.Add(PackageName);
		const TArray<UWorld*> Worlds = ZLOmniStreamAutoPopulateLibraryInternal::ResolveLevels(SinglePackage);
		if (Worlds.Num() == 0)
		{
			continue;
		}

		if (ZLOmniStreamAutoPopulateLibraryInternal::DoesWorldMatchClassRequirements(
			Worlds[0],
			RequiredActorClass,
			RequiredComponentClass))
		{
			MatchingLevels.AddUnique(PackageName);
		}
	}
#endif
	return MatchingLevels;
}

TArray<FZLAutoPopulateLevelActorDesc> UZLOmniStreamAutoPopulateLibrary::GetLevelActorDisplayEntriesByClassRequirements(
	const TArray<FString>& CandidateLevelPackageNames,
	const FString& RequiredActorClassPath,
	const FString& RequiredActorComponentClassPath)
{
	TArray<FZLAutoPopulateLevelActorDesc> MatchingEntries;
#if WITH_EDITOR
	UClass* RequiredActorClass = nullptr;
	if (!RequiredActorClassPath.IsEmpty())
	{
		RequiredActorClass = StaticLoadClass(AActor::StaticClass(), nullptr, *RequiredActorClassPath);
	}

	UClass* RequiredComponentClass = nullptr;
	if (!RequiredActorComponentClassPath.IsEmpty())
	{
		RequiredComponentClass = StaticLoadClass(UActorComponent::StaticClass(), nullptr, *RequiredActorComponentClassPath);
	}

	for (const FString& PackageName : CandidateLevelPackageNames)
	{
		if (PackageName.IsEmpty())
		{
			continue;
		}

		TArray<FString> SinglePackage;
		SinglePackage.Add(PackageName);
		const TArray<UWorld*> Worlds = ZLOmniStreamAutoPopulateLibraryInternal::ResolveLevels(SinglePackage);
		if (Worlds.Num() == 0 || !IsValid(Worlds[0]))
		{
			continue;
		}

		for (ULevel* Level : Worlds[0]->GetLevels())
		{
			if (!IsValid(Level))
			{
				continue;
			}

			for (AActor* Actor : Level->Actors)
			{
				if (!IsValid(Actor))
				{
					continue;
				}

				if (RequiredActorClass && !Actor->IsA(RequiredActorClass))
				{
					continue;
				}

				if (RequiredComponentClass && !Actor->FindComponentByClass(RequiredComponentClass))
				{
					continue;
				}

#if WITH_EDITOR
				FString ActorDisplayName = Actor->GetActorLabel();
#else
				FString ActorDisplayName = Actor->GetName();
#endif
				if (ActorDisplayName.IsEmpty())
				{
					ActorDisplayName = Actor->GetName();
				}

				FZLAutoPopulateLevelActorDesc Entry;
				Entry.ActorDisplayName = ActorDisplayName;
				Entry.LevelPackageName = PackageName;
				MatchingEntries.Add(MoveTemp(Entry));
			}
		}
	}

	MatchingEntries.Sort([](const FZLAutoPopulateLevelActorDesc& A, const FZLAutoPopulateLevelActorDesc& B)
	{
		const int32 NameCompare = A.ActorDisplayName.Compare(B.ActorDisplayName, ESearchCase::IgnoreCase);
		if (NameCompare == 0)
		{
			return A.LevelPackageName.Compare(B.LevelPackageName, ESearchCase::IgnoreCase) < 0;
		}
		return NameCompare < 0;
	});
#endif
	return MatchingEntries;
}
