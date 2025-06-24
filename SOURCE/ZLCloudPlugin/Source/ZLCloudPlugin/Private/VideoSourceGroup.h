// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginVersion.h"
#if UNREAL_5_1_OR_NEWER

#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/SingleThreadRunnable.h"
#include "Templates/SharedPointer.h"
#include "VideoSource.h"
#include "ZLCloudpluginVideoInput.h"

namespace ZLCloudPlugin
{
	class FVideoSourceGroup : public TSharedFromThis<FVideoSourceGroup>
	{
	public:
		static TSharedPtr<FVideoSourceGroup> Create();
		~FVideoSourceGroup();

		void SetVideoInput(TSharedPtr<FZLCloudPluginVideoInput> InVideoInput);
		TSharedPtr<FZLCloudPluginVideoInput> GetVideoInput() { return VideoInput; }
		void SetFPS(int32 InFramesPerSecond);
		int32 GetFPS();

		void SetCoupleFramerate(bool Couple);

		FVideoSource* CreateVideoSource(const TFunction<bool()>& InShouldGenerateFramesCheck);
		void RemoveVideoSource(const FVideoSource* ToRemove);
		void RemoveAllVideoSources();

		void Start();
		void Stop();
		void Tick();
		bool IsThreadRunning() const { return bRunning; }

	private:
		FVideoSourceGroup();

		void StartThread();
		void StopThread();
		void CheckStartStopThread();
		void OnFrameCaptured();

		class FFrameThread : public FRunnable, public FSingleThreadRunnable
		{
		public:
			FFrameThread(FVideoSourceGroup* InTickGroup)
				: TickGroup(InTickGroup)
			{
			}
			virtual ~FFrameThread() = default;

			virtual bool Init() override;
			virtual uint32 Run() override;
			virtual void Stop() override;
			virtual void Exit() override;

			virtual FSingleThreadRunnable* GetSingleThreadInterface() override
			{
				bIsRunning = true;
				return this;
			}

			virtual void Tick() override;

			void PushFrame();

			bool bIsRunning = false;
			FVideoSourceGroup* TickGroup = nullptr;
			uint64 LastTickCycles = 0;
		};

		bool bRunning = false;
		bool bThreadRunning = false;
		bool bCoupleFramerate = false;
		int32 FramesPerSecond = 30;
		TSharedPtr<FZLCloudPluginVideoInput> VideoInput;
		TUniquePtr<FFrameThread> FrameRunnable;
		FRunnableThread* FrameThread = nullptr; // constant FPS tick thread
		TArray<FVideoSource*> VideoSources;

		FDelegateHandle FrameDelegateHandle;

		mutable FCriticalSection CriticalSection;
	};
} // namespace ZLCloudPlugin

#endif
