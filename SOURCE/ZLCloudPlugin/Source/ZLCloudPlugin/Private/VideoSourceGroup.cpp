// Copyright ZeroLight ltd. All Rights Reserved.

#include "VideoSourceGroup.h"

#if UNREAL_5_1_OR_NEWER

#include "VideoSource.h"
#include "ZLCloudPluginPrivate.h"
#include "EditorZLCloudPluginSettings.h"

namespace ZLCloudPlugin
{
	TSharedPtr<FVideoSourceGroup> FVideoSourceGroup::Create()
	{
		return TSharedPtr<FVideoSourceGroup>(new FVideoSourceGroup());
	}

	FVideoSourceGroup::FVideoSourceGroup()
		: bCoupleFramerate(true)
	{

		const UZLCloudPluginSettings* Settings = GetDefault<UZLCloudPluginSettings>();
		check(Settings);
		FramesPerSecond = Settings->FramesPerSecond;
	}

	FVideoSourceGroup::~FVideoSourceGroup()
	{
		Stop();
	}

	void FVideoSourceGroup::SetVideoInput(TSharedPtr<FZLCloudPluginVideoInput> InVideoInput)
	{
		if (VideoInput)
		{
			VideoInput->OnFrameCaptured.Remove(FrameDelegateHandle);
		}

		VideoInput = InVideoInput;

		if (VideoInput)
		{
			FrameDelegateHandle = VideoInput->OnFrameCaptured.AddSP(AsShared(), &FVideoSourceGroup::OnFrameCaptured);
		}
	}

	void FVideoSourceGroup::SetFPS(int32 InFramesPerSecond)
	{
		FramesPerSecond = InFramesPerSecond;
	}

	int32 FVideoSourceGroup::GetFPS()
	{
		return FramesPerSecond;
	}

	void FVideoSourceGroup::SetCoupleFramerate(bool Couple)
	{
		bCoupleFramerate = Couple;
	}


	FVideoSource* FVideoSourceGroup::CreateVideoSource(const TFunction<bool()>& InShouldGenerateFramesCheck)
	{
		FVideoSource* NewVideoSource = new FVideoSource(VideoInput, InShouldGenerateFramesCheck);
		{
			FScopeLock Lock(&CriticalSection);
			VideoSources.Add(NewVideoSource);
		}
		CheckStartStopThread();
		return NewVideoSource;
	}

	void FVideoSourceGroup::RemoveVideoSource(const FVideoSource* ToRemove)
	{
		{
			FScopeLock Lock(&CriticalSection);
			VideoSources.RemoveAll([ToRemove](const FVideoSource* Target) {
				return Target == ToRemove;
			});
		}
		CheckStartStopThread();
	}


	void FVideoSourceGroup::RemoveAllVideoSources()
	{
		{
			FScopeLock Lock(&CriticalSection);
			VideoSources.Empty();
		}
		CheckStartStopThread();
	}


	void FVideoSourceGroup::Start()
	{
		if (!bRunning)
		{
			if (VideoSources.Num() > 0)
			{
				StartThread();
			}
			bRunning = true;
		}
	}

	void FVideoSourceGroup::Stop()
	{
		if (bRunning)
		{
			StopThread();
			bRunning = false;
		}
	}

	void FVideoSourceGroup::Tick()
	{
		FScopeLock Lock(&CriticalSection);
		// for each player session, push a frame
		for (auto& VideoSource : VideoSources)
		{
			if (VideoSource)
			{
				VideoSource->MaybePushFrame();
			}
		}
	}

	void FVideoSourceGroup::OnFrameCaptured()
	{
		if (bCoupleFramerate)
		{
			Tick();
		}
#if UNREAL_5_7_OR_NEWER
		else
		{
			// We are in decoupled render/streaming mode - we should wake our VideoSourceGroup::FFrameThread (if it is sleeping)
			if(FrameRunnable && FrameRunnable->FrameEvent.Get())
			{
				FrameRunnable->FrameEvent.Get()->Trigger();
			}
		}
#endif
	}

	void FVideoSourceGroup::StartThread()
	{
		if (!bCoupleFramerate && !bThreadRunning)
		{
#if UNREAL_5_7_OR_NEWER
			FrameRunnable = MakeUnique<FFrameThread>(AsWeak());
			FrameThread = FRunnableThread::Create(FrameRunnable.Get(), TEXT("FVideoSourceGroup Thread"), 0, TPri_TimeCritical);
#else
			FrameRunnable = MakeUnique<FFrameThread>(this);
			FrameThread = FRunnableThread::Create(FrameRunnable.Get(), TEXT("FVideoSourceGroup Thread"));	
#endif
			bThreadRunning = true;
		}
	}

	void FVideoSourceGroup::StopThread()
	{
		if (FrameThread != nullptr)
		{
			FrameThread->Kill(true);
		}
		FrameThread = nullptr;
		bThreadRunning = false;
	}

	void FVideoSourceGroup::CheckStartStopThread()
	{
		if (bRunning)
		{
			const int32 NumSources = VideoSources.Num();
			if (bThreadRunning && NumSources == 0)
			{
				StopThread();
			}
			else if (!bThreadRunning && NumSources > 0)
			{
				StartThread();
			}
		}
	}

	bool FVideoSourceGroup::FFrameThread::Init()
	{
		return true;
	}

	uint32 FVideoSourceGroup::FFrameThread::Run()
	{
		bIsRunning = true;

		while (bIsRunning)
		{
#if UNREAL_5_7_OR_NEWER
			if(TSharedPtr<FVideoSourceGroup> VideoSourceGroup = OuterVideoSourceGroup.Pin())
			{
				const double TimeSinceLastSubmitMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - LastSubmitCycles);

				// Decrease this value to make expected frame delivery more precise, however may result in more old frames being sent
				const double PrecisionFactor = 0.1;
				// ZL: Using a default wait factor since we don't have the PixelStreaming CVar
				const double WaitFactor = 1.0;

				// In "auto" mode vary this value based on historical average
				const double TargetSubmitMs = 1000.0 / VideoSourceGroup->FramesPerSecond;
				const double TargetSubmitMsWithPadding = TargetSubmitMs * WaitFactor;
				const double CloseEnoughMs = TargetSubmitMs * PrecisionFactor;
				const bool bFrameOverdue = TimeSinceLastSubmitMs >= TargetSubmitMsWithPadding;

				// Check frame arrived in time
				if(!bFrameOverdue)
				{
					// Frame arrived in a timely fashion, but is it too soon to maintain our target rate? If so, sleep.
					double WaitTimeRemainingMs = TargetSubmitMsWithPadding - TimeSinceLastSubmitMs;
					if(WaitTimeRemainingMs > CloseEnoughMs)
					{
						bool bGotNewFrame = FrameEvent.Get()->Wait(WaitTimeRemainingMs);
						if(!bGotNewFrame)
						{
							UE_LOG(LogZLCloudPlugin, VeryVerbose, TEXT("Old frame submitted"));
						}
					}
				}

				// Push frame immediately
				PushFrame(VideoSourceGroup);

			}
#else
			uint64 PreTickCycles = FPlatformTime::Cycles64();

			PushFrame();

			const double DeltaMs = FPlatformTime::ToMilliseconds64(LastTickCycles - PreTickCycles);
			const double FrameDeltaMs = 1000.0 / TickGroup->FramesPerSecond;
			const double SleepMs = FrameDeltaMs - DeltaMs;
			PreTickCycles = LastTickCycles;

			// Sleep as long as we need for a constant FPS
			if (SleepMs > 0)
			{
				FPlatformProcess::Sleep(static_cast<float>(SleepMs / 1000.0));
			}
#endif
		}
		
		return 0;
	}

	void FVideoSourceGroup::FFrameThread::Stop()
	{
		bIsRunning = false;
	}

	void FVideoSourceGroup::FFrameThread::Exit()
	{
		bIsRunning = false;
	}

#if UNREAL_5_7_OR_NEWER

	/*
	* Note this function is required as part of `FSingleThreadRunnable` and only gets called when engine is run in single-threaded mode, 
	* so the logic is much less complex as this is not a case we particularly optimize for, a simple tick on an interval will be acceptable.
	*/
	void FVideoSourceGroup::FFrameThread::Tick()
	{
		if(TSharedPtr<FVideoSourceGroup> VideoSourceGroup = OuterVideoSourceGroup.Pin())
		{
			const uint64 NowCycles = FPlatformTime::Cycles64();
			const double DeltaMs = FPlatformTime::ToMilliseconds64(NowCycles - LastSubmitCycles);
			const double TargetSubmitMs = 1000.0 / VideoSourceGroup->FramesPerSecond;
			if (DeltaMs >= TargetSubmitMs)
			{
				PushFrame(VideoSourceGroup);
			}
		}
	}

	void FVideoSourceGroup::FFrameThread::PushFrame(TSharedPtr<FVideoSourceGroup> VideoSourceGroup)
	{
		VideoSourceGroup->Tick();
		LastSubmitCycles = FPlatformTime::Cycles64();
	}

	double FVideoSourceGroup::FFrameThread::CalculateSleepOffsetMs(double TargetSubmitMs, uint64 LastCaptureCycles, uint64 CyclesBetweenCaptures, bool& bResetOffset) const
	{
		// This method is defined for API compatibility but not currently used in the Run() method
		// It could be used for more advanced timing adjustments in the future
		bResetOffset = false;
		return 0.0;
	}
	
#else

	void FVideoSourceGroup::FFrameThread::Tick()
	{
		const uint64 NowCycles = FPlatformTime::Cycles64();
		const double DeltaMs = FPlatformTime::ToMilliseconds64(NowCycles - LastTickCycles);
		const double FrameDeltaMs = 1000.0 / TickGroup->FramesPerSecond;
		if (DeltaMs >= FrameDeltaMs)
		{
			PushFrame();
		}
	}

	void FVideoSourceGroup::FFrameThread::PushFrame()
	{
		TickGroup->Tick();
		LastTickCycles = FPlatformTime::Cycles64();
	}
		
#endif
} // namespace ZLCloudPlugin

#endif
