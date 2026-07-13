// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLAudioSubmixCapturer.h"

namespace ZLCloudPlugin
{
	ZLAudioSubmixCapturer::ZLAudioSubmixCapturer()
	{
		m_pluginReady = false;
	}

	ZLAudioSubmixCapturer::~ZLAudioSubmixCapturer()
	{
		Uninitialise();
	}

	bool ZLAudioSubmixCapturer::Initialise()
	{
		FScopeLock Lock(&CriticalSection);

		if (!GEngine)
		{
			return false;
		}

		FAudioDeviceHandle ActiveAudioDevice = GEngine->GetActiveAudioDevice();
		if (!ActiveAudioDevice)
		{
			ActiveAudioDevice = GEngine->GetMainAudioDevice();
		}

		if (!ActiveAudioDevice)
		{
			return false;
		}

		const bool bDeviceChanged = m_AudioDevice && (m_AudioDevice.GetDeviceID() != ActiveAudioDevice.GetDeviceID());
		if (bDeviceChanged && m_bIsRegistered)
		{
			m_AudioDevice->UnregisterSubmixBufferListener(this);
			m_bIsRegistered = false;
		}

		if (bDeviceChanged || !m_AudioDevice)
		{
			m_AudioDevice = ActiveAudioDevice;
		}

		if (!m_bIsRegistered)
		{
			m_AudioDevice->RegisterSubmixBufferListener(this);
			m_bIsRegistered = true;
		}

		return true;
	}

	bool ZLAudioSubmixCapturer::Uninitialise()
	{
		FScopeLock Lock(&CriticalSection);

		if (m_AudioDevice)
		{
			if (m_bIsRegistered)
			{
				m_AudioDevice->UnregisterSubmixBufferListener(this);
				m_bIsRegistered = false;
			}

			m_AudioDevice.Reset();

			return true;
		}

		return false;
	}

	void ZLAudioSubmixCapturer::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
	{
		FScopeLock Lock(&CriticalSection);

		if (m_pluginReady)
			CloudStream2DLL::OnAudioData(AudioData, (int32_t)SampleRate, (int32_t)NumChannels, (int32_t)NumSamples);
	}
} // namespace ZLCloudPlugin