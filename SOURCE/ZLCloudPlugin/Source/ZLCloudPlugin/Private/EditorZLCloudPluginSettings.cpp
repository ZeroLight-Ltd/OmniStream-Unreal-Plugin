// Copyright ZeroLight ltd. All Rights Reserved.

#include "EditorZLCloudPluginSettings.h"
#include "JavaScriptKeyCodes.inl"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "ZLCloudPluginVersion.h"
#include "ZeroLightMainButton.h"

#if WITH_EDITOR
#include "SZLFilteredKeysEditor.h"
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

	SanitizePathSettings();

	UpdateFilteredKeys();

}

namespace
{
	// Well past any path a project can actually use, so treat anything longer as junk
	static const int32 MaxStoredPathLength = 1024;
	// A path never legitimately walks up this far out of the project
	static const int32 MaxParentDirSegments = 16;
	// "Content/Content" is plausible, a third repeat of the same directory name in a row is not
	static const int32 MaxRepeatedPathSegments = 2;

	FString CleanStoredPathString(const FString& path)
	{
		FString cleaned = path.TrimStartAndEnd().TrimQuotes().TrimStartAndEnd();
		cleaned.ReplaceInline(TEXT("\\"), TEXT("/"));
		FPaths::RemoveDuplicateSlashes(cleaned);
		return cleaned;
	}

	// Follows symlinks/junctions so that a project reached through a mapped drive compares equal to its real location
	FString ResolvePathOnDisk(const FString& absolutePath)
	{
		if (absolutePath.IsEmpty())
			return absolutePath;

		if (FPaths::FileExists(absolutePath) || FPaths::DirectoryExists(absolutePath))
		{
			FString onDisk = IFileManager::Get().GetFilenameOnDisk(*absolutePath);
			if (!onDisk.IsEmpty())
				return CleanStoredPathString(onDisk);
		}

		return CleanStoredPathString(absolutePath);
	}
}

FString UZLCloudPluginSettings::ResolveStoredPath(const FString& storedPath)
{
	FString path = CleanStoredPathString(storedPath);
	if (path.IsEmpty())
		return path;

	// FPaths resolves relative paths against FPlatformProcess::BaseDir() (the engine binaries folder), so a
	// project relative value read back from the ini must be rebased on the project before it is expanded.
	if (FPaths::IsRelative(path))
		path = FPaths::ProjectDir() / path;

	path = FPaths::ConvertRelativePathToFull(path);
	FPaths::CollapseRelativeDirectories(path);
	FPaths::RemoveDuplicateSlashes(path);

	return path;
}

bool UZLCloudPluginSettings::IsStoredPathCorrupt(const FString& storedPath)
{
	const FString path = CleanStoredPathString(storedPath);
	if (path.IsEmpty())
		return false;

	if (path.Len() > MaxStoredPathLength)
		return true;

	TArray<FString> segments;
	path.ParseIntoArray(segments, TEXT("/"), true);

	int32 parentDirSegments = 0;
	int32 repeatCount = 1;
	for (int32 i = 0; i < segments.Num(); ++i)
	{
		if (segments[i].Equals(TEXT("..")))
		{
			++parentDirSegments;
			repeatCount = 1;
			continue;
		}

		if (i > 0 && segments[i].Equals(segments[i - 1], ESearchCase::IgnoreCase))
		{
			if (++repeatCount > MaxRepeatedPathSegments)
				return true;
		}
		else
		{
			repeatCount = 1;
		}
	}

	return parentDirSegments > MaxParentDirSegments;
}

FString UZLCloudPluginSettings::MakeStoredPath(const FString& path)
{
	const FString absolutePath = ResolveStoredPath(path);
	if (absolutePath.IsEmpty())
		return absolutePath;

	const FString resolvedTarget = ResolvePathOnDisk(absolutePath);

	FString resolvedProjectDir = ResolvePathOnDisk(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	if (!resolvedProjectDir.EndsWith(TEXT("/")))
		resolvedProjectDir += TEXT("/");

	FString relativePath = resolvedTarget;
	if (FPaths::MakePathRelativeTo(relativePath, *(resolvedProjectDir + TEXT("."))) &&
		FPaths::IsRelative(relativePath) &&
		!IsStoredPathCorrupt(relativePath) &&
		FPaths::IsSamePath(ResolvePathOnDisk(ResolveStoredPath(relativePath)), resolvedTarget))
	{
		return relativePath;
	}

	// Different volume, or the relative form does not lead back to the same place: an absolute path is still valid
	return absolutePath;
}

void UZLCloudPluginSettings::SanitizePathSettings()
{
	const FString defaultBuildFolder = FPaths::ProjectDir() + TEXT("Build");

	FString sanitizedBuildFolder = CleanStoredPathString(buildFolder);
	if (IsStoredPathCorrupt(sanitizedBuildFolder))
	{
		UE_LOG(LogZLCloudPluginSettings, Warning, TEXT("buildFolder in DefaultZLCloudPluginSettings.ini is not a usable path, resetting to \"%s\". Discarded value: %s"),
			*defaultBuildFolder, *sanitizedBuildFolder);
		sanitizedBuildFolder.Empty();
	}
	buildFolder = sanitizedBuildFolder.IsEmpty() ? defaultBuildFolder : ResolveStoredPath(sanitizedBuildFolder);

	FString sanitizedThumbnailPath = CleanStoredPathString(thumbnailImagePath.FilePath);
	if (IsStoredPathCorrupt(sanitizedThumbnailPath))
	{
		UE_LOG(LogZLCloudPluginSettings, Warning, TEXT("thumbnailImagePath in DefaultZLCloudPluginSettings.ini is not a usable path, clearing it. Discarded value: %s"),
			*sanitizedThumbnailPath);
		sanitizedThumbnailPath.Empty();
	}
	thumbnailImagePath.FilePath = sanitizedThumbnailPath;

	if (thumbnailImagePath.FilePath.IsEmpty())
	{
		FString defaultThumbnailFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AutoScreenshot.png"));
		if (FPaths::FileExists(defaultThumbnailFilePath))
			thumbnailImagePath.FilePath = defaultThumbnailFilePath;
	}
}

void UZLCloudPluginSettings::UpdateFilteredKeys()
{
	FString CommaList = filteredKeyList;

	TArray<FString> KeyStringArray;
	CommaList.ParseIntoArray(KeyStringArray, TEXT(","), true);
	FilteredKeys.Empty();
	for (auto&& KeyString : KeyStringArray)
	{
		const int KeyCode = FCString::Atoi(*KeyString.TrimStartAndEnd());
		const FKey* AgnosticKey = ZLCloudPlugin::GetFKeyFromJavaScriptKeyCode(KeyCode);
		if (AgnosticKey && AgnosticKey->IsValid())
		{
			FilteredKeys.Add(*AgnosticKey);
		}
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

void FZLCloudPluginSettingsCustomization::OpenFilteredKeysEditor()
{
	SZLFilteredKeysEditor::OpenWindow();
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

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UZLCloudPluginSettings, bDownloadFFmpegForMediaOnDemandVideos)
		&& bDownloadFFmpegForMediaOnDemandVideos)
	{
		FZeroLightMainButton::EnsureFFmpegInstalledForMediaOnDemandVideosAsync([](bool bSuccess)
		{
			if (!bSuccess)
			{
				UE_LOG(LogZLCloudPluginSettings, Warning, TEXT("Failed to install FFmpeg after enabling media on demand video support"));
			}
		});
	}
}

void UZLCloudPluginSettings::SaveToCustomIni()
{
	const FString IniPath = FPaths::ProjectConfigDir() + TEXT("DefaultZLCloudPluginSettings.ini");

	const FString NormalizedIniPath = FConfigCacheIni::NormalizeConfigIniPath(IniPath);

	FConfigFile ConfigFile;
	ConfigFile.Read(NormalizedIniPath); // Load existing content

	const FString Section = TEXT("/Script/ZLCloudPlugin.ZLCloudPluginSettings");

	// Store paths relative to the project where possible, following symlinks/junctions so that e.g. a project
	// opened through a mapped drive still resolves onto the same volume. MakeStoredPath keeps the absolute path
	// when the relative form would not resolve back to the same location.
	FString buildFolderToStore = MakeStoredPath(buildFolder);
	if (buildFolderToStore.IsEmpty() || IsStoredPathCorrupt(buildFolderToStore))
	{
		const FString defaultBuildFolder = MakeStoredPath(FPaths::ProjectDir() + TEXT("Build"));
		UE_LOG(LogZLCloudPluginSettings, Warning, TEXT("Not writing invalid buildFolder \"%s\" to config, using \"%s\" instead"),
			*buildFolder, *defaultBuildFolder);
		buildFolderToStore = defaultBuildFolder;
	}

	FString RelativeThumbnailPath = MakeStoredPath(thumbnailImagePath.FilePath);
	if (IsStoredPathCorrupt(RelativeThumbnailPath))
	{
		UE_LOG(LogZLCloudPluginSettings, Warning, TEXT("Not writing invalid thumbnailImagePath \"%s\" to config, clearing it"), *thumbnailImagePath.FilePath);
		RelativeThumbnailPath.Empty();
	}


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
	WRITE_CONFIG("timeoutForBlockOnRenderFence", *FString::FromInt(timeoutForBlockOnRenderFence));
	WRITE_CONFIG("initialHeartBeatTimeout", *FString::SanitizeFloat(initialHeartBeatTimeout));
	WRITE_CONFIG("heartBeatTimeout", *FString::SanitizeFloat(heartBeatTimeout));
	WRITE_CONFIG("screenshotFrameWaitCountOverride", *FString::FromInt(screenshotFrameWaitCountOverride));
	WRITE_CONFIG("environmentSettleFrames", *FString::FromInt(environmentSettleFrames));
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
	WRITE_CONFIG("bAllowCinematicQualityOverridesInMediaOnDemandVideos", bAllowCinematicQualityOverridesInMediaOnDemandVideos ? TEXT("True") : TEXT("False"));
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