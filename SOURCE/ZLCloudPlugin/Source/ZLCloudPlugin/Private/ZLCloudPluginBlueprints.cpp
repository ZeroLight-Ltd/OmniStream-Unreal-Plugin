// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginBlueprints.h"
#include "ZLCloudPluginPrivate.h"
#include "Misc/FileHelper.h"
#include "ZLCloudPluginInputComponent.h"
#include "ZLCloudPluginAudioComponent.h"
#include "ZLCloudPluginPlayerId.h"
#include "ZLCloudPluginStateManager.h"
#include "EditorZLCloudPluginSettings.h"
#include "ZLScreenshot.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

void UZLCloudPluginBlueprints::SendData(FString jsonData)
{
	IZLCloudPluginModule* Module = ZLCloudPlugin::FZLCloudPluginModule::GetModule();
	if (Module)
	{
		Module->SendData(jsonData);
	}
}

UZLCloudPluginDelegates* UZLCloudPluginBlueprints::GetZLCloudPluginDelegates()
{
	return UZLCloudPluginDelegates::GetZLCloudPluginDelegates();
}

static UWorld* GetActivePlayWorldForCapture()
{
	UWorld* World = GEngine ? GEngine->GetCurrentPlayWorld() : nullptr;
#if WITH_EDITOR
	if (World == nullptr && GEngine)
	{
		if (UGameViewportClient* Viewport = GEngine->GameViewport)
		{
			World = Viewport->GetWorld();
		}
	}
#endif
	return World;
}

bool UZLCloudPluginBlueprints::IsPiePlayActive()
{
#if WITH_EDITOR
	if (GEditor && GEditor->PlayWorld)
	{
		return true;
	}
#endif
	return GEngine && GEngine->GetCurrentPlayWorld() != nullptr;
}

bool UZLCloudPluginBlueprints::IsFfmpegVideoSupportEnabled()
{
	const UZLCloudPluginSettings* PluginSettings = GetDefault<UZLCloudPluginSettings>();
	return PluginSettings && PluginSettings->bDownloadFFmpegForMediaOnDemandVideos;
}

bool UZLCloudPluginBlueprints::EnableFfmpegVideoSupport()
{
#if WITH_EDITOR
	UZLCloudPluginSettings* PluginSettings = GetMutableDefault<UZLCloudPluginSettings>();
	if (PluginSettings == nullptr)
	{
		return false;
	}

	if (PluginSettings->bDownloadFFmpegForMediaOnDemandVideos)
	{
		return true;
	}

	FProperty* Property = UZLCloudPluginSettings::StaticClass()->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(UZLCloudPluginSettings, bDownloadFFmpegForMediaOnDemandVideos));
	if (Property)
	{
		PluginSettings->PreEditChange(Property);
	}

	PluginSettings->bDownloadFFmpegForMediaOnDemandVideos = true;

	// Same path as ticking the setting in OmniStream Settings: save the ini and download FFmpeg.
	FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ValueSet);
	PluginSettings->PostEditChangeProperty(PropertyChangedEvent);
	return PluginSettings->bDownloadFFmpegForMediaOnDemandVideos;
#else
	return false;
#endif
}

bool UZLCloudPluginBlueprints::RequestScreenshotJob(const FString& SettingsJson, FString& OutErrorMessage)
{
	OutErrorMessage.Empty();

	UWorld* World = GetActivePlayWorldForCapture();
	if (World == nullptr)
	{
		OutErrorMessage = TEXT("Request unhandled, no active Play-In-Editor/Standalone running");
		return false;
	}

	const auto AnsiString = StringCast<ANSICHAR>(*SettingsJson);
	const char* CharMessage = AnsiString.Get();

#ifdef SUPPORT_LEGACY_MESSAGES
	const bool bQueued = ZLCloudPlugin::ZLScreenshot::Get()->RequestScreenshot(CharMessage, World, OutErrorMessage, false);
#else
	const bool bQueued = ZLCloudPlugin::ZLScreenshot::Get()->RequestScreenshot(CharMessage, World, OutErrorMessage);
#endif

	return bQueued;
}

bool UZLCloudPluginBlueprints::IsScreenshotJobActive()
{
	TSharedPtr<ZLCloudPlugin::ZLScreenshot> Screenshot = ZLCloudPlugin::ZLScreenshot::Get();
	if (Screenshot.IsValid() && (Screenshot->HasCurrentRender() || Screenshot->HasNextRender()))
	{
		return true;
	}

	if (UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager())
	{
		if (StateManager->IsProcessingStateRequest())
		{
			return true;
		}
	}

	return false;
}
