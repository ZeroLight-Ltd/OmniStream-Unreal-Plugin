// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IZLCloudPluginModule.h"
#include "ZLCloudPluginModule.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Containers/Array.h"
#include "ZLCloudPluginDelegates.h"
#include "ZLCloudPluginBlueprints.generated.h"

UCLASS()
class ZLCLOUDPLUGIN_API UZLCloudPluginBlueprints : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/**
     * Send json data to webpage hosting the stream
     * 
     * @param   jsonData	json string to we sent to web page
     */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Send Data")
	static void SendData(FString jsonData);


	// ZLCloudPluginDelegates
	/**
	 * Get the singleton. This allows application-specific blueprints to bind
	 * to delegates of interest.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Delegates")
	static UZLCloudPluginDelegates* GetZLCloudPluginDelegates();

	/** True when a Play-In-Editor or standalone play session has an active game world. */
	UFUNCTION(BlueprintPure, Category = "Zerolight Omnistream Capture")
	static bool IsPiePlayActive();

	/** True when OmniStream Settings has "Download ffmpeg for media on demand videos" enabled. */
	UFUNCTION(BlueprintPure, Category = "Zerolight Omnistream Capture")
	static bool IsFfmpegVideoSupportEnabled();

	/**
	 * Queue a screenshot or video capture job through ZLScreenshot using the same JSON
	 * payload as a server CAPTUREIMAGE / media request. Requires an active play session.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Capture")
	static bool RequestScreenshotJob(const FString& SettingsJson, FString& OutErrorMessage);
};