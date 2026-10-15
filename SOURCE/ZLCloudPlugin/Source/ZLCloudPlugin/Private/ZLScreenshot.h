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

#define SUPPORT_LEGACY_MESSAGES

class LauncherComms;
class UZLCloudPluginStateManager;
class UMeshComponent;
class UMaterialInterface;
class UCameraComponent;


namespace ZLCloudPlugin
{
	enum ScreenshotType
	{
		DEFAULT2D,        //Standard single screenshot image
		EQUIRECT360,      //Returns equirectangular projection single image of cubemap faces
		//CUBEMAP360,     //Returns individual images for each face - not yet implemented, but might be in future
		//STEREOPANO360,  //Returns 2 equirectangular images for stereoscopic panoramas - not yet implemented, but might be in future
	};

	enum ZLScreenshotAAMode
	{
		None,
		TAA,
		TSR,
		DLSS,
		DLAA
	};

	class ZLScreenshotJob
	{
	public:
		ZLScreenshotJob() {};
		ZLScreenshotJob(int32 InWidth, int32 InHeight, FString& InFileName, FString& InPath, FString& InUID, UWorld* InWorld, const TSharedPtr<FJsonObject>* stateRequestData, ScreenshotType InScreenshotType = ScreenshotType::DEFAULT2D);

		void RevertSettings();
	public:

		int32 width, height;
		UWorld* world;
		FString format;
		FString path;
		FString uid;
		bool useMRQPipeline = false;
		bool debug = false;
		TArray<FString> cVarOverrides;
		TSharedPtr<FJsonObject> stateData = nullptr;
		/** Local image tool: do not fill schema-default `configuration` when the job did not set it. */
		bool bOmitImplicitConfiguration = false;
		bool stateRequestSent = false;
		bool transparent = false;
		double jobStartTime = 0.0f;
		double captureStartTime = 0.0f;
		//bool needsCameraPostprocessAdjustmentPerFace = false;
		ZLScreenshotAAMode aaMode = ZLScreenshotAAMode::TAA;
		int spatialSampleCount = 4;
		int temporalSampleCount = 4;
		/** Skips the MRQ cinematic quality game overrides that video jobs normally enable. */
		bool ignoreCinematicOverrides = false;
		/** Experimental: leaves streaming/LOD/shadow/VT game overrides at their runtime values to cut MRQ VRAM. */
		bool avoidLargeVramAllocations = false;
		/** When true, video jobs leave the per-frame stills on disk after ffmpeg muxes the video. */
		bool keepSourceFrames = false;
		/** Local video-tool mode: retained stills are PNG with straight alpha. */
		bool transparentSourceFrames = false;
		// Cubemap faces to capture for EQUIRECT360 (always 6 for the GPU stitch path).
		int32 totalFaceCount = 0;
		// Frustum margin for viewport cubemap faces (hides seams at face edges).
		float panoCubemapOverscan = 0.1f;
		// Cubemap GI warmup: unset (<0) => 4 frames/orientation, 0 => skip, >0 => that many frames.
		int32 panoCubemapWarmupFrames = -1;

#ifdef SUPPORT_LEGACY_MESSAGES
		bool isLegacyMessage = false;
#endif //SUPPORT_LEGACY_MESSAGES

		/** libZL websocket ImageCapture (message 55) — response goes via CloudStream2, not LauncherComms. */
		bool bStreamImageCapture = false;
		/** Environment to restore after transparent stream capture (websocket path). */
		FString streamRestoreEnvironment;
		bool bNeedsStreamEnvironmentRestore = false;

		FString videoFormat;
		FString videoSequenceName;
		FString videoCodec;
		FString videoPixelFormat;
		TArray<FString>videoEncodeOptions;
		double videoFrameRate = 30.0;
		int32 videoStartFrame = 0;
		int32 videoNumFrames = -1;
		TObjectPtr<ULevelSequence> videoCaptureSequenceAsset = nullptr;

		UMovieScene *sourceMovieScene = nullptr;
		FFrameRate sourceFrameRate;
		TRange<FFrameNumber> sourcePlaybackRange;


		ScreenshotType type = ScreenshotType::DEFAULT2D;
		int32 faceID = -1;
		int32 lastFaceCompletedID = -1;

		TSharedPtr<FJsonObject> postJobCurrentState = nullptr;
		TSharedPtr<FJsonObject> postJobTimeoutState = nullptr;
		TSharedPtr<FJsonObject> postJobUnmatchedState = nullptr;
		bool jobStateRequestFinished = false;
		bool jobStateRequestSuccess = false;
		bool jobCaptureStarted = false;
		bool environmentChangedInStateRequest = false;
		/** Viewport captures re-issued so far because the readback came back vertically combed. */
		int32 combRejectRetryCount = 0;
		/** Viewport captures re-issued so far to confirm whether heavy clipping is real content. */
		int32 blownOutRejectRetryCount = 0;
		/** Clipped fraction of the previous capture, or negative when there has not been one. */
		double lastSaturatedFraction = -1.0;

		TObjectPtr<UMoviePipelineExecutorJob> ActiveRenderJob;

		/** When transparent screenshot is used, backup of (component, material index, original material) so we can restore after capture */
		struct FTransparentGlassMaterialBackup
		{
			TWeakObjectPtr<UMeshComponent> Component;
			int32 MaterialIndex = 0;
			TObjectPtr<UMaterialInterface> OriginalMaterial = nullptr;
		};
		TArray<FTransparentGlassMaterialBackup> TransparentGlassMaterialBackups;

		/** Outer used for transparent-screenshot MIDs so they can be GC'd when the job is reset (avoids leak). */
		TObjectPtr<UObject> TransparentGlassMaterialSwapOuter = nullptr;
	};

	class FZLCubemapGpuStitcher;

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
		bool PerformMRQCapture(int width, int height, FString outputPath, FString jobName, FString& errorMsgOut);
	#ifdef SUPPORT_LEGACY_MESSAGES
		bool RequestScreenshot(const char* settingsJson, UWorld* InWorld, FString& errorMsgOut, bool isLegacyMessage);
	#else //SUPPORT_LEGACY_MESSAGES
		bool RequestScreenshot(const char* settingsJson, UWorld* InWorld, FString& errorMsgOut);
	#endif //SUPPORT_LEGACY_MESSAGES
		/** Queue a viewport capture for libZL cloudstream.captureImage(); uses the same render path as RequestScreenshot. */
		bool RequestStreamImageCapture(const FString& RequestId, int32 Width, int32 Height, const FString& Format, UWorld* InWorld, FString& ErrorMsgOut, const FString& OptionsJson = FString());
		void UpdateCurrentRenderStateRequestProgress(bool completed, bool matchSuccess);
		void SetCurrentRenderStateData(TSharedPtr<FJsonObject> currentState, TSharedPtr<FJsonObject> timeoutState = nullptr, TSharedPtr<FJsonObject> unmatchedState = nullptr);
		inline bool HasCurrentRender() { return m_CurrentRender.IsValid(); }
		inline bool HasNextRender() { return m_NextRender.IsValid(); }
		void Set2DODMode(bool is2DOD);
		bool LoadImageAsFColorArray(const FString& ImagePath, TArray<FColor>& OutColorData, int32& OutWidth, int32& OutHeight);
		void ApplyCVarOverridesDirect();

#if UNREAL_5_3_OR_NEWER
		void OnMoviePipelineFinished(FMoviePipelineOutputData Results);
#endif

	private:
		void OnScreenshotComplete(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData);

		/** Visits all cubemap orientations (incl. up/down) at capture FOV before face 0. */
		bool TickCubemapPanoSweep();

		bool TickEnvironmentChangeSettle();

		void ApplyCubemapPanoView(const FRotator& Rot, float FovDegrees, bool bCameraCut);

		void SetCubemapPanoRotation(const FRotator& Rot);

		/** Copies the live view's post-process onto the pano capture camera and neutralizes
		 *  local exposure so it can't add per-face tonemap seams. Exposure itself is inherited
		 *  as-is; lock it (Manual or Min==Max EV) on the camera/level for a seamless pano. */
		void ApplyFixedPanoExposure(class UCameraComponent* Camera);

		void CleanupCubemapGpuStitcher();
		/** Builds protocol metadata and sends an already-encoded image for any capture method. */
		void SendCapturedImageResponse(const TArray64<uint8>& ImageBytes, const FIntPoint& ImageSize);
		/** Sends base64 JSON response for libZL websocket ImageCapture (message 55). */
		void SendStreamImageCaptureResponse(const TArray64<uint8>& ImageBytes);
		/** Restores the pre-capture environment after transparent websocket stream capture. */
		void RestoreStreamTransparentEnvironmentIfNeeded();
		/**
		 * Builds capture defaults + job JSON cVarOverrides, then applies once (SetByCode).
		 * No-op if already applied for this job; safe to call from pano setup and from ApplyCVarOverridesDirect.
		 */
		void ApplyMergedCaptureCVars();
		/** Restores every CVar touched by ApplyMergedCaptureCVars. Safe after m_CurrentRender is reset. */
		void RestoreJobCVarOverrides();

		void SendImageFailureResponse(FString& errorMsg);

		/** Clears the active render job and resets content generation state to None. */
		void ClearCurrentRenderJob();

		/** Clears local-editor 2D on-demand capture mode after a screenshot/video job ends (WITH_EDITOR only). */
		void EditorRestoreLocalCapture2DODModeAfterJob();

		void PauseGameTime();
		void ResumeGameTime();

		/** Applies merged capture CVars early so pano warmup uses the pano-specific pack. */
		void BeginPanoCaptureOverrides();

		void FinalizePanoViewRestore();

		/** Cubemap high-res face capture size (overscan size if set, else central face size). */
		int32 GetCubemapCaptureSize() const { return static_cast<int32>(m_faceCaptureSize > 0 ? m_faceCaptureSize : m_faceSize); }

		/** Writes already-encoded image bytes to m_CurrentRender->path. No-op if the job has no output path. */
		void SaveImageBytesToDisk(const TArray64<uint8>& ImageBytes, const TCHAR* LogContext);

		bool s_rejectUnmatchedStateJobs = false;

		float m_cacheTimeDilation = 1.0f;
		TWeakObjectPtr<UWorld> m_timeDilatedWorld;
		bool m_gameTimePaused = false;

		ACameraActor* CameraActor = nullptr;
		bool bCreatedCamera = false;

		TSharedPtr<ZLScreenshotJob> m_NextRender;
		TSharedPtr<ZLScreenshotJob> m_CurrentRender;

		LauncherComms* m_LauncherComms;

		bool m_Initialized;

		FDelegateHandle m_onScreenshotCapturedDelegateHandle;

		static constexpr int s_hiresScreenshotFrameCount = 8;
		/** Default HighResScreenshotDelay when r.RayTracing is on (job JSON may override). */
		static constexpr int s_hiresScreenshotFrameCountRaytracing = 16;

		/**
		 * Frames of DLSS accumulation to discard when a job asks for resetDLSSHistory.
		 * Covers the engine tick that starts the pipeline plus MRQ's warm-up render, so the live
		 * viewport's history cannot bleed into the first captured frames. Keep this low: while the
		 * reset is asserted DLSS never accumulates, so a long window just yields an aliased image.
		 */
		static constexpr int s_dlssHistoryResetFrames = 3;

		/** Re-captures allowed after a vertical-comb rejection before the image is sent regardless. */
		static constexpr int s_maxCombRejectRetries = 2;

		/** Re-captures allowed to confirm heavy clipping before the image is sent regardless. */
		static constexpr int s_maxBlownOutRejectRetries = 2;

		static constexpr int s_panoCubemapSweepFramesPerOrientation = 4;

		// Null unless a pano job is mid-flight; FinalizePanoViewRestore() uses these as its
		// only gate, since it also runs from failure paths after m_CurrentRender is reset.
		APlayerController* m_playerController = nullptr;
		AActor* m_initialViewTarget = nullptr;
		ACameraActor* m_panoViewTarget = nullptr;

		FQuat m_startRotation;
		FQuat m_faceRotation[6];
		// Rig-relative orientation that actually produced each captured cubemap face,
		// recorded at screenshot-issue time (TakeHighResScreenShot is deferred, so the
		// rendered frame reflects the camera orientation from the issuing tick).
		FQuat m_panoCubemapFaceRigRot[6];

		uint32_t m_faceSize = 0;
		// Cubemap: high-res screenshot resolution (includes overscan margin). 0 => use m_faceSize.
		uint32_t m_faceCaptureSize = 0;
		// Frustum margin for cubemap faces.
		float m_panoCubemapOverscan = 0.f;

		/** True after ApplyMergedCaptureCVars for the current job (defaults + JSON). */
		bool m_captureCVarsApplied = false;
		struct FCaptureCVarState
		{
			FString Value;
			uint32 SetByFlags = 0;
		};
		/**
		 * Pre-job CVar values and priorities for everything ApplyMergedCaptureCVars touched.
		 * Stored on ZLScreenshot (not the job) so failure paths can restore after Reset.
		 */
		TMap<FString, FCaptureCVarState> m_jobSavedCVarStates;

		double m_panoFaceRequestTimeSeconds = 0.0;

		// Cubemap capture: GPU stitcher (ZLCubeToEquirect) fed from TakeHighResScreenShot face captures.
		TUniquePtr<FZLCubemapGpuStitcher> m_panoCubemapGpuStitcher;
		bool m_panoFaceZeroSetupDone = false;
		bool m_panoCubemapWarmupComplete = false;
		bool m_panoCubemapSweepActive = false;
		int32 m_panoCubemapSweepStepIndex = 0;
		int32 m_panoCubemapSweepFramesRemaining = 0;
		float m_panoCubemapCaptureFovDegrees = 90.f;

		bool m_environmentSettleComplete = false;
		bool m_environmentSettleActive = false;
		int32 m_environmentSettleFramesRemaining = 0;

		struct FPanoTiming
		{
			double CaptureWallStartSeconds = 0.0;
			double CaptureWallEndSeconds = 0.0;
			double FaceRequestToCallbackAccumSeconds = 0.0;
			double FinalEncodeSeconds = 0.0;
			double CompositeReadbackSeconds = 0.0;
			int32 AppliedScreenshotDelay = 0;
			int32 AppliedAntiAliasingMethod = -1;
			bool bAppliedDLSSEnabled = false;
			int32 CompletedFaceCount = 0;
			bool bCaptureWallStarted = false;

			void Reset()
			{
				CaptureWallStartSeconds = 0.0;
				CaptureWallEndSeconds = 0.0;
				FaceRequestToCallbackAccumSeconds = 0.0;
				FinalEncodeSeconds = 0.0;
				CompositeReadbackSeconds = 0.0;
				AppliedScreenshotDelay = 0;
				AppliedAntiAliasingMethod = -1;
				bAppliedDLSSEnabled = false;
				CompletedFaceCount = 0;
				bCaptureWallStarted = false;
			}

			double CaptureWallSeconds() const
			{
				return bCaptureWallStarted ? FMath::Max(0.0, CaptureWallEndSeconds - CaptureWallStartSeconds) : 0.0;
			}
		};

		FPanoTiming m_panoTiming;

		void LogPanoCubemapTimingSummary() const;
		void LogDefault2DViewportTimingSummary(int32 Width, int32 Height, double CaptureWallSeconds, double FinalEncodeSeconds) const;
		void LogDefault2DMRQTimingSummary(int32 Width, int32 Height, bool bVideoCapture, const FString& VideoFormat, double CaptureWallSeconds, double FinalEncodeSeconds) const;

		UZLCloudPluginDelegates* Delegates = nullptr;
	};
}
