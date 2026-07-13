// Copyright ZeroLight ltd. All Rights Reserved.

#include "EditorZLCloudPluginSettings.h"
#include "JavaScriptKeyCodes.inl"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "ZLCloudPluginVersion.h"

#if WITH_EDITOR
#include <Developer/SettingsEditor/Public/ISettingsEditorModule.h>
#endif

DECLARE_LOG_CATEGORY_EXTERN(LogZLCloudPluginSettings, Log, All);
DEFINE_LOG_CATEGORY(LogZLCloudPluginSettings);


UZLCloudPluginSettings::UZLCloudPluginSettings(const FObjectInitializer& ObjectInitlaizer) : Super(ObjectInitlaizer)
{
	FString savedIniPath = FPaths::ProjectConfigDir() + TEXT("DefaultZLCloudPluginSettings.ini");
	const FString NormalizedIniPath = FConfigCacheIni::NormalizeConfigIniPath(savedIniPath);
	LoadConfig(NULL, *NormalizedIniPath);

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

	FString savedIniPath = FConfigCacheIni::NormalizeConfigIniPath(FPaths::ProjectConfigDir() + TEXT("DefaultZLCloudPluginSettings.ini"));

	SaveConfig(NULL, *savedIniPath);
#if WITH_EDITOR
	SaveToCustomIni();
#endif
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

	//Super::PostEditChangeProperty(PropertyChangedEvent);
	FString savedIniPath = FConfigCacheIni::NormalizeConfigIniPath(FPaths::ProjectConfigDir() + TEXT("DefaultZLCloudPluginSettings.ini"));

	SaveConfig(NULL, *savedIniPath);
	SaveToCustomIni();
}

void UZLCloudPluginSettings::SaveToCustomIni()
{
	const FString IniPath = FPaths::ProjectConfigDir() + TEXT("DefaultZLCloudPluginSettings.ini");

	const FString NormalizedIniPath = FConfigCacheIni::NormalizeConfigIniPath(IniPath);

	FConfigFile ConfigFile;
	ConfigFile.Read(NormalizedIniPath); // Load existing content

	const FString Section = TEXT("/Script/ZLCloudPlugin.ZLCloudPluginSettings");

	// Resolve symlinks/junctions so that e.g. P: (symlink to project location) becomes a path we can store relative to project
	FString resolvedBuildFolder = buildFolder.TrimStartAndEnd();
	if (!resolvedBuildFolder.IsEmpty() && IFileManager::Get().DirectoryExists(*resolvedBuildFolder))
	{
		FString onDisk = IFileManager::Get().GetFilenameOnDisk(*resolvedBuildFolder);
		if (!onDisk.IsEmpty())
			resolvedBuildFolder = onDisk;
	}
	FString projectDirBase = FPaths::ProjectDir();
	if (!projectDirBase.EndsWith(TEXT("/")) && !projectDirBase.EndsWith(TEXT("\\")))
		projectDirBase += TEXT("/");
	FString resolvedProjectDir = IFileManager::Get().GetFilenameOnDisk(*projectDirBase);
	if (resolvedProjectDir.IsEmpty())
		resolvedProjectDir = projectDirBase;
	if (!resolvedProjectDir.EndsWith(TEXT("/")) && !resolvedProjectDir.EndsWith(TEXT("\\")))
		resolvedProjectDir += TEXT("/");
	// Always store relative path (resolved paths may be on same volume after following symlinks)
	FString buildFolderToStore = resolvedBuildFolder;
	FPaths::MakePathRelativeTo(buildFolderToStore, *(resolvedProjectDir + TEXT(".")));
	if (!FPaths::IsRelative(buildFolderToStore))
	{
		// Fallback: try relative to unresolved project dir
		buildFolderToStore = buildFolder.TrimStartAndEnd();
		FPaths::MakePathRelativeTo(buildFolderToStore, *(projectDirBase + TEXT(".")));
	}
	if (!FPaths::IsRelative(buildFolderToStore))
		buildFolderToStore = buildFolder.TrimStartAndEnd(); // still absolute (e.g. different physical drive): store as-is

	FString RelativeThumbnailPath = thumbnailImagePath.FilePath;
	FPaths::MakePathRelativeTo(RelativeThumbnailPath, *FPaths::ProjectDir());


	// Helper macro to write key-value pairs
#define WRITE_CONFIG(Key, Value) \
	ConfigFile.SetString(*Section, TEXT(Key), Value)

	WRITE_CONFIG("bMouseAlwaysAttached", bMouseAlwaysAttached ? TEXT("True") : TEXT("False"));
	WRITE_CONFIG("filteredKeyList", *filteredKeyList);
	WRITE_CONFIG("FramesPerSecond", *FString::FromInt(FramesPerSecond));
	WRITE_CONFIG("DelayAppReadyToStream", DelayAppReadyToStream ? TEXT("True") : TEXT("False"));
	WRITE_CONFIG("bRebootAppOnDisconnect", bRebootAppOnDisconnect ? TEXT("True") : TEXT("False"));
	WRITE_CONFIG("bDisableTextureStreamingOnLaunch", bDisableTextureStreamingOnLaunch ? TEXT("True") : TEXT("False"));
	WRITE_CONFIG("stateRequestWarningTime", *FString::SanitizeFloat(stateRequestWarningTime));
	WRITE_CONFIG("stateRequestTimeout", *FString::SanitizeFloat(stateRequestTimeout));
	WRITE_CONFIG("initialHeartBeatTimeout", *FString::SanitizeFloat(initialHeartBeatTimeout));
	WRITE_CONFIG("heartBeatTimeout", *FString::SanitizeFloat(heartBeatTimeout));
	WRITE_CONFIG("screenshotFrameWaitCountOverride", *FString::FromInt(screenshotFrameWaitCountOverride));
	WRITE_CONFIG("supportsVR", supportsVR ? TEXT("True") : TEXT("False"));
	WRITE_CONFIG("deployName", *deployName);
	WRITE_CONFIG("displayName", *displayName);
	WRITE_CONFIG("buildId", *buildId);
	WRITE_CONFIG("buildFolder", *buildFolderToStore);
	WRITE_CONFIG("portalAssetLineId", *portalAssetLineId);
	WRITE_CONFIG("portalServerUrl", *portalServerUrl);
	WRITE_CONFIG("httpProxyOverride", *httpProxyOverride);
	WRITE_CONFIG("runInfoCommandLineParams", *runInfoCommandLineParams);
	WRITE_CONFIG("buildConfiguration", *buildConfiguration);
	WRITE_CONFIG("bDownloadFFmpegForMediaOnDemandVideos", bDownloadFFmpegForMediaOnDemandVideos ? TEXT("True") : TEXT("False"));
	WRITE_CONFIG("showExperimentalFeatures", showExperimentalFeatures ? TEXT("True") : TEXT("False"));

	FString NormalizedPath = RelativeThumbnailPath;
	NormalizedPath.ReplaceInline(TEXT("/"), TEXT("\\"));

	const FString FinalFormatted = FString::Printf(TEXT("(FilePath=\"%s\")"), *NormalizedPath);

#if UNREAL_5_4_OR_NEWER
	ConfigFile.RemoveKeyFromSection(*Section, TEXT("thumbnailImagePath"));
	ConfigFile.AddToSection(*Section, TEXT("thumbnailImagePath"), FinalFormatted);
#else
	FConfigSection* ConfigSection = ConfigFile.Find(Section);
	ConfigSection->Remove(TEXT("thumbnailImagePath"));
	ConfigSection->Add(TEXT("thumbnailImagePath"), FConfigValue(FinalFormatted));
#endif

#undef WRITE_CONFIG

	ConfigFile.Dirty = true;
	ConfigFile.Write(NormalizedIniPath);
}
#endif