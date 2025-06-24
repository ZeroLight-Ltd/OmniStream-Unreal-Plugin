// Copyright ZeroLight ltd. All Rights Reserved.

#include "EditorZLCloudPluginSettings.h"
#include "JavaScriptKeyCodes.inl"

#if WITH_EDITOR
#include <Developer/SettingsEditor/Public/ISettingsEditorModule.h>
#endif

DECLARE_LOG_CATEGORY_EXTERN(LogZLCloudPluginSettings, Log, All);
DEFINE_LOG_CATEGORY(LogZLCloudPluginSettings);


UZLCloudPluginSettings::UZLCloudPluginSettings(const FObjectInitializer& ObjectInitlaizer) : Super(ObjectInitlaizer)
{
	FString savedIniPath = FPaths::GeneratedConfigDir() + TEXT("WindowsEditor/ZLCloudPluginSettings.ini");
	LoadConfig(NULL, *savedIniPath);

	UE_LOG(LogZLCloudPluginSettings, Display, TEXT("Filtered keys list %s"), *filteredKeyList);

	if (deployName.Equals(""))
		deployName = FApp::GetProjectName();

	if (buildFolder.Equals(""))
		buildFolder = FPaths::ProjectDir() + "Build";

	if (thumbnailImagePath.FilePath.Equals(""))
	{
		FString defaultThumbnailFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AutoScreenshot.png"));
		if (FPaths::FileExists(defaultThumbnailFilePath))
			thumbnailImagePath.FilePath = defaultThumbnailFilePath;
	}

	UpdateFilteredKeys();

}

void UZLCloudPluginSettings::UpdateFilteredKeys()
{
	FString CommaList = filteredKeyList;

	TArray<FString> KeyStringArray;
	CommaList.ParseIntoArray(KeyStringArray, TEXT(","), true);
	FilteredKeys.Empty();
	for (auto&& KeyString : KeyStringArray)
	{
		int KeyCode = FCString::Atoi(*KeyString);
		const FKey* AgnosticKey = ZLCloudPlugin::JavaScriptKeyCodeToFKey[KeyCode];
		
		FilteredKeys.Add(*AgnosticKey);
	}

	FString savedIniPath = FPaths::GeneratedConfigDir() + TEXT("WindowsEditor/ZLCloudPluginSettings.ini");

	SaveConfig(NULL, *savedIniPath);
}

FName UZLCloudPluginSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UZLCloudPluginSettings::GetSectionText() const
{
	return NSLOCTEXT("ZLCloudPluginSettings", "ZLCloudPluginSettingsSection", "ZeroLight OmniStream");
}
#endif

#if WITH_EDITOR
void UZLCloudPluginSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if ((PropertyName == GET_MEMBER_NAME_CHECKED(UZLCloudPluginSettings, filteredKeyList)))
	{
		UpdateFilteredKeys();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
	FString savedIniPath = FPaths::GeneratedConfigDir() + TEXT("WindowsEditor/ZLCloudPluginSettings.ini");

	SaveConfig(NULL, *savedIniPath);
}
#endif