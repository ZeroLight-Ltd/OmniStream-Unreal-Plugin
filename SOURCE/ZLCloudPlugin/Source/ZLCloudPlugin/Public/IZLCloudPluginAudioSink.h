// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IZLCloudPluginAudioConsumer;

// Interface for a sink that collects audio coming in from the browser and passes into into UE's audio system.
class ZLCLOUDPLUGIN_API IZLCloudPluginAudioSink
{
public:
	IZLCloudPluginAudioSink() {}
	virtual ~IZLCloudPluginAudioSink() {}
	virtual void AddAudioConsumer(IZLCloudPluginAudioConsumer* AudioConsumer) = 0;
	virtual void RemoveAudioConsumer(IZLCloudPluginAudioConsumer* AudioConsumer) = 0;
	virtual bool HasAudioConsumers() = 0;
};