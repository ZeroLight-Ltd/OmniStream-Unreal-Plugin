// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Interface for consuming audio coming in from the browser.
class ZLCLOUDPLUGIN_API IZLCloudPluginAudioConsumer
{
public:
	IZLCloudPluginAudioConsumer(){};
	virtual ~IZLCloudPluginAudioConsumer(){};
	virtual void ConsumeRawPCM(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames) = 0;
	virtual void OnConsumerAdded() = 0;
	virtual void OnConsumerRemoved() = 0;
};
