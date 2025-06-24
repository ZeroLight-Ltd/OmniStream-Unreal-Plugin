// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginInputComponent.h"
#include "IZLCloudPluginModule.h"
#include "ZLCloudPluginModule.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/GameUserSettings.h"
#include "ZLCloudPluginPrivate.h"
#include "ZLCloudPluginProtocol.h"
//#include "IZLCloudPluginStreamer.h"
#include "Utils.h"

UZLCloudPluginInput::UZLCloudPluginInput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ZLCloudPluginModule(ZLCloudPlugin::FZLCloudPluginModule::GetModule())
{
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	SetComponentTickEnabled(true);
}

void UZLCloudPluginInput::BeginPlay()
{
	Super::BeginPlay();

	if (ZLCloudPluginModule)
	{
		// When this component is initializing it registers itself with the ZLCloudStream module.
		ZLCloudPluginModule->AddInputComponent(this);
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLCloudStream input component not added because ZLCloudStream module is not loaded. This is expected on dedicated servers."));
	}
}

void UZLCloudPluginInput::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (ZLCloudPluginModule)
	{
		// When this component is destructing it unregisters itself with the ZLCloudStream module.
		ZLCloudPluginModule->RemoveInputComponent(this);
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLCloudStream input component not removed because ZLCloudStream module is not loaded. This is expected on dedicated servers."));
	}
}

void UZLCloudPluginInput::SendZLCloudPluginResponse(const FString& Descriptor)
{
	if (ZLCloudPluginModule)
	{
		ZLCloudPluginModule->SendData(Descriptor);
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLCloudStream input component skipped sending response. This is expected on dedicated servers."));
	}
}

