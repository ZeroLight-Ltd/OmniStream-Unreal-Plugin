// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Delegates/DelegateSignatureImpl.inl"
#include "ZLTrackedStateBlueprint.generated.h"

UCLASS()
class ZLCLOUDPLUGIN_API UZLTrackedStateBlueprint : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UZLTrackedStateBlueprint(const FObjectInitializer& ObjectInitializer);

    // Function to save variables of a Blueprint instance to JSON
    UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Tracked State")
    FString SaveStateBlueprintVariables(UObject* BlueprintInstance);

    // Function to load variables from JSON to a Blueprint instance
    UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Tracked State")
    void ProcessStateBlueprintVariables(UObject* BlueprintInstance, const FString& JsonString);

    // Function to send state to the state manager
    UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Tracked State")
    void SendToStateManager(const FString& bluePrintName, const FString& JsonString);

	// Function to get state from the state manager
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Tracked State")
	bool GetFromStateManager(const FString& bluePrintName, FString& OutJsonString);

    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTrackedStateUpdated);
    UPROPERTY(BlueprintAssignable, Category = "Zerolight Omnistream Tracked State")
    FTrackedStateUpdated OnTrackedStateUpdate;


	/**
	 * Create the singleton.
	 */
	static UZLTrackedStateBlueprint* CreateInstance();


	// GetZLTrackedStateInstance
	/**
	 * Get the singleton. This allows application-specific blueprints to bind
	 * to delegates of interest.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Tracked State")
	static UZLTrackedStateBlueprint* GetZLTrackedStateInstance()
	{
		if (Singleton == nullptr)
		{
			return CreateInstance();
		}
		return Singleton;
	}

private:
	// The singleton object.
	static UZLTrackedStateBlueprint* Singleton;
};