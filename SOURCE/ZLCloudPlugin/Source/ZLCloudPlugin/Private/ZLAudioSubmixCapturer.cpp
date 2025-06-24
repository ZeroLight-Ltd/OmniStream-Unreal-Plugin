// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLAudioSubmixCapturer.h"

namespace ZLCloudPlugin
{
	ZLAudioSubmixCapturer::ZLAudioSubmixCapturer()
	{
		if (GEngine)
			m_AudioDevice = GEngine->GetActiveAudioDevice();

		m_pluginReady = false;
	}

	ZLAudioSubmixCapturer::~ZLAudioSubmixCapturer()
	{
		if (m_AudioDevice)
			m_AudioDevice.Reset();
	}

	bool ZLAudioSubmixCapturer::Initialise()
	{
		FScopeLock Lock(&CriticalSection);

		if (GEngine)
		{
			if (!m_AudioDevice)
				m_AudioDevice = GEngine->GetActiveAudioDevice();

			if (m_AudioDevice)
			{
#if WITH_EDITOR
				if (m_AudioDevice.GetDeviceID() != GEngine->GetActiveAudioDevice().GetDeviceID())
				{
					m_AudioDevice->UnregisterSubmixBufferListener(this);
					m_AudioDevice.Reset();
					m_AudioDevice = GEngine->GetActiveAudioDevice();

					if (!m_AudioDevice)
						return false;
				}
#endif		
				m_AudioDevice->RegisterSubmixBufferListener(this);

				return true;
			}
			else
			{
				return false;
			}
		}
		else
			return false;
	}

	bool ZLAudioSubmixCapturer::Uninitialise()
	{
		FScopeLock Lock(&CriticalSection);

		if (GEngine && m_AudioDevice)
		{
			m_AudioDevice->UnregisterSubmixBufferListener(this);

			return true;
		}
		else
			return false;
	}

	void ZLAudioSubmixCapturer::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
	{
		FScopeLock Lock(&CriticalSection);

		if (m_pluginReady)
			CloudStream2DLL::OnAudioData(AudioData, (int32_t)SampleRate, (int32_t)NumChannels, (int32_t)NumSamples);
	}
} // namespace ZLCloudPlugin