// Copyright ZeroLight ltd. All Rights Reserved.
#include "ZLCloudPluginEditorModule.h"
#include "EdGraphUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogZLCloudPluginEditor, Log, All);

static void AppendZLSchemaPresetsNonUFSLineToDefaultGameIni()
{
	static const TCHAR* RelFolder = TEXT("ZLSchemaPresets");
	const FString ContentDir = FPaths::Combine(FPaths::ProjectContentDir(), RelFolder);
	if (!FPaths::DirectoryExists(ContentDir))
	{
		return;
	}

	const FString IniPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultGame.ini"));
	FString IniText;
	if (!FFileHelper::LoadFileToString(IniText, *IniPath))
	{
		UE_LOG(LogZLCloudPluginEditor, Warning, TEXT("Could not load '%s' for ZLSchemaPresets packaging line."), *IniPath);
		return;
	}

	static const TCHAR* Section = TEXT("[/Script/UnrealEd.ProjectPackagingSettings]");
	const FString EntryValue = FString::Printf(TEXT("DirectoriesToAlwaysStageAsNonUFS=(Path=\"%s\")"), RelFolder);
	if (IniText.Contains(EntryValue))
	{
		return;
	}

	const FString LE = IniText.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
	const FString Line = FString::Printf(TEXT("+%s%s"), *EntryValue, *LE);
	static const FString NonUFSKey(TEXT("+DirectoriesToAlwaysStageAsNonUFS="));

	auto FindIndexAfterLineEnding = [&IniText](int32 FromIndex) -> int32
	{
		const int32 N = IniText.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FromIndex);
		if (N == INDEX_NONE)
		{
			return IniText.Len();
		}
		return N + 1;
	};

	const int32 SectionStart = IniText.Find(Section, ESearchCase::CaseSensitive);
	if (SectionStart == INDEX_NONE)
	{
		if (!IniText.IsEmpty() && !IniText.EndsWith(TEXT("\n")) && !IniText.EndsWith(TEXT("\r\n")))
		{
			IniText += LE;
		}
		IniText += FString::Printf(TEXT("%s%s%s"), Section, *LE, *Line);
	}
	else
	{
		const int32 AfterHeader = SectionStart + FCString::Strlen(Section);
		const int32 NextSection = IniText.Find(TEXT("\n["), ESearchCase::CaseSensitive, ESearchDir::FromStart, AfterHeader);
		const int32 BodyEndExclusive = (NextSection == INDEX_NONE) ? IniText.Len() : (NextSection + 1);

		int32 InsertAfter = INDEX_NONE;
		for (int32 Search = AfterHeader; Search < BodyEndExclusive;)
		{
			const int32 Hit = IniText.Find(NonUFSKey, ESearchCase::CaseSensitive, ESearchDir::FromStart, Search);
			if (Hit == INDEX_NONE || Hit >= BodyEndExclusive)
			{
				break;
			}
			InsertAfter = FindIndexAfterLineEnding(Hit);
			Search = InsertAfter;
		}

		if (InsertAfter != INDEX_NONE)
		{
			IniText.InsertAt(InsertAfter, Line);
		}
		else if (NextSection == INDEX_NONE)
		{
			if (!IniText.EndsWith(TEXT("\n")) && !IniText.EndsWith(TEXT("\r\n")))
			{
				IniText += LE;
			}
			IniText += Line;
		}
		else
		{
			IniText.InsertAt(NextSection + 1, Line);
		}
	}

	if (FFileHelper::SaveStringToFile(IniText, *IniPath))
	{
		UE_LOG(LogZLCloudPluginEditor, Log, TEXT("Wrote ZLSchemaPresets NonUFS staging line to '%s'."), *IniPath);
	}
	else
	{
		UE_LOG(LogZLCloudPluginEditor, Warning, TEXT("Could not save '%s' after adding ZLSchemaPresets line."), *IniPath);
	}
}

void FZLCloudPluginEditorModule::StartupModule()
{
	AppendZLSchemaPresetsNonUFSLineToDefaultGameIni();
	FEdGraphUtilities::RegisterVisualPinFactory(MakeShareable(new FSchemaKeyPinFactory()));
}

void FZLCloudPluginEditorModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FZLCloudPluginEditorModule, ZLCloudPluginEditor)
