// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "Components/ActorComponent.h"
#include "IZLCloudPluginModule.h"
#include "ZLCloudPluginInputComponent.generated.h"

/**
 * This component may be attached to an actor to allow UI interactions to be
 * handled as the delegate will be notified about the interaction and will be
 * supplied with a generic descriptor string containing, for example, JSON data.
 * Responses back to the source of the UI interactions may also be sent.
 */
UCLASS(Blueprintable, ClassGroup = (ZLCloudPlugin), meta = (BlueprintSpawnableComponent))
class ZLCLOUDPLUGIN_API UZLCloudPluginInput : public UActorComponent
{
	GENERATED_BODY()

public:
	UZLCloudPluginInput(const FObjectInitializer& ObjectInitializer);
	void BeginPlay() override;
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// The delegate which will be notified about a UI interaction.
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInput, const FString&, Descriptor);
	UPROPERTY(BlueprintAssignable, Category = "ZLCloudPlugin Input")
	FOnInput OnInputEvent;

	/**
	 * Send a response back to the source of the UI interactions.
	 * @param Descriptor - A generic descriptor string.
	 */
	UFUNCTION(BlueprintCallable, Category = "ZLCloudPlugin Input")
	void SendZLCloudPluginResponse(const FString& Descriptor);


private:
	// For convenience we keep a reference to the ZL Cloud plugin.
	IZLCloudPluginModule* ZLCloudPluginModule;
};
