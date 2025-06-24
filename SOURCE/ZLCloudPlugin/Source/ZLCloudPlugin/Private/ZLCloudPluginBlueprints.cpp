// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginBlueprints.h"
#include "ZLCloudPluginPrivate.h"
#include "Misc/FileHelper.h"
#include "ZLCloudPluginInputComponent.h"
#include "ZLCloudPluginAudioComponent.h"
#include "ZLCloudPluginPlayerId.h"
#include "ZLCloudPluginStateManager.h"

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
