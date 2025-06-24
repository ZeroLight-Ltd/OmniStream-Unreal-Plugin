// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ISubmixBufferListener.h"
#include "CloudStream2dll.h"

namespace ZLCloudPlugin
{
	class ZLAudioSubmixCapturer : public ISubmixBufferListener
	{
	public:
		ZLAudioSubmixCapturer();
		virtual ~ZLAudioSubmixCapturer();

		bool Initialise();
		bool Uninitialise();
		void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;
	
		bool m_pluginReady;
		TSharedPtr<ZLAudioSubmixCapturer> m_sharedPtr;
	private:
		FAudioDeviceHandle m_AudioDevice;
		FCriticalSection CriticalSection;
	};
} // namespace ZLCloudPlugin
