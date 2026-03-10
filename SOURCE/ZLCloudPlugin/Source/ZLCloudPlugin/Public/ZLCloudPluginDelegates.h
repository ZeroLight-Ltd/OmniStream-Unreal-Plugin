// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginPlayerId.h"
#include "Containers/UnrealString.h"
#include "Delegates/DelegateSignatureImpl.inl"
#include "Dom/JsonObject.h"
#include "Math/Matrix.h"
#include "Math/IntRect.h"
#include "ZLCloudPluginDelegates.generated.h"

/** Filled by plugins (e.g. from screenshot capture frame) so screenshot code can use exact view for projection */
struct FZLCaptureViewInfo
{
	FMatrix ViewProjectionMatrix = FMatrix::Identity;
	FIntRect ViewRect;
	bool bValid = false;
};

/** Params for OnPrepareScreenshot: either use capture view matrix (bUseCaptureView) or fallback to width/height */
struct FZLPrepareScreenshotParams
{
	int32 CaptureWidth = 0;
	int32 CaptureHeight = 0;
	bool bUseCaptureView = false;
	FMatrix ViewProjectionMatrix = FMatrix::Identity;
	FIntRect ViewRect;
};


UCLASS()
class ZLCLOUDPLUGIN_API UZLCloudPluginDelegates : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Connected
	 */
	// BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FConnectedStream);
	UPROPERTY(BlueprintAssignable, Category = "Zerolight Omnistream Delegates")
	FConnectedStream OnConnectedStream;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE(FConnectedStreamNative);
	FConnectedStreamNative OnConnectedStreamNative;


	/**
	 * Disconnected
	 */
	// BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDisconnectedStream);
	UPROPERTY(BlueprintAssignable, Category = "Zerolight Omnistream Delegates")
	FDisconnectedStream OnDisconnectedStream;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE(FDisconnectedStreamNative);
	FDisconnectedStreamNative OnDisconnectedStreamNative;


	/**
	 * Recieve Data
	 */
	 // BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRecieveData, FString, jsonData);
	UPROPERTY(BlueprintAssignable, Category = "Zerolight Omnistream Delegates")
	FRecieveData OnRecieveData;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE_OneParam(FRecieveDataNative, FString);
	FRecieveDataNative OnRecieveDataNative;


	/**
	 * On Content Generation Start 
	 */
	 // BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnContentGenerationBegin, bool, bIsPanoImage);
	UPROPERTY(BlueprintAssignable, Category = "Zerolight Omnistream Delegates")
	FOnContentGenerationBegin OnContentGenerationBegin;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnContentGenerationBeginNative, bool);
	FOnContentGenerationBeginNative OnContentGenerationBeginNative;

	/**
	 * On Content Generation Finished
	 */
	 // BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnContentGenerationFinished, bool, bIsPanoImage);
	UPROPERTY(BlueprintAssignable, Category = "Zerolight Omnistream Delegates")
	FOnContentGenerationFinished OnContentGenerationFinished;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnContentGenerationFinishedNative, bool);
	FOnContentGenerationFinishedNative OnContentGenerationFinishedNative;

	/**
	 * On Get Version Info - allows other plugins to add their version information
	 */
	// C++ Delegate - takes a shared JSON object that plugins can modify to add their version info
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnGetVersionInfoNative, TSharedPtr<FJsonObject>);
	FOnGetVersionInfoNative OnGetVersionInfoNative;

	/**
	 * On Get Screenshot Capture View - called before OnPrepareScreenshot; plugins that have the capture frame's view (e.g. from scene view extension) fill OutCaptureView. Use this so projection uses the exact screenshot view matrix.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnGetScreenshotCaptureViewNative, FZLCaptureViewInfo&);
	FOnGetScreenshotCaptureViewNative OnGetScreenshotCaptureViewNative;

	/**
	 * On Prepare Screenshot - called before building screenshot response JSON. When Params.bUseCaptureView is true, use Params.ViewProjectionMatrix and Params.ViewRect for projection; otherwise use Params.CaptureWidth/CaptureHeight.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPrepareScreenshotNative, const FZLPrepareScreenshotParams&);
	FOnPrepareScreenshotNative OnPrepareScreenshotNative;

	/**
	 * On Build Screenshot State - allows other plugins to add extra data into screenshot response JSON
	 * @param ResponseStateData - the JSON object being built for the screenshot response
	 * @param CaptureWorld - the world for which the screenshot was captured (so plugins can match PIE/correct world)
	 */
	// C++ Delegate - takes the response JSON object and the world that was captured
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnBuildScreenshotStateNative, TSharedPtr<FJsonObject>, class UWorld*);
	FOnBuildScreenshotStateNative OnBuildScreenshotStateNative;

	/**
	 * On Screenshot Requested - fired when a high-res screenshot is about to be taken (before TakeHighResScreenShot).
	 * Allows plugins to e.g. enable immediate occlusion readback for the capture frame.
	 * @param Width - capture width
	 * @param Height - capture height
	 * @param World - world being captured
	 */
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnScreenshotRequestedNative, int32, int32, class UWorld*);
	FOnScreenshotRequestedNative OnScreenshotRequestedNative;

	/**
	 * On Before Build Screenshot State - fired just before OnPrepareScreenshot when building the screenshot response.
	 * Allows plugins to drain occlusion results for the capture frame (e.g. ProcessAccumulatedResults + SetImmediateReadback(false)).
	 * @param CaptureWorld - the world that was captured
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBeforeBuildScreenshotStateNative, class UWorld*);
	FOnBeforeBuildScreenshotStateNative OnBeforeBuildScreenshotStateNative;


	/**
	 * Create the singleton.
	 */
	static UZLCloudPluginDelegates* CreateInstance();

	static UZLCloudPluginDelegates* GetZLCloudPluginDelegates()
	{
		if (Singleton == nullptr)
		{
			return CreateInstance();
		}
		return Singleton;
	}

private:
	// The singleton object.
	static UZLCloudPluginDelegates* Singleton;
};
