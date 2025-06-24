// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginPlayerId.h"
#include "Containers/UnrealString.h"
#include "Delegates/DelegateSignatureImpl.inl"
#include "ZLCloudPluginDelegates.generated.h"


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
	 * Pre-panoramic capture events
	 */
	 // BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPrePanoCapture);
	UPROPERTY(BlueprintAssignable, Category = "Zerolight Omnistream Delegates")
	FPrePanoCapture OnPrePanoCapture;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE(FPrePanoCaptureNative);
	FPrePanoCaptureNative OnPrePanoCaptureNative;


	/**
	 * Post-panoramic capture events
	 */
	 // BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPostPanoCapture);
	UPROPERTY(BlueprintAssignable, Category = "Zerolight Omnistream Delegates")
	FPrePanoCapture OnPostPanoCapture;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE(FPostPanoCaptureNative);
	FPostPanoCaptureNative OnPostPanoCaptureNative;


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
