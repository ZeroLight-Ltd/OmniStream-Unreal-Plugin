// ZLSpotLightDataDrivenUIComponent.h

#pragma once

#include "CoreMinimal.h"
#include "IZLCloudPluginModule.h"
#include "Components/ActorComponent.h"
#include "ZLSpotLightDataDrivenUIManager.generated.h"

UCLASS(Blueprintable, ClassGroup = (ZLSpotLight) , meta = (BlueprintSpawnableComponent))
class ZLCLOUDPLUGIN_API UZLSpotLightDataDrivenUIManager : public UActorComponent
{
	GENERATED_BODY()

public:
	UZLSpotLightDataDrivenUIManager();

	// Blueprint event delegate for receiving data from server
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDataReceived, const FString&, Data);

	// Register a category name for this instance
	UFUNCTION(BlueprintCallable, Category = "ZLSpotLight|DataDriven")
	FString RegisterCategoryName(const FString& CategoryName);

	// Unregister a category name for this instance
	UFUNCTION(BlueprintCallable, Category = "ZLSpotLight|DataDriven")
	void UnregisterCategoryName();

	// Static function to sync all registered data to server
	static FString SyncDataToServer(bool bSimpleFormat);

	// Static function to receive and distribute data from server
	UFUNCTION(BlueprintCallable, Category = "ZLSpotLight|DataDriven")
	static void ReceiveDataFromServer(const FString& ServerData);

	// Blueprint delegate for receiving data
	UPROPERTY(BlueprintAssignable, Category = "ZLSpotLight|DataDriven")
	FOnDataReceived OnDataReceived;

	UFUNCTION(BlueprintCallable, Category = "SpotLight|Data")
	void SetJsonData(const FString& InJsonData);

protected:
	UPROPERTY()
	FString StoredJsonData;

private:
	// Stored category name for this instance
	FString CategoryName;

	// Static map to store all registered instances
	static TMap<FString, UZLSpotLightDataDrivenUIManager*> RegisteredManagers;

	// Helper function to generate unique category name
	static FString GenerateUniqueCategoryName(const FString& BaseName);
};
