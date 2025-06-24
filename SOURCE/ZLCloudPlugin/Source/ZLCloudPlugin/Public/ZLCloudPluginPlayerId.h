// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

using FZLCloudPluginPlayerId = FString;

ZLCLOUDPLUGIN_API inline FZLCloudPluginPlayerId ToPlayerId(FString PlayerIdString)
{
	return FZLCloudPluginPlayerId(PlayerIdString);
}

ZLCLOUDPLUGIN_API inline FZLCloudPluginPlayerId ToPlayerId(int32 PlayerIdInteger)
{
	return FString::FromInt(PlayerIdInteger);
}

ZLCLOUDPLUGIN_API inline int32 PlayerIdToInt(FZLCloudPluginPlayerId PlayerId)
{
	return FCString::Atoi(*PlayerId);
}

static const FZLCloudPluginPlayerId INVALID_PLAYER_ID = ToPlayerId(FString(TEXT("Invalid Player Id")));
static const FZLCloudPluginPlayerId SFU_PLAYER_ID = FString(TEXT("1"));