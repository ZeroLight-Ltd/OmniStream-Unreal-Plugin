// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginPlayerId.h"
#include "Containers/UnrealString.h"
#include "Delegates/DelegateSignatureImpl.inl"
#include "Dom/JsonObject.h"
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
