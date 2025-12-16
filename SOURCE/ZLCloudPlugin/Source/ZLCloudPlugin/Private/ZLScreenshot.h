// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "PCH.h"
#include "CoreFwd.h"
#include "ZLStopwatch.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "ZLCloudPluginApplicationWrapper.h"
#include "ZLCloudPluginInputHandler.h"
#include "MoviePipelineExecutor.h"
#include "InputDevice.h"
#include <fstream>
#include <vector>
#include <string>
#include "ZLCloudPluginDelegates.h"
#include "ZLCloudPluginVersion.h"

class LauncherComms;
class UZLCloudPluginStateManager;


namespace ZLCloudPlugin
{
	enum ScreenshotType
	{
		DEFAULT2D,        //Standard single screenshot image
		EQUIRECT360,      //Returns equirectangular projection single image of cubemap faces
		//CUBEMAP360,     //Returns individual images for each face - not yet implemented, but might be in future
		//STEREOPANO360,  //Returns 2 equirectangular images for stereoscopic panoramas - not yet implemented, but might be in future
	};

	class ZLScreenshotJob
	{
	public:
		ZLScreenshotJob() {};
		ZLScreenshotJob(int32 InWidth, int32 InHeight, FString& InFileName, FString& InPath, FString& InUID, UWorld* InWorld, const TSharedPtr<FJsonObject>* stateRequestData, ScreenshotType InScreenshotType = ScreenshotType::DEFAULT2D);

	public:

		int32 width, height;
		UWorld* world;
		FString format;
		FString path;
		FString uid;
		bool useMRQPipeline = false;
		TArray<FString> cVarOverrides;
		TSharedPtr<FJsonObject> stateData = nullptr;
		bool stateRequestSent = false;
		double jobStartTime = 0.0f;
		double captureStartTime = 0.0f;
		bool needsCameraPostprocessAdjustmentPerFace = false;


		ScreenshotType type = ScreenshotType::DEFAULT2D;
		int32 faceID = -1;
		int32 lastFaceCompletedID = -1;

		TSharedPtr<FJsonObject> postJobCurrentState = nullptr;
		TSharedPtr<FJsonObject> postJobTimeoutState = nullptr;
		TSharedPtr<FJsonObject> postJobUnmatchedState = nullptr;
		bool jobStateRequestFinished = false;
		bool jobStateRequestSuccess = false;
		bool jobCaptureStarted = false;

		TObjectPtr<UMoviePipelineExecutorJob> ActiveRenderJob;
	};

	class ZLScreenshot
	{
	public:
		ZLScreenshot();
		~ZLScreenshot();

		/**
		 * Create the singleton.
		 */
		static TSharedPtr<ZLScreenshot> Singleton;
		static TSharedPtr<ZLScreenshot> CreateInstance();

		static TSharedPtr<ZLScreenshot> Get()
		{
			if (Singleton == nullptr)
			{
				return CreateInstance();
			}
			return Singleton;
		}

		void Init(LauncherComms* launcherComms);
		void Update();
		bool PerformMRQCapture(int width, int height, FString outputPath, FString jobName);
		bool RequestScreenshot(const char* settingsJson, UWorld* InWorld, FString& errorMsgOut);
		void UpdateCurrentRenderStateRequestProgress(bool completed, bool matchSuccess);
		void SetCurrentRenderStateData(TSharedPtr<FJsonObject> currentState, TSharedPtr<FJsonObject> timeoutState = nullptr, TSharedPtr<FJsonObject> unmatchedState = nullptr);
		inline bool HasCurrentRender() { return m_CurrentRender.IsValid(); }
		void Set2DODMode(bool is2DOD);
		bool LoadImageAsFColorArray(const FString& ImagePath, TArray<FColor>& OutColorData, int32& OutWidth, int32& OutHeight);

#if UNREAL_5_3_OR_NEWER
		void OnMoviePipelineFinished(FMoviePipelineOutputData Results);
#endif

	private:
		void OnScreenshotComplete(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData);

		void SendImageFailureResponse(FString& errorMsg);

		void PauseGameTime();
		void ResumeGameTime();

		bool s_rejectUnmatchedStateJobs = false;

		float m_cacheTimeDilation = 1.0f;

		ACameraActor* CameraActor = nullptr;
		bool bCreatedCamera = false;

		TSharedPtr<ZLScreenshotJob> m_NextRender;
		TSharedPtr<ZLScreenshotJob> m_CurrentRender;

		LauncherComms* m_LauncherComms;

		bool m_Initialized;

		FDelegateHandle m_onScreenshotCapturedDelegateHandle;

		static constexpr int s_hiresScreenshotFrameCount = 8;
		static constexpr int s_hiresScreenshotFrameCountRaytracing = 32;

		APlayerController* m_playerController;
		AActor* m_initialViewTarget = nullptr;
		ACameraActor* m_panoViewTarget = nullptr;

		FQuat m_startRotation;
		FQuat m_faceRotation[6];

		uint32_t m_faceSize = 0;
		unsigned char* m_outBuffer = nullptr;
		int** m_imageData = nullptr;
		int m_numPixels = 0;
		bool m_injectMetadata = false;
		bool m_equirect360JobFinished = false;
		FString m_finalOutpath = "";
		int m_imageBytesSize = 0;
		float m_cacheAutoExposureMaxBrightness = 8.0f;
		float m_cacheAutoExposureMinBrightness = 0.03f;
		float m_cacheVignetteIntensity = 0.4f;

		UZLCloudPluginDelegates* Delegates = nullptr;
	};
}
