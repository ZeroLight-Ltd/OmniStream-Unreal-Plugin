// Copyright ZeroLight ltd. All Rights Reserved.
#include "ZLScreenshot.h"
#include "LauncherComms.h"
#include "ZLCloudPluginPrivate.h"
#include "ZLJobTrace.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "ZLCubemapPanoCapture.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/IConsoleManager.h"
#include "RenderingThread.h"
#include "ContentStreaming.h"
#include "Engine/World.h"

#if UNREAL_5_3_OR_NEWER
#include "HighResScreenshot.h"
#include "ImageUtils.h"
#else
#include "Engine/Public/HighResScreenshot.h"
#include "Engine/Public/ImageUtils.h"
#endif

#include "Widgets/SViewport.h"
#include <EditorZLCloudPluginSettings.h>
#include "Engine/GameInstance.h"
#include "MovieScene.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#if UNREAL_5_3_OR_NEWER
#include "MoviePipelineQueue.h"
#include "MoviePipeline.h"
#include "MoviePipelineQueueEngineSubsystem.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineBlueprintLibrary.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelinePrimaryConfig.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineDeferredPasses.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "MoviePipelineConsoleVariableSetting.h"
#if WITH_DLSS
#include "MoviePipelineDLSSSetting.h"
#endif
#endif
#include "ZLCloudPluginStateManager.h"
#include "ZLCloudPluginModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"

#include <AssetRegistry/AssetRegistryModule.h>

#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "ZLTransientMaterialSwapOuter.h"

using namespace ZLCloudPlugin;

namespace
{
	static const FName ZLGlassTag(TEXT("ZLGlass"));
	static const TCHAR* ZLGlassShaderPath = TEXT("/ZLEditorTools/ZLGlassShader.ZLGlassShader");
	static const TCHAR* ZLGlassShaderAlphaScreenshotPath = TEXT("/ZLEditorTools/ZLGlassShader_AlphaScreenshot.ZLGlassShader_AlphaScreenshot");
	static constexpr double ZLMRQProgressSendIntervalSeconds = 5.0;

	struct FMRQVideoProgressState
	{
		bool bActive = false;
		FString OutputDirectory;
		FString JobUID;
		FString FrameExtension;
		int32 TotalFrames = 0;
		double LastSendTimeSeconds = 0.0;
		int32 LastPercentSent = -1;
	};

	FMRQVideoProgressState GMRQVideoProgressState;

	void ResetMRQVideoProgressState()
	{
		GMRQVideoProgressState = FMRQVideoProgressState();
	}

	void BeginMRQVideoProgressState(const FString& InOutputDirectory, const FString& InJobUID, const FString& InFrameExtension, int32 InTotalFrames)
	{
		ResetMRQVideoProgressState();
		GMRQVideoProgressState.bActive = InTotalFrames > 0 && !InOutputDirectory.IsEmpty() && !InJobUID.IsEmpty() && !InFrameExtension.IsEmpty();
		GMRQVideoProgressState.OutputDirectory = InOutputDirectory;
		GMRQVideoProgressState.JobUID = InJobUID;
		GMRQVideoProgressState.FrameExtension = InFrameExtension;
		GMRQVideoProgressState.TotalFrames = InTotalFrames;
		GMRQVideoProgressState.LastSendTimeSeconds = FPlatformTime::Seconds();
	}

	int32 CountCompletedMRQFrames()
	{
		if (!GMRQVideoProgressState.bActive)
		{
			return 0;
		}

		FString SearchExtension = GMRQVideoProgressState.FrameExtension;

		// If the extension is jpg or jpeg, use a wildcard to catch both variants
		if (SearchExtension.Equals(TEXT("jpg"), ESearchCase::IgnoreCase) ||
			SearchExtension.Equals(TEXT("jpeg"), ESearchCase::IgnoreCase))
		{
			SearchExtension = TEXT("jp*g");
		}

		TArray<FString> FoundFiles;
		const FString WildcardFilePath = FPaths::Combine(
			GMRQVideoProgressState.OutputDirectory,
			GMRQVideoProgressState.JobUID + TEXT(".*.") + SearchExtension);
		IFileManager::Get().FindFiles(FoundFiles, *WildcardFilePath, true, false);
		return FoundFiles.Num();
	}

	int32 CalculateMRQProgressPercent(int32 CompletedFrames)
	{
		if (!GMRQVideoProgressState.bActive || GMRQVideoProgressState.TotalFrames <= 0)
		{
			return 0;
		}

		const float ProgressRatio = (static_cast<float>(CompletedFrames) * 100.0f) / static_cast<float>(GMRQVideoProgressState.TotalFrames);
		return FMath::Clamp(FMath::FloorToInt(ProgressRatio), 0, 99);
	}

	bool IsMaterialChildOfZLGlassShader(UMaterialInterface* Mat)
	{
		if (!Mat) return false;
		if (Mat->GetPathName().Equals(ZLGlassShaderPath, ESearchCase::IgnoreCase)) return true;
		if (UMaterialInstance* MI = Cast<UMaterialInstance>(Mat))
		{
			if (UMaterialInterface* Parent = MI->Parent)
				return IsMaterialChildOfZLGlassShader(Parent);
		}
		return false;
	}

	void SwapTransparentGlassMaterialsForAlphaScreenshot(ZLScreenshotJob* Job)
	{
		if (!Job || !Job->world || !Job->transparent) return;

		UMaterialInterface* AlphaScreenshotParent = LoadObject<UMaterialInterface>(nullptr, ZLGlassShaderAlphaScreenshotPath);
		if (!AlphaScreenshotParent)
		{
			UE_LOG(LogZLCloudPlugin, Warning, TEXT("Transparent screenshot: Could not load ZLGlassShader_AlphaScreenshot at %s"), ZLGlassShaderAlphaScreenshotPath);
			return;
		}

		Job->TransparentGlassMaterialBackups.Empty();
		// Use a concrete transient Outer for all MIDs so they become unreachable when the job is reset and can be GC'd (UObject is abstract)
		Job->TransparentGlassMaterialSwapOuter = NewObject<UZLTransientMaterialSwapOuter>(ZL_TRANSIENT_PACKAGE_OUTER, NAME_None, RF_Transient);

		for (TActorIterator<AActor> It(Job->world); It; ++It)
		{
			TArray<UStaticMeshComponent*> Components;
			It->GetComponents<UStaticMeshComponent>(Components);
			for (UStaticMeshComponent* SMComp : Components)
			{
				if (!SMComp || !SMComp->ComponentTags.Contains(ZLGlassTag)) continue;

				UStaticMesh* StaticMesh = SMComp->GetStaticMesh();
				if (!StaticMesh) continue;

				const TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();
				for (int32 Idx = 0; Idx < StaticMaterials.Num(); ++Idx)
				{
					UMaterialInterface* CurrentMat = SMComp->GetMaterial(Idx);
					if (!CurrentMat || !IsMaterialChildOfZLGlassShader(CurrentMat)) continue;

					UMaterialInstanceDynamic* DynMID = UMaterialInstanceDynamic::Create(AlphaScreenshotParent, Job->TransparentGlassMaterialSwapOuter);
					if (!DynMID) continue;

					if (UMaterialInstance* SourceMI = Cast<UMaterialInstance>(CurrentMat))
					{
						DynMID->CopyParameterOverrides(SourceMI);
						// Copy any base property overrides from the instance
						DynMID->BasePropertyOverrides = SourceMI->BasePropertyOverrides;
					}

					// Explicitly force the TwoSided state to match the true evaluated state of the original material
					DynMID->BasePropertyOverrides.bOverride_TwoSided = true;
					DynMID->BasePropertyOverrides.TwoSided = CurrentMat->IsTwoSided();

					Job->TransparentGlassMaterialBackups.Add({ SMComp, Idx, CurrentMat });
					SMComp->SetMaterial(Idx, DynMID);
				}
			}
		}

		UE_LOG(LogZLCloudPlugin, Display, TEXT("Transparent screenshot: Swapped %d glass material slot(s) to AlphaScreenshot variant."), Job->TransparentGlassMaterialBackups.Num());
	}

	void RestoreTransparentGlassMaterials(ZLScreenshotJob* Job)
	{
		if (!Job) return;
		for (const ZLScreenshotJob::FTransparentGlassMaterialBackup& Backup : Job->TransparentGlassMaterialBackups)
		{
			if (UMeshComponent* Comp = Backup.Component.Get())
				Comp->SetMaterial(Backup.MaterialIndex, Backup.OriginalMaterial);
		}
		Job->TransparentGlassMaterialBackups.Empty();
		// Release the transient Outer so the MIDs are no longer referenced and can be GC'd
		Job->TransparentGlassMaterialSwapOuter = nullptr;
		UE_LOG(LogZLCloudPlugin, Display, TEXT("Transparent screenshot: Restored original materials."));
	}

	// PNG has sRGB RGB, linear alpha; glass is premultiplied. 
	// Convert to straight alpha for web browsers (RGB*A + bg*(1-A)).
	// Bright reflections (>255) are clamped in 8 bit we fake recovery by boosting the alpha channel.
	void UnpremultiplyAlphaPixel(FColor& Pixel)
	{
		const uint8 OrigA = Pixel.A;

		// Skip fully opaque pixels
		if (OrigA == 255)
			return;

		FLinearColor LinColor = FLinearColor::FromSRGBColor(Pixel);
		float AlphaLin = OrigA / 255.0f;

		// Alpha Boosting
		// Find the brightest linear color channel
		float MaxColor = FMath::Max3(LinColor.R, LinColor.G, LinColor.B);

		// Use the max color as the alpha to fix issues where the color is already bright, but the alpha is dark.
		// Which causes the color to be larger than 255, but clamped. 
		// Due to the clamping the alpha will then overcompensate in the browser and darken too much causing a cutout effect around windows.
		if (MaxColor > AlphaLin)
		{
			AlphaLin = MaxColor;
		}

		// Unpremultiply safely divide by the new boosted alpha
		if (AlphaLin >= 1.0f / 255.0f)
		{
			LinColor.R /= AlphaLin;
			LinColor.G /= AlphaLin;
			LinColor.B /= AlphaLin;
		}

		// Convert back to 8-bit sRGB space
		Pixel = LinColor.ToFColor(true);

		// Save the new boosted alpha
		Pixel.A = (uint8)FMath::Clamp(FMath::RoundToInt(AlphaLin * 255.0f), 0, 255);
	}

	bool UsesCubemapPanoCapture(const ZLScreenshotJob* Job)
	{
		return Job
			&& Job->type == ScreenshotType::EQUIRECT360
			&& !Job->useMRQPipeline;
	}

	FString ResolveMRQOutputImagePath(const FString& OutputDir, const FString& JobUID, const FString& Extension)
	{
		FString NormalizedExt = Extension.ToLower();
		if (NormalizedExt == TEXT("jpeg"))
		{
			NormalizedExt = TEXT("jpg");
		}

		const FString ExactPath = FPaths::Combine(OutputDir, JobUID + TEXT(".") + NormalizedExt);
		if (FPaths::FileExists(ExactPath))
		{
			return ExactPath;
		}

		FString SearchExt = NormalizedExt;
		if (SearchExt == TEXT("jpg"))
		{
			SearchExt = TEXT("jp*g");
		}

		const FString WildcardPath = FPaths::Combine(OutputDir, JobUID + TEXT(".*.") + SearchExt);
		TArray<FString> FoundFiles;
		IFileManager::Get().FindFiles(FoundFiles, *WildcardPath, true, false);
		if (FoundFiles.Num() == 0)
		{
			return ExactPath;
		}

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		FString NewestFile = FoundFiles[0];
		FDateTime NewestTime = FDateTime::MinValue();
		for (const FString& FileName : FoundFiles)
		{
			const FString FullPath = FPaths::Combine(OutputDir, FileName);
			const FDateTime FileTime = PlatformFile.GetTimeStamp(*FullPath);
			if (FileTime >= NewestTime)
			{
				NewestTime = FileTime;
				NewestFile = FileName;
			}
		}

		return FPaths::Combine(OutputDir, NewestFile);
	}

	void DeleteMRQOutputImages(const FString& OutputDir, const FString& JobUID, const FString& Extension)
	{
		FString SearchExt = Extension.ToLower();
		if (SearchExt == TEXT("jpeg"))
		{
			SearchExt = TEXT("jpg");
		}
		if (SearchExt == TEXT("jpg"))
		{
			SearchExt = TEXT("jp*g");
		}

		const FString WildcardPath = FPaths::Combine(OutputDir, JobUID + TEXT(".*.") + SearchExt);
		TArray<FString> FoundFiles;
		IFileManager::Get().FindFiles(FoundFiles, *WildcardPath, true, false);
		for (const FString& FileName : FoundFiles)
		{
			IFileManager::Get().Delete(*FPaths::Combine(OutputDir, FileName), true);
		}
	}

	float GetCubemapOverscanFOVDegrees(float Overscan)
	{
		const float OverscanTan = 1.f + FMath::Clamp(Overscan, 0.f, 1.f);
		return FMath::RadiansToDegrees(2.f * FMath::Atan(OverscanTan));
	}

	bool StateRequestChangesEnvironment(const TSharedPtr<FJsonObject>& RequestState, FString& OutPreviousEnvironment, FString& OutRequestedEnvironment)
	{
		OutPreviousEnvironment.Reset();
		OutRequestedEnvironment.Reset();

		if (!RequestState.IsValid())
		{
			return false;
		}

		if (!RequestState->TryGetStringField(TEXT("environment"), OutRequestedEnvironment) || OutRequestedEnvironment.IsEmpty())
		{
			return false;
		}

		UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
		TSharedPtr<FJsonObject> CurrentState = StateManager ? StateManager->GetCurrentAppState() : nullptr;
		if (!CurrentState.IsValid() || !CurrentState->TryGetStringField(TEXT("environment"), OutPreviousEnvironment))
		{
			return true;
		}

		return !OutPreviousEnvironment.Equals(OutRequestedEnvironment, ESearchCase::IgnoreCase);
	}
}

ZLScreenshotJob::ZLScreenshotJob(int32 InWidth, int32 InHeight, FString& InFormat, FString& InPath, FString& InUID, UWorld* InWorld, const TSharedPtr<FJsonObject>* stateRequestData, ScreenshotType InScreenshotType)
	: width(InWidth), height(InHeight), world(InWorld), format(InFormat), path(InPath), uid(InUID), type(InScreenshotType)
{
	if (stateRequestData != nullptr)
	{
		stateData = MakeShared<FJsonObject>();
		FJsonObject::Duplicate(*stateRequestData, stateData);
	}
}

void ZLScreenshotJob::RevertSettings()
{
	if (sourceMovieScene != nullptr)
	{
		sourceMovieScene->SetPlaybackRange(sourcePlaybackRange);
		sourceMovieScene->SetDisplayRate(sourceFrameRate);
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("MRQ SequenceState [RevertSettings:Skipped] No sourceMovieScene cached for restore."));
	}
}

TSharedPtr<ZLScreenshot> ZLScreenshot::Singleton = nullptr;

TSharedPtr<ZLScreenshot> ZLScreenshot::CreateInstance()
{
	if (Singleton == nullptr)
	{
		Singleton = MakeShared<ZLScreenshot>();
		return Singleton;
	}
	return Singleton;
}

ZLScreenshot::ZLScreenshot()
{
	m_Initialized = false;

	m_faceRotation[0] = FQuat(FRotator(0, 180, 0).Quaternion());	// Back-face
	m_faceRotation[1] = FQuat(FRotator(270, 270, 0).Quaternion());	// Down-face
	m_faceRotation[2] = FQuat(FRotator(90, 270, 0).Quaternion());	// Up-face
	m_faceRotation[3] = FQuat(FRotator(0, 270, 0).Quaternion());	// Left-face
	m_faceRotation[4] = FQuat(FRotator(0, 90, 0).Quaternion());		// Right-face
	m_faceRotation[5] = FQuat(FRotator(0, 0, 0).Quaternion());		// Front-face
}

ZLScreenshot::~ZLScreenshot()
{
	m_Initialized = false;
}

void ZLScreenshot::Init(class LauncherComms* launcherComms)
{
	if (m_Initialized) return;

	m_LauncherComms = launcherComms;
	m_Initialized = true;

	Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates();

	if (!Delegates)
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Error getting ZLCloudPlugin delegates."));
}

void ZLScreenshot::Update()
{
	if (!m_CurrentRender.IsValid() && GMRQVideoProgressState.bActive)
	{
		ResetMRQVideoProgressState();
	}

	if (m_CurrentRender.IsValid() && m_CurrentRender->useMRQPipeline && !m_CurrentRender->videoFormat.IsEmpty() && GMRQVideoProgressState.bActive)
	{
		const double currentTimeSeconds = FPlatformTime::Seconds();
		if ((currentTimeSeconds - GMRQVideoProgressState.LastSendTimeSeconds) >= ZLMRQProgressSendIntervalSeconds)
		{
			const int32 completedFrames = CountCompletedMRQFrames();
			const int32 progressPercent = CalculateMRQProgressPercent(completedFrames);
			if (progressPercent != GMRQVideoProgressState.LastPercentSent)
			{
				const FString progress = FString::FromInt(progressPercent);
				m_LauncherComms->SendLauncherMessage("CAPTUREMEDIAPROGRESS", progress);
				GMRQVideoProgressState.LastPercentSent = progressPercent;
			}

			GMRQVideoProgressState.LastSendTimeSeconds = currentTimeSeconds;
		}
	}

	//Check if we have a request
	if (m_NextRender.IsValid() || m_CurrentRender.IsValid())
	{
		bool captureScreenshot = false;
		//Check if we arent still rendering a request
		if (!m_CurrentRender.IsValid())
		{
			m_CurrentRender = m_NextRender;
			m_NextRender.Reset();// Mark that we can queue up the next render, won't process until m_CurrentRender is done.

			m_CurrentRender->jobStartTime = FApp::GetCurrentTime();

			m_environmentSettleComplete = false;
			m_environmentSettleActive = false;
			m_environmentSettleFramesRemaining = 0;

			//If the screenshot needs to do state request, we need to wait till state is matching the request to complete the job
			if (m_CurrentRender->stateData != nullptr)
			{
				int32 MergedKeyCount = 0;
				UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
				UStateKeyInfoAsset* SchemaAsset = StateManager ? StateManager->GetCurrentSchemaAsset() : nullptr;
				TSharedPtr<FJsonObject> stateDataToBroadcast = MergeRequestStateWithSchemaDefaults(
					m_CurrentRender->stateData, SchemaAsset, MergedKeyCount);

				if (MergedKeyCount > 0)
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Screenshot state request: %d key(s) missing from request have been merged from schema defaults (stale state avoided)."), MergedKeyCount);
				}

				FString previousEnvironment;
				FString requestedEnvironment;
				m_CurrentRender->environmentChangedInStateRequest = StateRequestChangesEnvironment(stateDataToBroadcast, previousEnvironment, requestedEnvironment);
				if (m_CurrentRender->environmentChangedInStateRequest)
				{
					UE_LOG(LogZLCloudPlugin, Display,
						TEXT("Screenshot state request switches environment '%s' -> '%s'"),
						previousEnvironment.IsEmpty() ? TEXT("<unset>") : *previousEnvironment,
						*requestedEnvironment);
				}

				FString stateDataStr;
				TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&stateDataStr);
				if (!stateDataToBroadcast.IsValid() || !FJsonSerializer::Serialize(stateDataToBroadcast.ToSharedRef(), writer))
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Error broadcasting OnReceiveData for state request"));
				}

				FString stateDataStrPretty;
				TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> writerPretty = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&stateDataStrPretty);
				if (stateDataToBroadcast.IsValid() && FJsonSerializer::Serialize(stateDataToBroadcast.ToSharedRef(), writerPretty))
				{
					writerPretty->Close();
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Screenshot state request (requested + merged defaults):\n%s"), *stateDataStrPretty);
				}

				if (Delegates)
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("OnReceiveData Screenshot Request"));

					ZLJobTrace::JOBTRACE_TIMER_START("StateProcessing");
					Delegates->OnRecieveData.Broadcast(stateDataStr);
					m_CurrentRender->stateRequestSent = true;
				}

				if (!m_CurrentRender->stateRequestSent)
				{
					//Nothing will ever confirm or time out the request, so don't gate the capture on a result that cannot arrive
					UE_LOG(LogZLCloudPlugin, Error, TEXT("Screenshot state request could not be submitted, capturing without a state match"));
					m_CurrentRender->jobStateRequestFinished = true;
					m_CurrentRender->jobStateRequestSuccess = false;
				}
			}
			else
			{
				//No state request to wait on, so the capture gate is satisfied for the lifetime of the job
				m_CurrentRender->jobStateRequestFinished = true;
				m_CurrentRender->jobStateRequestSuccess = true;
				captureScreenshot = true;
			}
		}
		else
		{
			if (m_CurrentRender->jobStateRequestFinished && m_CurrentRender->stateRequestSent)
			{
				ZLJobTrace::JOBTRACE_TIMER_END("StateProcessing");
			}

			if (m_CurrentRender->jobStateRequestFinished && m_CurrentRender->jobStateRequestSuccess) //State matches current render state data
			{
				captureScreenshot = true;
			}
			else
			{
				if (m_CurrentRender->jobStateRequestFinished) //If state request finished but was not fully successful need to pass on rejection here and inform IM of what failed
				{
					if (s_rejectUnmatchedStateJobs)
					{
						FString errorMsg = "Timeout reached waiting for state data to match request.";

						ZLScreenshot::SendImageFailureResponse(errorMsg);

						ClearCurrentRenderJob();
						EditorRestoreLocalCapture2DODModeAfterJob();
						ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
						return;
					}
					else //Continue capture but just add unmatched state data to end response
					{
						captureScreenshot = true;
					}
				}
			}
		}

		// Every capture type waits for the state request to complete or time out. Pano relies on
		// re-entering this block each tick to advance its faces, which only sets jobCaptureStarted
		// on the 2D path, so the same gate serves both.
		if (captureScreenshot && !m_CurrentRender->jobCaptureStarted)
		{
			if (!TickEnvironmentChangeSettle())
			{
				return;
			}

			if (UGameViewportClient* GameViewport = m_CurrentRender->world->GetGameViewport())
			{
				FViewport* Viewport = GameViewport->Viewport;
				if (Viewport != nullptr)
				{
					UZLCloudPluginDelegates* PluginDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates();

					FString ProjectSavedDir = FPaths::ProjectSavedDir();
					FString ContentGenOutputPath = FPaths::Combine(ProjectSavedDir, TEXT("ContentGeneration"));

					if (m_CurrentRender->type == ScreenshotType::DEFAULT2D || (m_CurrentRender->type == ScreenshotType::EQUIRECT360 && m_CurrentRender->faceID == 0))
					{
						if (PluginDelegates)
						{
							PluginDelegates->OnContentGenerationBegin.Broadcast(m_CurrentRender->type == ScreenshotType::EQUIRECT360);
							PluginDelegates->OnContentGenerationBeginNative.Broadcast(m_CurrentRender->type == ScreenshotType::EQUIRECT360);
						}
					}

					if (m_CurrentRender->type == ScreenshotType::DEFAULT2D)
					{
						ZLJobTrace::JOBTRACE_TIMER_START("Render");
						m_CurrentRender->captureStartTime = FPlatformTime::Seconds();

						PauseGameTime();


						if (m_CurrentRender->useMRQPipeline)
						{
							if (m_CurrentRender->transparent)
								SwapTransparentGlassMaterialsForAlphaScreenshot(m_CurrentRender.Get());
							FString errorMsg;
							if (!PerformMRQCapture(m_CurrentRender->width, m_CurrentRender->height, ContentGenOutputPath, m_CurrentRender->uid, errorMsg))
							{
								ZLScreenshot::SendImageFailureResponse(errorMsg);
								ClearCurrentRenderJob();
								EditorRestoreLocalCapture2DODModeAfterJob();
								ResumeGameTime();
								return;
							}
						}
						else
						{
							ApplyCVarOverridesDirect();

							FHighResScreenshotConfig& HighResScreenshotConfig = GetHighResScreenshotConfig();
							HighResScreenshotConfig.SetResolution(m_CurrentRender->width, m_CurrentRender->height);

							if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
							{
								LocalDelegates->OnScreenshotRequestedNative.Broadcast(m_CurrentRender->width, m_CurrentRender->height, m_CurrentRender->world);
							}

							UE_LOG(LogZLCloudPlugin, Display, TEXT("TakeHighResScreenShot - Start Time %f - Start Frame %11d"), m_CurrentRender->captureStartTime, GFrameCounter);
							Viewport->TakeHighResScreenShot();
						}
						m_CurrentRender->jobCaptureStarted = true;

					}
					else if (m_CurrentRender->type == ScreenshotType::EQUIRECT360)
					{
						if (GEngine)
						{
							if (UWorld* World = m_CurrentRender->world)
							{
								if (!ZLJobTrace::JOBTRACE_IS_CURRENTLY_TIMING("Render"))
									ZLJobTrace::JOBTRACE_TIMER_START("Render");

								if (m_CurrentRender->faceID == -1)
								{
#if WITH_EDITOR
									if (UGameViewportClient* ViewportClient = World->GetGameViewport())
									{
										TSharedPtr<SWidget> ViewportWidget = ViewportClient->GetGameViewportWidget();

										if (ViewportWidget.IsValid())
										{
											FSlateApplication::Get().SetKeyboardFocus(ViewportWidget.ToSharedRef(), EFocusCause::SetDirectly);
										}
									}
#endif
									m_playerController = World->GetFirstPlayerController();

									FVector startLoc(0, 0, 0);
									FRotator startRot(0, 0, 0);
									m_playerController->GetPlayerViewPoint(startLoc, startRot);
									m_initialViewTarget = m_playerController->GetViewTarget();

									// Cubemap equirect is yaw-centered; spring-arm pitch/roll must not tilt the cube basis.
									const FRotator panoSpawnRot(0.f, startRot.Yaw, 0.f);

									BeginPanoCaptureOverrides();

									m_startRotation = panoSpawnRot.Quaternion();
									m_panoFaceZeroSetupDone = false;
									m_panoCubemapWarmupComplete = false;
									m_panoCubemapSweepActive = false;
									m_panoCubemapSweepStepIndex = 0;
									m_panoCubemapSweepFramesRemaining = 0;
									m_faceCaptureSize = 0;
									m_panoCubemapOverscan = 0.f;
									CleanupCubemapGpuStitcher();

									m_panoCubemapOverscan = FMath::Clamp(m_CurrentRender->panoCubemapOverscan, 0.f, 1.f);
									const int32 CentralFaceSize = 8 * static_cast<int32>(floor(2.0 * static_cast<double>(m_CurrentRender->height) / PI / 8.0));
									m_faceSize = static_cast<uint32>(CentralFaceSize);
									m_faceCaptureSize = (m_panoCubemapOverscan > KINDA_SMALL_NUMBER)
										? static_cast<uint32>(FMath::Max(8, FMath::CeilToInt(static_cast<float>(CentralFaceSize) * (1.f + m_panoCubemapOverscan))))
										: m_faceSize;
									m_CurrentRender->totalFaceCount = 6;

									m_panoTiming.Reset();
									m_panoFaceRequestTimeSeconds = 0.0;
									m_panoTiming.CaptureWallStartSeconds = FPlatformTime::Seconds();
									m_panoTiming.bCaptureWallStarted = true;

									m_panoCubemapCaptureFovDegrees = GetCubemapOverscanFOVDegrees(m_panoCubemapOverscan);

									if (m_CurrentRender->panoCubemapWarmupFrames == 0)
									{
										UE_LOG(LogZLCloudPlugin, Display,
											TEXT("[Pano] cubemap: equirect=%dx%d faceCapture=%u centralFace=%u overscan=%.2f fov=%.1f warmup_sweep=disabled"),
											m_CurrentRender->width,
											m_CurrentRender->height,
											m_faceCaptureSize,
											m_faceSize,
											m_panoCubemapOverscan,
											m_panoCubemapCaptureFovDegrees);
									}
									else
									{
										const int32 EffectiveSweepFrames = (m_CurrentRender->panoCubemapWarmupFrames > 0)
											? m_CurrentRender->panoCubemapWarmupFrames
											: s_panoCubemapSweepFramesPerOrientation;

										UE_LOG(LogZLCloudPlugin, Display,
											TEXT("[Pano] cubemap: equirect=%dx%d faceCapture=%u centralFace=%u overscan=%.2f fov=%.1f warmup_sweep_frames=%d"),
											m_CurrentRender->width,
											m_CurrentRender->height,
											m_faceCaptureSize,
											m_faceSize,
											m_panoCubemapOverscan,
											m_panoCubemapCaptureFovDegrees,
											EffectiveSweepFrames);
									}

									m_panoViewTarget = World->SpawnActor<class ACameraActor>(startLoc, panoSpawnRot);

									FViewTargetTransitionParams TransitionParams;
									TransitionParams.BlendTime = 0.0f;

									UCameraComponent* cam = m_panoViewTarget->GetCameraComponent();
									cam->SetAspectRatio(1.0f);

									// Inherit the live view's post-process and lock exposure so every face
									// is exposed identically. Must run before SetViewTarget so we can still read
									// the outgoing active camera's post-process from the camera cache.
									ApplyFixedPanoExposure(cam);

									m_playerController->SetViewTarget(m_panoViewTarget, TransitionParams);
									if (m_playerController->PlayerCameraManager)
									{
										m_playerController->PlayerCameraManager->SetGameCameraCutThisFrame();
									}

								}

								if (UsesCubemapPanoCapture(m_CurrentRender.Get())
									&& m_CurrentRender->faceID == 0
									&& !m_panoCubemapWarmupComplete)
								{
									if (m_CurrentRender->panoCubemapWarmupFrames == 0)
									{
										m_panoCubemapWarmupComplete = true;
										UE_LOG(LogZLCloudPlugin, Display, TEXT("[Pano] cubemap warmup sweep skipped"));
									}
									else if (!TickCubemapPanoSweep())
									{
										return;
									}
									PauseGameTime();
									return;
								}

								if (m_CurrentRender->faceID == 0 && !m_panoFaceZeroSetupDone)
								{
									// Cubemap capture defers this block until after the orientation sweep so the
									// viewport stays at normal resolution while GI warms up.
									if (UsesCubemapPanoCapture(m_CurrentRender.Get()) && !m_panoCubemapWarmupComplete)
									{
										return;
									}

									FHighResScreenshotConfig& HighResScreenshotConfig = GetHighResScreenshotConfig();
									const int32 CubemapCaptureRes = GetCubemapCaptureSize();
									HighResScreenshotConfig.SetResolution(CubemapCaptureRes, CubemapCaptureRes);

									const FIntPoint res(CubemapCaptureRes, CubemapCaptureRes);
									GEngine->GameUserSettings->SetScreenResolution(res);

									APlayerCameraManager* PlayerCamera = m_playerController->PlayerCameraManager;

									if (m_panoCubemapCaptureFovDegrees <= KINDA_SMALL_NUMBER)
									{
										m_panoCubemapCaptureFovDegrees = GetCubemapOverscanFOVDegrees(m_panoCubemapOverscan);
									}
									PlayerCamera->SetFOV(m_panoCubemapCaptureFovDegrees);
									if (m_panoViewTarget != nullptr)
									{
										if (UCameraComponent* PanoCam = m_panoViewTarget->GetCameraComponent())
										{
											PanoCam->SetFieldOfView(m_panoCubemapCaptureFovDegrees);
										}
									}

									m_panoCubemapGpuStitcher = MakeUnique<FZLCubemapGpuStitcher>();
									if (!m_panoCubemapGpuStitcher->Begin(
										m_CurrentRender->width,
										GetCubemapCaptureSize(),
										m_panoCubemapOverscan))
									{
										FString errorMsg = TEXT("Failed to start cubemap GPU stitcher.");
										ZLScreenshot::SendImageFailureResponse(errorMsg);
										CleanupCubemapGpuStitcher();
										m_CurrentRender.Reset();
										EditorRestoreLocalCapture2DODModeAfterJob();
										return;
									}

									ApplyCVarOverridesDirect();

									m_panoFaceZeroSetupDone = true;
								}

								if (m_CurrentRender->faceID >= 0 && m_CurrentRender->faceID < m_CurrentRender->totalFaceCount)
								{
									if (m_CurrentRender->lastFaceCompletedID != m_CurrentRender->faceID - 1) //We are still waiting for last faceID to complete
										return;

									// Notify plugins to prepare/update their data before capturing the screenshot (only on first face)
									if (m_CurrentRender->faceID == 0)
									{
										if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
										{
											FZLPrepareScreenshotParams PrepareParams;
											PrepareParams.CaptureWidth = GetCubemapCaptureSize();
											PrepareParams.CaptureHeight = GetCubemapCaptureSize();
											LocalDelegates->OnPrepareScreenshotNative.Broadcast(PrepareParams);
										}
									}

									FString FormattedString = FString::Printf(TEXT("CaptureFaceID%d"), m_CurrentRender->faceID);
									ZLJobTrace::JOBTRACE_TIMER_START(FormattedString);

									const int32 FaceIndex = m_CurrentRender->faceID;

									if (FaceIndex == 0)
									{
										SetCubemapPanoRotation(m_startRotation.Rotator());

										if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
										{
											LocalDelegates->OnScreenshotRequestedNative.Broadcast(
												GetCubemapCaptureSize(),
												GetCubemapCaptureSize(),
												m_CurrentRender->world);
										}
									}

									m_CurrentRender->captureStartTime = FPlatformTime::Seconds();
									m_panoFaceRequestTimeSeconds = m_CurrentRender->captureStartTime;

									if (m_panoViewTarget && FaceIndex >= 0 && FaceIndex < 6)
									{
										const FQuat CurrentCamQuat = m_panoViewTarget->GetActorQuat();
										m_panoCubemapFaceRigRot[FaceIndex] = m_startRotation.Inverse() * CurrentCamQuat;
									}

									Viewport->TakeHighResScreenShot();
									m_CurrentRender->faceID++;
								}

								if (m_CurrentRender->faceID < 0)
									m_CurrentRender->faceID++;
							}
						}
					}
					else
					{
						UE_LOG(LogZLCloudPlugin, Error, TEXT("Invalid screenshot type"));
					}
				}
			}
		}
	}
}

bool ZLScreenshot::PerformMRQCapture(int width, int height, FString outputPath, FString jobName, FString& errorMsgOut)
{
	ResetMRQVideoProgressState();

	if (m_CurrentRender->useMRQPipeline)
	{
#if UNREAL_5_3_OR_NEWER
		UGameInstance* GameInstance = m_CurrentRender->world->GetGameInstance();
		if (!GameInstance)
		{
			// Handle error: No game instance
			return false;
		}
		UMoviePipelineQueueEngineSubsystem* Subsystem = GEngine->GetEngineSubsystem<UMoviePipelineQueueEngineSubsystem>();
		if (!Subsystem)
		{
			// Handle error: Subsystem not available
			return false;
		}

		FString fileNameFormat = jobName;
		ULevelSequence* LevelSequence = NULL;
		int32 renderedFrames = 1;
		int32 startFrame = 0;
		m_CurrentRender->videoCaptureSequenceAsset = nullptr;

		bool isVideoCapture = !m_CurrentRender->videoFormat.IsEmpty();
		if (isVideoCapture)
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray <FAssetData> AssetData;
			AssetRegistryModule.Get().GetAssetsByClass(FTopLevelAssetPath(ULevelSequence::StaticClass()->GetPathName()), AssetData);
			for (FAssetData assetDatum : AssetData)
			{
				if (m_CurrentRender->videoSequenceName.Equals(assetDatum.AssetName.ToString(), ESearchCase::IgnoreCase))
				{
					LevelSequence = Cast<ULevelSequence>(assetDatum.GetAsset());
					m_CurrentRender->videoCaptureSequenceAsset = LevelSequence;
					UE_LOG(LogZLCloudPlugin, Display, TEXT("MRQ Sequence lookup matched by name. Requested=%s ResolvedAsset=%s"),
						*m_CurrentRender->videoSequenceName,
						LevelSequence ? *LevelSequence->GetPathName() : TEXT("<null>"));
					startFrame = m_CurrentRender->videoStartFrame;
					renderedFrames = m_CurrentRender->videoNumFrames;	//use supplied limit, or default (-1) will play the whole sequence
					break;
				}
			}

			if (LevelSequence == NULL)
			{
				errorMsgOut = "Failed to find specified sequence";
				return false;
			}
		}

		// Create a new Level Sequence at runtime
		if (LevelSequence == NULL)
		{
			LevelSequence = NewObject<ULevelSequence>(ZL_TRANSIENT_PACKAGE_OUTER, NAME_None, RF_Transient);
			LevelSequence->Initialize(); // Important to initialize the sequence
			renderedFrames = 1;
		}

		// ffmpeg expects %04d starting at 0; offset must cancel the sequence's real first display frame
		int32 frameNumberOffset = -startFrame;

		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (MovieScene)
		{
			//if we change stuff on original sequence, will modify content (and prompt to save on exit)
			//so undo any changes when we leave with RevertSettings

			TRange<FFrameNumber> playbackRange = MovieScene->GetPlaybackRange();
			m_CurrentRender->sourcePlaybackRange = playbackRange;
			m_CurrentRender->sourceMovieScene = MovieScene;
			m_CurrentRender->sourceFrameRate = MovieScene->GetDisplayRate();
			//so we can change them back in RevertSettings()

			MovieScene->SetDisplayRate(FFrameRate((int32)(m_CurrentRender->videoFrameRate * 1000.0), 1000));	//i.e. accurate to 3 decimal places

			double ticksPerSecond = MovieScene->GetTickResolution().AsDecimal();
			double ticksPerFrame = ticksPerSecond / m_CurrentRender->videoFrameRate;
			if (renderedFrames > 0)
			{
				FFrameNumber renderedTicks = (int32)(renderedFrames * ticksPerFrame);
				FFrameNumber startTick = (int32)(startFrame * ticksPerFrame);
				MovieScene->SetPlaybackRange(startTick, renderedTicks.Value);
			}
			else
			{
				FFrameNumber startTick = playbackRange.GetLowerBoundValue();
				FFrameNumber lastFrameTicks = playbackRange.GetUpperBoundValue();
				FFrameNumber renderedTicks = lastFrameTicks - startTick;
				if (startFrame > 0)
				{
					startTick = (int32)(startFrame * ticksPerFrame);	//should this be offset from LowerBoundValue? assuming it'll pretty much always be zero
					renderedTicks = lastFrameTicks - startTick;
					MovieScene->SetPlaybackRange(startTick, renderedTicks.Value);
				}
				renderedFrames = (int32)((renderedTicks.Value + ticksPerFrame - 1) / ticksPerFrame);	//round up to include any frame we enter
			}

			if (isVideoCapture)
			{
				fileNameFormat += ".{frame_number}";

				// PlaybackRange lower bound is not enough: Commute is [0, ...) but first shot starts at tick 48000.
				// Offset must cancel the first frame MRQ will actually write (earliest shot/cut), so ffmpeg sees 0000.
				FFrameNumber effectiveStartTick = MovieScene->GetPlaybackRange().GetLowerBoundValue();
				bool bFoundSectionStart = false;
				FFrameNumber earliestSectionTick = effectiveStartTick;

				auto ConsiderSection = [&bFoundSectionStart, &earliestSectionTick](const UMovieSceneSection* Section)
				{
					if (!Section || !Section->IsActive())
					{
						return;
					}
					const TRange<FFrameNumber> SectionRange = Section->GetRange();
					if (!SectionRange.HasLowerBound() || SectionRange.GetLowerBound().IsOpen())
					{
						return;
					}
					const FFrameNumber SectionStart = SectionRange.GetLowerBoundValue();
					if (!bFoundSectionStart || SectionStart < earliestSectionTick)
					{
						earliestSectionTick = SectionStart;
						bFoundSectionStart = true;
					}
				};

				if (UMovieSceneCinematicShotTrack* ShotTrack = MovieScene->FindTrack<UMovieSceneCinematicShotTrack>())
				{
					for (UMovieSceneSection* Section : ShotTrack->GetAllSections())
					{
						ConsiderSection(Section);
					}
				}
				else if (UMovieSceneCameraCutTrack* CutTrack = MovieScene->FindTrack<UMovieSceneCameraCutTrack>())
				{
					for (UMovieSceneSection* Section : CutTrack->GetAllSections())
					{
						ConsiderSection(Section);
					}
				}

				if (bFoundSectionStart)
				{
					effectiveStartTick = earliestSectionTick;
				}

				const int32 outputStartFrame = (int32)FMath::RoundToDouble(effectiveStartTick.Value / ticksPerFrame);
				frameNumberOffset = -outputStartFrame;
				UE_LOG(LogZLCloudPlugin, Display, TEXT("MRQ FrameNumberOffset=%d (effective start tick=%d -> display frame=%d, usedShotOrCut=%s)"),
					frameNumberOffset, effectiveStartTick.Value, outputStartFrame, bFoundSectionStart ? TEXT("true") : TEXT("false"));
			}
		}

		UMoviePipelineExecutorJob* Job = Subsystem->AllocateJob(LevelSequence);
		if (!Job)
		{
			errorMsgOut = TEXT("Failed to allocate MRQ job.");
			if (m_CurrentRender.IsValid())
			{
				UE_LOG(LogZLCloudPlugin, Error, TEXT("MRQ job allocation failed. Restoring captured sequence settings immediately."));
				m_CurrentRender->RevertSettings();
			}
			return false;
		}

		Job->SetSequence(LevelSequence);

#if UNREAL_5_2_OR_NEWER
		UMoviePipelinePrimaryConfig* MasterConfig = NewObject<UMoviePipelinePrimaryConfig>(ZL_TRANSIENT_PACKAGE_OUTER);

		UMoviePipelineGameOverrideSetting* GameOverrides = Cast<UMoviePipelineGameOverrideSetting>( MasterConfig->FindOrAddSettingByClass(UMoviePipelineGameOverrideSetting::StaticClass()));
		if (GameOverrides)
		{
			const UZLCloudPluginSettings* PluginSettings = GetDefault<UZLCloudPluginSettings>();
			const bool bCinematicOverridesAllowed = PluginSettings == nullptr || PluginSettings->bAllowCinematicQualityOverridesInMediaOnDemandVideos;
			GameOverrides->bCinematicQualitySettings = bCinematicOverridesAllowed && isVideoCapture && !m_CurrentRender->ignoreCinematicOverrides;

			// Experimental VRAM probe: MRQ's Game Overrides default to disabling texture streaming, forcing
			// LOD 0, high quality shadows and full-res VT feedback, which is where the extra GPU memory goes.
			// Every value MRQ touches is captured and restored on pipeline teardown, so leaving them alone
			// here keeps the running game exactly as it was before the job.
			if (m_CurrentRender->avoidLargeVramAllocations)
			{
				GameOverrides->bCinematicQualitySettings = false;
				GameOverrides->TextureStreaming = EMoviePipelineTextureStreamingMethod::None;
				GameOverrides->bUseLODZero = false;
				GameOverrides->bDisableHLODs = false;
				GameOverrides->bFlushStreamingManagers = false;
				GameOverrides->bOverrideVirtualTextureFeedbackFactor = false;

				UE_LOG(LogZLCloudPlugin, Display, TEXT("MRQ job %s running with reduced VRAM game overrides"), *jobName);
			}
		}
#else
		UMoviePipelineMasterConfig* MasterConfig = NewObject<UMoviePipelineMasterConfig>(ZL_TRANSIENT_PACKAGE_OUTER);
#endif

		MasterConfig->FindOrAddSettingByClass(UMoviePipelineDeferredPassBase::StaticClass());

		// Set the output resolution
		UMoviePipelineOutputSetting* OutputSetting = MasterConfig->FindSetting<UMoviePipelineOutputSetting>();
		if (OutputSetting)
		{
			OutputSetting->OutputResolution = FIntPoint(width, height);
			if (m_CurrentRender->path != "")
				OutputSetting->OutputDirectory.Path = m_CurrentRender->path;
			else
			{
				if (!FPaths::DirectoryExists(outputPath))
				{
					IPlatformFile& platformFile = FPlatformFileManager::Get().GetPlatformFile();

					platformFile.CreateDirectory(*outputPath);
				}
				OutputSetting->OutputDirectory.Path = outputPath;
			}
			OutputSetting->FileNameFormat = fileNameFormat;
			OutputSetting->bOverrideExistingOutput = false;
			if (m_CurrentRender->bStreamImageCapture)
			{
				// Stream capture request IDs are small incrementing numbers; delete any prior
				// output with the same name so MRQ cannot succeed by reusing a stale PNG on disk.
				DeleteMRQOutputImages(OutputSetting->OutputDirectory.Path, fileNameFormat, m_CurrentRender->format);
				OutputSetting->bOverrideExistingOutput = true;
			}
			OutputSetting->ZeroPadFrameNumbers = 4;	//4 (default) gives 166 seconds @ 60fps, matches %04d used by ffmpeg; increase to 5 (and change to %5d) if more is needed
			OutputSetting->FrameNumberOffset = frameNumberOffset;
		}
		Job->JobName = jobName;

		if (isVideoCapture && OutputSetting)
		{
			FString frameExtension = m_CurrentRender->format;
			frameExtension.ToLowerInline();
			if (frameExtension.IsEmpty())
			{
				frameExtension = TEXT("png");
			}

			BeginMRQVideoProgressState(OutputSetting->OutputDirectory.Path, m_CurrentRender->uid, frameExtension, renderedFrames);
		}


		// Set the output format
		if (m_CurrentRender->format.Equals(TEXT("png"), ESearchCase::IgnoreCase))
		{
			MasterConfig->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_PNG::StaticClass());
			if (m_CurrentRender->transparent)
			{
				UMoviePipelineDeferredPassBase* MainPass = Cast<UMoviePipelineDeferredPassBase>(MasterConfig->FindOrAddSettingByClass(UMoviePipelineDeferredPassBase::StaticClass()));
				MainPass->bRenderMainPass = true;
				MainPass->bAccumulatorIncludesAlpha = true;

				UMoviePipelineImageSequenceOutput_PNG* PNGOutput = MasterConfig->FindSetting<UMoviePipelineImageSequenceOutput_PNG>();
				if (PNGOutput)
				{
					PNGOutput->bWriteAlpha = true;
				}
			}
		}
		else if (m_CurrentRender->format.Equals(TEXT("jpg"), ESearchCase::IgnoreCase))
		{
			MasterConfig->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_JPG::StaticClass());
		}
		else
		{
			// Fallback to PNG
			MasterConfig->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_PNG::StaticClass());
		}

		ZLScreenshotAAMode aaMode = m_CurrentRender->aaMode;

		// Set anti-aliasing settings
		UMoviePipelineAntiAliasingSetting* AntiAliasingSetting = Cast<UMoviePipelineAntiAliasingSetting>(MasterConfig->FindOrAddSettingByClass(UMoviePipelineAntiAliasingSetting::StaticClass()));

		switch (aaMode)
		{
		case ZLScreenshotAAMode::None:
			break;
		case ZLScreenshotAAMode::DLSS:
		case ZLScreenshotAAMode::DLAA:
#if WITH_DLSS
			if (IPluginManager::Get().FindPlugin("DLSS") && IPluginManager::Get().FindPlugin("DLSS")->IsEnabled())
			{
				UMoviePipelineDLSSSetting* DLSSSetting = Cast<UMoviePipelineDLSSSetting>(
					MasterConfig->FindOrAddSettingByClass(UMoviePipelineDLSSSetting::StaticClass())
				);

				if (DLSSSetting)
				{
					DLSSSetting->DLSSQuality = (aaMode == ZLScreenshotAAMode::DLSS) ? EMoviePipelineDLSSQuality::EMoviePipelineDLSSQuality_UltraQuality : EMoviePipelineDLSSQuality::EMoviePipelineDLSSQuality_DLAA;
					UE_LOG(LogZLCloudPlugin, Log, TEXT("DLSS Enabled for MRQ Job: %s"), *jobName);
				}
			}

			if (AntiAliasingSetting)
			{
				AntiAliasingSetting->SpatialSampleCount = m_CurrentRender->spatialSampleCount;
				AntiAliasingSetting->TemporalSampleCount = m_CurrentRender->temporalSampleCount;
				AntiAliasingSetting->bOverrideAntiAliasing = true;
				AntiAliasingSetting->AntiAliasingMethod = EAntiAliasingMethod::AAM_TSR; //https://forums.developer.nvidia.com/t/built-in-unreal-tsr-aa-is-required-for-dlaa/270176/3
				AntiAliasingSetting->bUseCameraCutForWarmUp = false;
				AntiAliasingSetting->RenderWarmUpCount = 4;
			}
#endif
			break;
		case ZLScreenshotAAMode::TSR:
		case ZLScreenshotAAMode::TAA:
		default:
			if (AntiAliasingSetting)
			{
				AntiAliasingSetting->SpatialSampleCount = m_CurrentRender->spatialSampleCount;
				AntiAliasingSetting->TemporalSampleCount = m_CurrentRender->temporalSampleCount;
				AntiAliasingSetting->bOverrideAntiAliasing = true;
				AntiAliasingSetting->AntiAliasingMethod = (aaMode == ZLScreenshotAAMode::TSR) ? EAntiAliasingMethod::AAM_TSR : EAntiAliasingMethod::AAM_TemporalAA;
				AntiAliasingSetting->bUseCameraCutForWarmUp = false;
			}
			break;
		}

		if (m_CurrentRender->cVarOverrides.Num() > 0)
		{
			UMoviePipelineConsoleVariableSetting* CVarSetting = Cast<UMoviePipelineConsoleVariableSetting>(
				MasterConfig->FindOrAddSettingByClass(UMoviePipelineConsoleVariableSetting::StaticClass())
			);

			if (CVarSetting)
			{
				for (const FString& CommandEntry : m_CurrentRender->cVarOverrides)
				{
					// Expected format: "r.ScreenPercentage 150"
					FString CVarName;
					FString CVarValueStr;

					if (CommandEntry.Split(TEXT(" "), &CVarName, &CVarValueStr))
					{
						// The MRQ Auto-Restore system only works for Numeric (Float/Int) CVars.
						// verify it is numeric before adding it to the map.
						if (CVarValueStr.IsNumeric())
						{
							float CVarValue = FCString::Atof(*CVarValueStr);
							CVarSetting->AddConsoleVariable(CVarName, CVarValue);
						}
						else
						{
							UE_LOG(LogZLCloudPlugin, Warning, TEXT("Ignored CVar '%s': Value is not numeric. MRQ only auto-restores numeric CVars."), *CommandEntry);
						}
					}
					else
					{
						UE_LOG(LogZLCloudPlugin, Warning, TEXT("Ignored CVar '%s': Invalid format. Expected 'Key Value'."), *CommandEntry);
					}
				}
			}
		}

		UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
		if (!Subsystem->OnRenderFinished.IsBound())
		{
			Subsystem->OnRenderFinished.AddDynamic(StateManager, &UZLCloudPluginStateManager::OnMoviePipelineFinishedNotifyZLScreenshot);
		}
		Job->SetConfiguration(MasterConfig);
		m_CurrentRender->ActiveRenderJob = Job;
		UE_LOG(LogZLCloudPlugin, Display, TEXT("MRQ dispatching render job. JobName=%s Sequence=%s"),
			*Job->JobName,
			LevelSequence ? *LevelSequence->GetPathName() : TEXT("<null>"));

		// Request state processing has completed in media capture. Producing the video sequence frames must use
		// media recording so internal sequence state triggers play out normally instead of jumping to end.
		if (isVideoCapture)
		{
			ZLCloudPlugin::FZLCloudPluginModule::GetModule()->SetContentGenerationState(EZLContentGenerationState::MediaRecording);
		}

		Subsystem->RenderJob(Job);
#else
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("MovieRenderQueue 2DOD Support is only available in UE 5.3+"));
		return false;
#endif
	}
	return true;
}

void SanitizeParam(FString& paramStr)
{
	//any parameter that gets sent to ffmpeg should be sanitized to prevent injection problems
	int32 c0(-1), c1(-1);
	for (int32 i = 0; i < paramStr.Len(); i++)
	{
		char c = paramStr[i];
		if ((c <= 32) || (c >= 128))
		{
			if (c0 >= 0)
			{
				c1 = i;
				break;
			}
		}
		else if (c0 < 0)
		{
			c0 = i;
		}
	}

	if (c1 >= 0)
	{
		paramStr.LeftInline(c1);
		UE_LOG(LogTemp, Warning, TEXT("Removed extraneous data from param giving %s"), *paramStr);
	}
}

#ifdef SUPPORT_LEGACY_MESSAGES
bool ZLScreenshot::RequestScreenshot(const char* settingsJson, UWorld* InWorld, FString& errorMsgOut, bool isLegacyMessage)
#else //SUPPORT_LEGACY_MESSAGES
bool ZLScreenshot::RequestScreenshot(const char* settingsJson, UWorld* InWorld, FString& errorMsgOut)
#endif //SUPPORT_LEGACY_MESSAGES
{
	UE_LOG(LogZLCloudPlugin, Display, TEXT("Received render request to process: %s"), *FString(settingsJson));

	//Check if we don't already have a screenshot requested
	UE_LOG(LogZLCloudPlugin, Display, TEXT("Requesting render"));
	if (m_NextRender.IsValid())
	{
		errorMsgOut = "Existing job request still processing";
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Still rendering screenshot"));
		return false;
	}

	errorMsgOut = "Request data could not be parsed";

	TSharedPtr<FJsonObject> JsonParsed;
	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(settingsJson);
	if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
	{
		FString jsonOut;
		int32 width, height;
		FString format, path;
		FString uid;
		int32 type;
		const TSharedPtr<FJsonObject>* stateRequestData = nullptr;

		if (!JsonParsed->TryGetStringField(TEXT("width"), jsonOut))
			return false;
		width = FCString::Atoi(*jsonOut);

		if (!JsonParsed->TryGetStringField(TEXT("height"), jsonOut))
			return false;
		height = FCString::Atoi(*jsonOut);

		if (!JsonParsed->TryGetStringField(TEXT("format"), format))
			return false;

		TSharedPtr<FJsonObject> AdvancedRenderConfig;

		//Optional: requires state-match complete callback before screenshot is taken
		if (!JsonParsed->TryGetObjectField(TEXT("state"), stateRequestData))
		{

		}
		else if (stateRequestData->IsValid() && stateRequestData->Get()->Values.Num() == 0)
		{
			stateRequestData = nullptr;
		}
		else if (stateRequestData->IsValid())
		{
			const TSharedPtr<FJsonObject>* OriginalObjectPtr = nullptr;
			if (stateRequestData->Get()->TryGetObjectField(TEXT("zerolightAdvancedRenderConfig"), OriginalObjectPtr) && OriginalObjectPtr && OriginalObjectPtr->IsValid())
			{
				FString JsonString;

				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);

				FJsonSerializer::Serialize(OriginalObjectPtr->ToSharedRef(), Writer);

				AdvancedRenderConfig = MakeShared<FJsonObject>();

				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
				FJsonSerializer::Deserialize(Reader, AdvancedRenderConfig);

				stateRequestData->Get()->RemoveField(TEXT("zerolightAdvancedRenderConfig")); //Ensure this doesnt go to StateManager
			}
		}

		//Optional: allows render config without a state request (e.g. local editor job submission)
		if (!AdvancedRenderConfig.IsValid())
		{
			const TSharedPtr<FJsonObject>* TopLevelRenderConfig = nullptr;
			if (JsonParsed->TryGetObjectField(TEXT("zerolightAdvancedRenderConfig"), TopLevelRenderConfig) && TopLevelRenderConfig && TopLevelRenderConfig->IsValid())
			{
				AdvancedRenderConfig = *TopLevelRenderConfig;
			}
		}

		//Optional: only writes to disk if dir path supplied
		path = "";
		JsonParsed->TryGetStringField(TEXT("path"), path);

		if (!JsonParsed->TryGetStringField(TEXT("uid"), uid))
		{
			//Should return but i dont have a ID at the moment and this is easier for debugging
			uid = FGuid::NewGuid().ToString();
			UE_LOG(LogZLCloudPlugin, Error, TEXT("No ID specified, using default: %s"), *uid);
		}

		if (!JsonParsed->TryGetNumberField(TEXT("type"), type))
			return false;

		m_NextRender = MakeShared<ZLScreenshotJob>(width, height, format, path, uid, InWorld, stateRequestData, (ScreenshotType)type);

#ifdef SUPPORT_LEGACY_MESSAGES
		m_NextRender->isLegacyMessage = isLegacyMessage;
#endif //SUPPORT_LEGACY_MESSAGES

		bool isVideoCapture = false;
		if (JsonParsed->TryGetBoolField(TEXT("videoCapture"), isVideoCapture) && isVideoCapture)
		{
			const UZLCloudPluginSettings* PluginSettings = GetMutableDefault<UZLCloudPluginSettings>();
			const FString FFmpegExePath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("ZLCloudPlugin/Binaries/ThirdParty/ffmpeg/ffmpeg.exe"));
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			const bool bFfmpegEnabled = PluginSettings->bDownloadFFmpegForMediaOnDemandVideos;
			const bool bFfmpegExists = PlatformFile.FileExists(*FFmpegExePath);

			if (!bFfmpegEnabled || !bFfmpegExists)
			{
				if (!bFfmpegEnabled)
				{
					errorMsgOut = TEXT("Video capture is not supported by this application. Enable \"Download ffmpeg for media on demand videos\" in OmniStream Settings (Build and Deploy) and rebuild the application.");
				}
				else
				{
					errorMsgOut = FString::Printf(
						TEXT("Video capture requires ffmpeg.exe but it was not found at %s. Enable \"Download ffmpeg for media on demand videos\" in OmniStream Settings (Build and Deploy) and rebuild the application."),
						*FFmpegExePath);
				}
				UE_LOG(LogZLCloudPlugin, Error, TEXT("Video capture request rejected: %s"), *errorMsgOut);
				m_NextRender.Reset();
				return false;
			}

			m_NextRender->videoFormat = m_NextRender->format;
			SanitizeParam(m_NextRender->videoFormat);
			if (!JsonParsed->TryGetStringField(TEXT("frameFormat"), m_NextRender->format))
			{
				m_NextRender->format = "png";
				UE_LOG(LogTemp, Warning, TEXT("No video frame format, defaulted to %s"), *m_NextRender->format);
			}

			JsonParsed->TryGetBoolField(TEXT("debug"), m_NextRender->debug);

			if (!JsonParsed->TryGetNumberField(TEXT("frameRate"), m_NextRender->videoFrameRate))
			{
				m_NextRender->videoFrameRate = 30.0;	//default to 30fps
				UE_LOG(LogTemp, Display, TEXT("No video frame rate, defaulted to %f fps"), m_NextRender->videoFrameRate);
			}
			if (!JsonParsed->TryGetNumberField(TEXT("numFrames"), m_NextRender->videoNumFrames))
			{
				m_NextRender->videoNumFrames = -1;	//default to playing all of specified sequence
				UE_LOG(LogTemp, Display, TEXT("No video frame frame count, defaulted to %d"), m_NextRender->videoNumFrames);
			}
			if (!JsonParsed->TryGetNumberField(TEXT("startFrame"), m_NextRender->videoStartFrame))
			{
				m_NextRender->videoStartFrame = 0;	//default to playing from beginning of specified sequence
				UE_LOG(LogTemp, Display, TEXT("No video start frame, defaulted to %d"), m_NextRender->videoStartFrame);
			}
			if (!JsonParsed->TryGetStringField(TEXT("sequenceName"), m_NextRender->videoSequenceName))
			{
				UE_LOG(LogTemp, Error, TEXT("No video sequence specified"));
				errorMsgOut = "Video capture requested with no sequence";
				m_NextRender.Reset();
				return false;
			}

			if (JsonParsed->TryGetStringField(TEXT("codec"), m_NextRender->videoCodec))
			{
				SanitizeParam(m_NextRender->videoCodec);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("No video codec specified, will use default for video format %s"), *m_NextRender->videoFormat);
			}

			if (JsonParsed->TryGetStringField(TEXT("pixelFormat"), m_NextRender->videoPixelFormat))
			{
				SanitizeParam(m_NextRender->videoPixelFormat);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("No video pixel format specified, will use default for video format %s"), *m_NextRender->videoFormat);
			}

			if (JsonParsed->TryGetStringArrayField(TEXT("encodeOptions"), m_NextRender->videoEncodeOptions))
			{
				for (FString& param : m_NextRender->videoEncodeOptions)
					SanitizeParam(param);
			}
			else
			{
				m_NextRender->videoEncodeOptions.Empty();
				m_NextRender->videoEncodeOptions.Add("-vf \"scale=out_color_matrix=bt709:out_range=limited,format=yuv420p\"");
				m_NextRender->videoEncodeOptions.Add("-colorspace bt709");
				m_NextRender->videoEncodeOptions.Add("-color_trc bt709");
				m_NextRender->videoEncodeOptions.Add("-color_primaries bt709");
				m_NextRender->videoEncodeOptions.Add("-color_range 1");
				m_NextRender->videoEncodeOptions.Add("-movflags faststart");

				UE_LOG(LogTemp, Display, TEXT("No video options, will use defaults"));
			}

			if (m_NextRender->videoFormat.Equals("mp4", ESearchCase::IgnoreCase))
			{
				if (m_NextRender->videoEncodeOptions.IsEmpty())	//don't set defaults if any specific options are set; assume the user has sent everything they need
				{
					if (m_NextRender->videoCodec.IsEmpty())
					{
						//use h264 for mp4; any other formats can use ffmpeg default, seems to be Lavc62.23.102 mpeg4
						//libx264 gives smaller output (65% in 30-frame test) than h264, but not available on LGPL version of ffmpeg
						m_NextRender->videoCodec = TEXT("h264");
						UE_LOG(LogTemp, Display, TEXT("Using our default video codec %s for format %s"), *m_NextRender->videoCodec, *m_NextRender->videoFormat);
					}

					if (m_NextRender->videoPixelFormat.IsEmpty())
					{
						//note: pix_fmt yuv420p makes no difference with libopenh264 (used on LGPL version of ffmpeg) but does with libx264 (GPL version)
						m_NextRender->videoPixelFormat = TEXT("yuv420p");
						UE_LOG(LogTemp, Display, TEXT("Using our default video pixel format %s for format %s"), *m_NextRender->videoPixelFormat, *m_NextRender->videoFormat);
					}
				}
			}

			m_NextRender->useMRQPipeline = true;	//force MovieRenderQueue pipeline, as this is the only way we have to get video data anyway
		}

		if (AdvancedRenderConfig.IsValid())
		{
			AdvancedRenderConfig->TryGetBoolField(TEXT("useMovieRenderQueue"), m_NextRender->useMRQPipeline);

			const TArray<TSharedPtr<FJsonValue>>* CVarArrayValue;
			if (AdvancedRenderConfig->TryGetArrayField(TEXT("cVarOverrides"), CVarArrayValue))
			{
				TArray<FString> cVarOverrides;
				for (TSharedPtr<FJsonValue> Value : *CVarArrayValue)
				{
					cVarOverrides.Add(Value.Get()->AsString());
				}

				m_NextRender->cVarOverrides = cVarOverrides;
			}

			FString aaModeString;
			if (AdvancedRenderConfig->TryGetStringField(TEXT("aaMode"), aaModeString))
			{
				if (aaModeString.Equals("TAA", ESearchCase::IgnoreCase))
				{
					m_NextRender->aaMode = ZLScreenshotAAMode::TAA;
				}
				else if (aaModeString.Equals("TSR", ESearchCase::IgnoreCase))
				{
					m_NextRender->aaMode = ZLScreenshotAAMode::TSR;
				}
				else if (aaModeString.Equals("DLSS", ESearchCase::IgnoreCase))
				{
					m_NextRender->aaMode = ZLScreenshotAAMode::DLSS;
				}
				else if (aaModeString.Equals("None", ESearchCase::IgnoreCase))
				{
					m_NextRender->aaMode = ZLScreenshotAAMode::None;
				}
				else if (aaModeString.Equals("DLAA", ESearchCase::IgnoreCase))
				{
					m_NextRender->aaMode = ZLScreenshotAAMode::DLAA;
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("Unknown aaMode '%s' in config. Defaulting to TAA."), *aaModeString);
				}
			}

			int spatialSamples;
			if (AdvancedRenderConfig->TryGetNumberField(TEXT("spatialSampleCount"), spatialSamples))
			{
				m_NextRender->spatialSampleCount = spatialSamples;
			}

			int temporalSamples;
			if (AdvancedRenderConfig->TryGetNumberField(TEXT("temporalSampleCount"), temporalSamples))
			{
				m_NextRender->temporalSampleCount = temporalSamples;
			}

			bool ignoreCinematicOverrides = false;
			if (AdvancedRenderConfig->TryGetBoolField(TEXT("ignoreCinematicOverrides"), ignoreCinematicOverrides))
			{
				m_NextRender->ignoreCinematicOverrides = ignoreCinematicOverrides;
			}

			bool avoidLargeVramAllocations = false;
			if (AdvancedRenderConfig->TryGetBoolField(TEXT("avoidLargeVramAllocations"), avoidLargeVramAllocations))
			{
				m_NextRender->avoidLargeVramAllocations = avoidLargeVramAllocations;
			}

			bool disableBillboardSprites = false;
			if (AdvancedRenderConfig->TryGetBoolField(TEXT("disableBillboardSprites"), disableBillboardSprites)
				&& disableBillboardSprites)
			{
				// Keeps editor-only billboards (light/audio/arrow component icons) out of the capture.
				// Goes through the job CVar list so both capture paths restore the previous value when
				// the job ends: MRQ on console variable setting teardown, the viewport path in
				// RestoreJobCVarOverrides. Shipping builds fix this show flag on, so it is a no-op there.
				m_NextRender->cVarOverrides.Add(TEXT("ShowFlag.BillboardSprites 0"));
			}

			bool keepSourceFrames = false;
			if (AdvancedRenderConfig->TryGetBoolField(TEXT("keepSourceFrames"), keepSourceFrames))
			{
				m_NextRender->keepSourceFrames = keepSourceFrames;
			}

			bool transparentSourceFrames = false;
			if (isVideoCapture &&
				AdvancedRenderConfig->TryGetBoolField(TEXT("transparentSourceFrames"), transparentSourceFrames) &&
				transparentSourceFrames)
			{
				m_NextRender->transparentSourceFrames = true;
				m_NextRender->transparent = true;
				m_NextRender->keepSourceFrames = true;
				m_NextRender->format = TEXT("png");
				m_NextRender->useMRQPipeline = true;
				// MRQ's game override owns r.PostProcessing.PropagateAlpha; adding it as a job cvar
				// reasserts it after that restore and leaves alpha on in the live viewport.
				UE_LOG(LogZLCloudPlugin, Display,
					TEXT("Video job %s will retain transparent PNG source frames"),
					*m_NextRender->uid);
			}

			bool resetDLSSHistory = false;
			if (AdvancedRenderConfig->TryGetBoolField(TEXT("resetDLSSHistory"), resetDLSSHistory)
				&& resetDLSSHistory)
			{
				// DLSS keeps accumulating the live viewport's history into the start of the capture, which
				// reads as a ghost of the pre-job view over the first frames. Requesting the reset through
				// the job CVar list means MRQ asserts it when the pipeline is set up - after the job is
				// dispatched and before the sequence renders - and restores it on pipeline teardown.
				m_NextRender->cVarOverrides.Add(FString::Printf(TEXT("r.NGX.DLSS.ResetHistoryFrames %d"), s_dlssHistoryResetFrames));
				UE_LOG(LogZLCloudPlugin, Display, TEXT("Job %s will discard %d frames of DLSS history as the capture starts"),
					*m_NextRender->uid, s_dlssHistoryResetFrames);
			}

			FString panoModeString;
			if (AdvancedRenderConfig->TryGetStringField(TEXT("panoMode"), panoModeString)
				&& !panoModeString.Equals(TEXT("cubemap"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogZLCloudPlugin, Warning,
					TEXT("Unsupported panoMode '%s' ignored; only cubemap is supported."),
					*panoModeString);
			}

			double panoCubemapOverscan;
			if (AdvancedRenderConfig->TryGetNumberField(TEXT("panoCubemapOverscan"), panoCubemapOverscan))
			{
				m_NextRender->panoCubemapOverscan = FMath::Clamp(static_cast<float>(panoCubemapOverscan), 0.f, 1.f);
			}

			int panoCubemapWarmup;
			if (AdvancedRenderConfig->TryGetNumberField(TEXT("panoCubemapWarmupFrames"), panoCubemapWarmup))
			{
				m_NextRender->panoCubemapWarmupFrames = FMath::Max(0, panoCubemapWarmup);
			}

		}

		bool transparent = false;
		if (JsonParsed->TryGetBoolField(TEXT("transparent"), transparent))
		{
			m_NextRender->transparent = transparent;
			if (transparent)
			{
				m_NextRender->useMRQPipeline = true;
				m_NextRender->cVarOverrides.Add("r.PostProcessing.PropagateAlpha=true");
			}
		}

		if (m_NextRender->type == ScreenshotType::EQUIRECT360 && m_NextRender->transparent)
		{
			errorMsgOut = TEXT("Transparent panorama capture is not supported.");
			UE_LOG(LogZLCloudPlugin, Error, TEXT("Panorama capture request rejected: %s"), *errorMsgOut);
			m_NextRender.Reset();
			return false;
		}

		if (m_NextRender->type == ScreenshotType::EQUIRECT360 && m_NextRender->useMRQPipeline)
		{
			errorMsgOut = TEXT("MRQ panorama capture is no longer supported; use the viewport GPU panorama path.");
			UE_LOG(LogZLCloudPlugin, Error, TEXT("Panorama capture request rejected: %s"), *errorMsgOut);
			m_NextRender.Reset();
			return false;
		}

		// r.HighResScreenshotDelay is applied in ApplyCVarOverridesDirect so JSON
		// cVarOverrides can win over the plugin default, and both restore via SetByCode.

		// Enter media capture for state matching and pre-capture processing until the job finishes.
		ZLCloudPlugin::FZLCloudPluginModule::GetModule()->SetContentGenerationState(EZLContentGenerationState::MediaCapture);
	}
	else
		return false;

	if (!m_onScreenshotCapturedDelegateHandle.IsValid()) //Locally testing 2DOD may not have set this delegate as no adopt needed
		Set2DODMode(true);

	return true;
}

bool ZLScreenshot::RequestStreamImageCapture(
	const FString& RequestId,
	int32 Width,
	int32 Height,
	const FString& Format,
	UWorld* InWorld,
	FString& ErrorMsgOut,
	const FString& OptionsJson)
{
	if (RequestId.IsEmpty())
	{
		ErrorMsgOut = TEXT("Request ID must not be empty");
		return false;
	}

	if (Width <= 0 || Height <= 0)
	{
		ErrorMsgOut = TEXT("Width and height must be positive");
		return false;
	}

	FString NormalizedFormat = Format.ToLower();
	if (NormalizedFormat == TEXT("jpeg"))
	{
		NormalizedFormat = TEXT("jpg");
	}

	if (NormalizedFormat != TEXT("png") && NormalizedFormat != TEXT("jpg"))
	{
		ErrorMsgOut = TEXT("Format must be 'png' or 'jpg'");
		return false;
	}

	if (InWorld == nullptr || InWorld->GetGameViewport() == nullptr)
	{
		ErrorMsgOut = TEXT("No active game viewport");
		return false;
	}

	if (m_NextRender.IsValid() || m_CurrentRender.IsValid())
	{
		ErrorMsgOut = TEXT("Existing job request still processing");
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Stream image capture rejected: %s"), *ErrorMsgOut);
		return false;
	}

	FString Path;
	FString FormatCopy = NormalizedFormat;
	FString RequestIdCopy = RequestId;
	ScreenshotType CaptureType = ScreenshotType::DEFAULT2D;
	bool transparent = false;
	FString TransparentEnvironmentOverride;
	TSharedPtr<FJsonObject> StreamCaptureState;
	const TSharedPtr<FJsonObject>* JobStateRequestData = nullptr;

	if (!OptionsJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> OptionsParsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OptionsJson);
		if (FJsonSerializer::Deserialize(Reader, OptionsParsed) && OptionsParsed.IsValid())
		{
			FString ModeString;
			if (OptionsParsed->TryGetStringField(TEXT("mode"), ModeString))
			{
				if (ModeString.Equals(TEXT("pano"), ESearchCase::IgnoreCase)
					|| ModeString.Equals(TEXT("EQUIRECT360"), ESearchCase::IgnoreCase))
				{
					CaptureType = ScreenshotType::EQUIRECT360;
				}
			}
			else
			{
				FString TypeString;
				if (OptionsParsed->TryGetStringField(TEXT("type"), TypeString)
					&& TypeString.Equals(TEXT("EQUIRECT360"), ESearchCase::IgnoreCase))
				{
					CaptureType = ScreenshotType::EQUIRECT360;
				}
			}

			OptionsParsed->TryGetBoolField(TEXT("transparent"), transparent);
			OptionsParsed->TryGetStringField(TEXT("environment"), TransparentEnvironmentOverride);
		}
		else
		{
			UE_LOG(LogZLCloudPlugin, Warning, TEXT("Stream image capture: failed to parse options JSON"));
		}
	}

	if (transparent)
	{
		if (CaptureType == ScreenshotType::EQUIRECT360)
		{
			ErrorMsgOut = TEXT("Transparent panorama capture is not supported.");
			return false;
		}

		if (NormalizedFormat != TEXT("png"))
		{
			ErrorMsgOut = TEXT("Transparent capture requires format 'png'");
			return false;
		}

		FormatCopy = TEXT("png");

		UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
		if (!StateManager)
		{
			ErrorMsgOut = TEXT("State manager unavailable for transparent capture");
			return false;
		}

		FString CurrentStateJson;
		StateManager->GetCurrentState(CurrentStateJson);

		TSharedPtr<FJsonObject> CurrentState;
		const TSharedRef<TJsonReader<>> StateReader = TJsonReaderFactory<>::Create(CurrentStateJson);
		if (!FJsonSerializer::Deserialize(StateReader, CurrentState) || !CurrentState.IsValid())
		{
			ErrorMsgOut = TEXT("Failed to read current state for transparent capture");
			return false;
		}

		StreamCaptureState = MakeShared<FJsonObject>();
		FJsonObject::Duplicate(CurrentState.ToSharedRef(), StreamCaptureState);

		const FString TransparentEnvironment = TransparentEnvironmentOverride.IsEmpty()
			? TEXT("TransparentScreenshotBG")
			: TransparentEnvironmentOverride;

		FString PreviousEnvironment;
		StreamCaptureState->TryGetStringField(TEXT("environment"), PreviousEnvironment);
		StreamCaptureState->SetStringField(TEXT("environment"), TransparentEnvironment);
		JobStateRequestData = &StreamCaptureState;

		m_NextRender = MakeShared<ZLScreenshotJob>(
			Width,
			Height,
			FormatCopy,
			Path,
			RequestIdCopy,
			InWorld,
			JobStateRequestData,
			CaptureType);
		m_NextRender->bStreamImageCapture = true;
		m_NextRender->transparent = true;
		m_NextRender->useMRQPipeline = true;
		m_NextRender->cVarOverrides.Add(TEXT("r.PostProcessing.PropagateAlpha=true"));

		if (!PreviousEnvironment.Equals(TransparentEnvironment, ESearchCase::IgnoreCase))
		{
			m_NextRender->streamRestoreEnvironment = PreviousEnvironment;
			m_NextRender->bNeedsStreamEnvironmentRestore = true;
		}

		ZLCloudPlugin::FZLCloudPluginModule::GetModule()->SetContentGenerationState(EZLContentGenerationState::MediaCapture);

		UE_LOG(LogZLCloudPlugin, Display,
			TEXT("Stream transparent capture: switching environment '%s' -> '%s'"),
			*PreviousEnvironment, *TransparentEnvironment);
	}
	else
	{
		m_NextRender = MakeShared<ZLScreenshotJob>(
			Width,
			Height,
			FormatCopy,
			Path,
			RequestIdCopy,
			InWorld,
			JobStateRequestData,
			CaptureType);
		m_NextRender->bStreamImageCapture = true;
	}

	if (!m_onScreenshotCapturedDelegateHandle.IsValid())
	{
		Set2DODMode(true);
	}

	UE_LOG(LogZLCloudPlugin, Display,
		TEXT("Stream image capture queued: requestId=%s, size=%dx%d, format=%s, mode=%s, transparent=%s"),
		*RequestId, Width, Height, *FormatCopy,
		CaptureType == ScreenshotType::EQUIRECT360 ? TEXT("pano") : TEXT("2d"),
		transparent ? TEXT("true") : TEXT("false"));

	return true;
}

void ZLCloudPlugin::ZLScreenshot::UpdateCurrentRenderStateRequestProgress(bool completed, bool matchSuccess)
{
	if (m_CurrentRender.IsValid())
	{
		m_CurrentRender->jobStateRequestFinished = completed;
		m_CurrentRender->jobStateRequestSuccess = matchSuccess;
	}
}

void ZLCloudPlugin::ZLScreenshot::SetCurrentRenderStateData(TSharedPtr<FJsonObject> currentState, TSharedPtr<FJsonObject> timeoutState, TSharedPtr<FJsonObject> unmatchedState)
{
	if (m_CurrentRender.IsValid())
	{
		if (currentState != nullptr)
		{
			m_CurrentRender->postJobCurrentState = MakeShared<FJsonObject>();
			FJsonObject::Duplicate(currentState, m_CurrentRender->postJobCurrentState);
		}

		if (timeoutState != nullptr)
		{
			m_CurrentRender->postJobTimeoutState = MakeShared<FJsonObject>();
			FJsonObject::Duplicate(timeoutState, m_CurrentRender->postJobTimeoutState);
		}

		if (unmatchedState != nullptr)
		{
			m_CurrentRender->postJobUnmatchedState = MakeShared<FJsonObject>();
			FJsonObject::Duplicate(unmatchedState, m_CurrentRender->postJobUnmatchedState);
		}
	}
}

void ZLScreenshot::Set2DODMode(bool is2DOD)
{
	if (is2DOD)
	{
		m_onScreenshotCapturedDelegateHandle = GEngine->GameViewport->OnScreenshotCaptured().AddRaw(this, &ZLScreenshot::OnScreenshotComplete);
		UE_LOG(LogZLCloudPlugin, Display, TEXT("Register ZLScreenshot OnScreenshotCaptured"));
	}
	else
	{
		if (m_onScreenshotCapturedDelegateHandle.IsValid())
		{
			GEngine->GameViewport->OnScreenshotCaptured().Remove(m_onScreenshotCapturedDelegateHandle);
			m_onScreenshotCapturedDelegateHandle.Reset();
			UE_LOG(LogZLCloudPlugin, Display, TEXT("Deregister ZLScreenshot OnScreenshotCaptured"));
		}
	}
	ZLCloudPlugin::FZLCloudPluginModule::GetModule()->SetOnDemandMode(is2DOD);
}

namespace
{
	struct FCaptureCVarDefault
	{
		const TCHAR* Name;
		const TCHAR* Value;
	};

	/**
	 * Capture CVar merge order (later wins):
	 *   1) GCapturePanoCVarDefaults                       — panorama jobs only
	 *   2) GCaptureRayTracingCVarDefaults                 — every RT-enabled capture
	 *   3) r.HighResScreenshotDelay                       — 16 if RT, else 8
	 *   4) UZLCloudPluginSettings screenshotFrameWaitCountOverride (if set)
	 *   5) Job JSON cVarOverrides                         — testing / per-job tuning
	 *
	 * 2D viewport captures receive the shared RT quality pack, screenshot delay and explicit
	 * job JSON overrides; pano-specific post-effect behavior remains pano-only.
	 * Applied once at each CVar's existing priority and restored with that same priority.
	 */

	// Panorama-only hygiene: these alter image appearance and/or Lumen behavior.
	static const FCaptureCVarDefault GCapturePanoCVarDefaults[] =
	{
		// Match TakeHighResScreenShot, which forces highest LOD for the capture batch.
		{ TEXT("r.ForceLOD"), TEXT("0") },
		// Frame-based warmup / settle should run as fast as the renderer allows.
		{ TEXT("t.MaxFPS"), TEXT("0") },
		{ TEXT("ShowFlag.Vignette"), TEXT("0") },
		{ TEXT("ShowFlag.MotionBlur"), TEXT("0") },
		{ TEXT("ShowFlag.Bloom"), TEXT("0") },
		{ TEXT("ShowFlag.SceneColorFringe"), TEXT("0") },
		// Surface-cache lighting + capture (MRQ offline-capture pattern).
		{ TEXT("r.LumenScene.Radiosity.UpdateFactor"), TEXT("1") },
		{ TEXT("r.LumenScene.DirectLighting.UpdateFactor"), TEXT("1") },
		{ TEXT("r.LumenScene.SurfaceCache.CardCaptureFactor"), TEXT("1") },
		{ TEXT("r.LumenScene.SurfaceCache.Feedback"), TEXT("0") },
		// Lumen history resample uses a 1D compute dispatch sized to card-capture tiles.
		// Pano viewport resize / high-res dummy RTs can exceed the RHI dispatch limit;
		// fresh captures do not need history resample.
		{ TEXT("r.LumenScene.SurfaceCache.ResampleLighting"), TEXT("0") },
		// Short-range AO horizon search leaves radial streaks after equirect stitch / on floors.
		{ TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO"), TEXT("0") },
		// Screen traces are view-dependent (HZB / depth history). Each cube face has a different
		// frustum, so GI and reflections lit by screen traces seam at face boundaries. Force HW /
		// world-space Lumen only for the capture batch (matches SceneCapture behavior).
		{ TEXT("r.Lumen.ScreenProbeGather.ScreenTraces"), TEXT("0") },
		{ TEXT("r.Lumen.Reflections.ScreenTraces"), TEXT("0") },
		// Screen probe upsampling jitters (DownsampleFactor px/frame) and picks probes
		// stochastically (1 tap instead of 4 bilinear); both are designed to resolve over the
		// temporal filter. A camera cut per orientation restarts that accumulation, so each face
		// is captured mid-convergence with a content-dependent bias — a whole-face offset that
		// overscan cannot crop away. Take the deterministic, higher-sample path instead.
		{ TEXT("r.Lumen.ScreenProbeGather.FullResolutionJitterWidth"), TEXT("0") },
		{ TEXT("r.Lumen.ScreenProbeGather.StochasticInterpolation"), TEXT("0") },
		// Tighter probe grid (engine default 16) so screen-space probe placement contributes
		// less to how a given surface is lit from one face's frustum versus another's.
		{ TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"), TEXT("4") },
		// More radiance-cache probes per frame without shortening temporal accumulation.
		{ TEXT("r.Lumen.ScreenProbeGather.RadianceCache.NumProbesToTraceBudget"), TEXT("300") },
	};

	// Ray-traced quality pack shared by every capture. HighResScreenshotDelay is added separately from
	// s_hiresScreenshotFrameCountRaytracing so the numeric default lives in one place.
	// Harmless no-ops when a named CVar is unused by the current project RT path.
	static const FCaptureCVarDefault GCaptureRayTracingCVarDefaults[] =
	{
		{ TEXT("r.RayTracing.Shadows.SamplesPerPixel"), TEXT("2") },
		{ TEXT("r.RayTracing.SkyLight.SamplesPerPixel"), TEXT("2") },
		{ TEXT("r.RayTracing.AmbientOcclusion.SamplesPerPixel"), TEXT("2") },
		{ TEXT("r.RayTracing.Translucency.SamplesPerPixel"), TEXT("2") },
		{ TEXT("r.Lumen.ScreenProbeGather.TracingOctahedronResolution"), TEXT("8") },
		{ TEXT("r.Lumen.Reflections.DownsampleFactor"), TEXT("1") },
		{ TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction.NumSamples"), TEXT("8") },
		{ TEXT("r.Lumen.Reflections.BilateralFilter.NumSamples"), TEXT("8") },
	};

	static const TCHAR* GHighResScreenshotDelayCVarName = TEXT("r.HighResScreenshotDelay");

	static bool IsRayTracingEnabled()
	{
		if (IConsoleVariable* RayTracingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing")))
		{
			return RayTracingCVar->GetInt() != 0;
		}
		return false;
	}

	/** Defaults only (no job JSON). Used as the base map before merging cVarOverrides. */
	static TMap<FString, FString> BuildCaptureDefaultCVars(
		bool bIsPano,
		int32 NonRayTracingScreenshotDelay,
		int32 RayTracingScreenshotDelay,
		int32 SettingsDelayOverride)
	{
		TMap<FString, FString> Defaults;

		if (bIsPano)
		{
			for (const FCaptureCVarDefault& Entry : GCapturePanoCVarDefaults)
			{
				Defaults.Add(Entry.Name, Entry.Value);
			}
		}

		const bool bRayTracing = IsRayTracingEnabled();
		if (bRayTracing)
		{
			for (const FCaptureCVarDefault& Entry : GCaptureRayTracingCVarDefaults)
			{
				Defaults.Add(Entry.Name, Entry.Value);
			}
		}

		Defaults.Add(
			GHighResScreenshotDelayCVarName,
			FString::FromInt(bRayTracing ? RayTracingScreenshotDelay : NonRayTracingScreenshotDelay));

		// Project setting wins over built-in delay, but still loses to job JSON later.
		if (SettingsDelayOverride != 0)
		{
			Defaults.Add(GHighResScreenshotDelayCVarName, FString::FromInt(SettingsDelayOverride));
		}

		return Defaults;
	}

	/** Overlay job entries shaped like "r.SomeCVar 123" onto the defaults map. */
	static void MergeJobCVarOverrides(TMap<FString, FString>& InOutMerged, const TArray<FString>& JobOverrides)
	{
		for (const FString& CommandEntry : JobOverrides)
		{
			FString CVarName;
			FString CVarValueStr;
			if (CommandEntry.Split(TEXT(" "), &CVarName, &CVarValueStr))
			{
				InOutMerged.Add(CVarName, CVarValueStr);
			}
		}
	}
}

void ZLScreenshot::ApplyMergedCaptureCVars()
{
	if (m_captureCVarsApplied || !m_CurrentRender.IsValid())
	{
		return;
	}

	m_jobSavedCVarStates.Empty();

	const UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();
	check(Settings);

	TMap<FString, FString> Merged = BuildCaptureDefaultCVars(
		m_CurrentRender->type == ScreenshotType::EQUIRECT360,
		s_hiresScreenshotFrameCount,
		s_hiresScreenshotFrameCountRaytracing,
		Settings->screenshotFrameWaitCountOverride);
	MergeJobCVarOverrides(Merged, m_CurrentRender->cVarOverrides);

	IConsoleManager& ConsoleMgr = IConsoleManager::Get();
	for (const TPair<FString, FString>& Pair : Merged)
	{
		if (IConsoleVariable* CVar = ConsoleMgr.FindConsoleVariable(*Pair.Key))
		{
			FCaptureCVarState PreviousState;
			PreviousState.Value = CVar->GetString();
			PreviousState.SetByFlags = CVar->GetFlags() & ECVF_SetByMask;
			m_jobSavedCVarStates.Add(Pair.Key, PreviousState);

			// Temporarily replace the value at its existing priority. This lets explicit console,
			// command-line and scalability ownership survive the capture unchanged.
			const EConsoleVariableFlags ExistingPriority =
				static_cast<EConsoleVariableFlags>(PreviousState.SetByFlags);
			CVar->Set(*Pair.Value, ExistingPriority);
			UE_LOG(LogZLCloudPlugin, Log,
				TEXT("ZLScreenshot: Applied capture CVar %s = %s (was %s, priority=0x%08x)"),
				*Pair.Key,
				*Pair.Value,
				*PreviousState.Value,
				PreviousState.SetByFlags);
		}
		else
		{
			UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLScreenshot: capture CVar '%s' not found"), *Pair.Key);
		}
	}

	if (IConsoleVariable* DelayCVar = ConsoleMgr.FindConsoleVariable(GHighResScreenshotDelayCVarName))
	{
		ZLJobTrace::JOBTRACE_ADD_DATA("r.HighResScreenshotDelay", MakeShared<FJsonValueNumber>(DelayCVar->GetInt()));
	}

	m_captureCVarsApplied = true;
}

void ZLScreenshot::RestoreJobCVarOverrides()
{
	if (m_jobSavedCVarStates.Num() == 0 && !m_captureCVarsApplied)
	{
		return;
	}

	IConsoleManager& ConsoleMgr = IConsoleManager::Get();
	for (const TPair<FString, FCaptureCVarState>& Pair : m_jobSavedCVarStates)
	{
		if (IConsoleVariable* CVar = ConsoleMgr.FindConsoleVariable(*Pair.Key))
		{
			const EConsoleVariableFlags SavedPriority =
				static_cast<EConsoleVariableFlags>(Pair.Value.SetByFlags);
			CVar->Set(*Pair.Value.Value, SavedPriority);
			UE_LOG(LogZLCloudPlugin, Log,
				TEXT("ZLScreenshot: Restored CVar %s = %s (priority=0x%08x)"),
				*Pair.Key,
				*Pair.Value.Value,
				Pair.Value.SetByFlags);
		}
	}

	m_jobSavedCVarStates.Empty();
	m_captureCVarsApplied = false;
}

void ZLScreenshot::BeginPanoCaptureOverrides()
{
	// Apply defaults + job JSON early so pano warmup / settle already use the capture pack.
	ApplyMergedCaptureCVars();
}

void ZLScreenshot::CleanupCubemapGpuStitcher()
{
	if (m_panoCubemapGpuStitcher.IsValid())
	{
		m_panoCubemapGpuStitcher->Cleanup();
		m_panoCubemapGpuStitcher.Reset();
	}
}

void ZLScreenshot::RestoreStreamTransparentEnvironmentIfNeeded()
{
	if (!m_CurrentRender.IsValid() || !m_CurrentRender->bStreamImageCapture || !m_CurrentRender->bNeedsStreamEnvironmentRestore)
	{
		return;
	}

	const FString RestoreEnvironment = m_CurrentRender->streamRestoreEnvironment;
	m_CurrentRender->bNeedsStreamEnvironmentRestore = false;

	TSharedPtr<FJsonObject> RestoreState = MakeShared<FJsonObject>();
	RestoreState->SetStringField(TEXT("environment"), RestoreEnvironment);

	FString StateDataStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&StateDataStr);
	if (!FJsonSerializer::Serialize(RestoreState.ToSharedRef(), Writer))
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("Stream transparent capture: failed to serialize environment restore state"));
		return;
	}

	if (Delegates)
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("Stream transparent capture: restoring environment to '%s'"), *RestoreEnvironment);
		Delegates->OnRecieveData.Broadcast(StateDataStr);
	}
}

void ZLScreenshot::SendStreamImageCaptureResponse(const TArray64<uint8>& ImageBytes)
{
	if (!m_CurrentRender.IsValid() || !m_CurrentRender->bStreamImageCapture)
	{
		return;
	}

	FString ResponseFormat = m_CurrentRender->format.ToLower();
	if (ResponseFormat == TEXT("jpeg"))
	{
		ResponseFormat = TEXT("jpg");
	}

	TArray<uint8> CompressedImageBytesArray;
	CompressedImageBytesArray.Append(ImageBytes.GetData(), ImageBytes.Num());
	const FString Base64Image = FBase64::Encode(CompressedImageBytesArray);

	TSharedPtr<FJsonObject> ResponseJson = MakeShareable(new FJsonObject);
	ResponseJson->SetStringField(TEXT("requestId"), m_CurrentRender->uid);
	ResponseJson->SetStringField(TEXT("format"), ResponseFormat);
	ResponseJson->SetStringField(TEXT("imageData"), Base64Image);

	FString ResponseJsonString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&ResponseJsonString);
	FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), JsonWriter);

	if (IZLCloudPluginModule* Module = FZLCloudPluginModule::GetModule())
	{
		Module->SendImageCapture(ResponseJsonString);
		Module->SetContentGenerationState(EZLContentGenerationState::None);
		UE_LOG(LogZLCloudPlugin, Display,
			TEXT("Stream image capture sent: requestId=%s, format=%s, bytes=%d"),
			*m_CurrentRender->uid, *ResponseFormat, CompressedImageBytesArray.Num());
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Stream image capture failed: ZLCloudPluginModule is null"));
	}

	RestoreStreamTransparentEnvironmentIfNeeded();
	ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
	m_CurrentRender.Reset();
	EditorRestoreLocalCapture2DODModeAfterJob();
}

void ZLScreenshot::SendCapturedImageResponse(const TArray64<uint8>& ImageBytes, const FIntPoint& ImageSize)
{
	if (!m_CurrentRender.IsValid() || m_LauncherComms == nullptr)
	{
		return;
	}

	ZLJobTrace::JOBTRACE_TIMER_START("ImageDataServerResponse");

	// Allow plugins to drain capture-frame work before response state is collected
	// (for example, ProcessAccumulatedResults + SetImmediateReadback(false)).
	if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
	{
		LocalDelegates->OnBeforeBuildScreenshotStateNative.Broadcast(m_CurrentRender->world);
	}

	// Prefer the exact capture view supplied by a plugin; otherwise use the final image size.
	FZLCaptureViewInfo CaptureInfo;
	CaptureInfo.bValid = false;
	if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
	{
		LocalDelegates->OnGetScreenshotCaptureViewNative.Broadcast(CaptureInfo);
	}

	FZLPrepareScreenshotParams PrepareParams;
	if (CaptureInfo.bValid)
	{
		PrepareParams.bUseCaptureView = true;
		PrepareParams.ViewProjectionMatrix = CaptureInfo.ViewProjectionMatrix;
		PrepareParams.ViewRect = CaptureInfo.ViewRect;
	}
	else
	{
		PrepareParams.CaptureWidth = ImageSize.X;
		PrepareParams.CaptureHeight = ImageSize.Y;
	}

	if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
	{
		LocalDelegates->OnPrepareScreenshotNative.Broadcast(PrepareParams);
	}

	TSharedPtr<FJsonObject> responseStateData = MakeShareable(new FJsonObject);

	if (m_CurrentRender->postJobCurrentState.IsValid())
		responseStateData->SetObjectField(TEXT("current_state"), m_CurrentRender->postJobCurrentState);

	if (m_CurrentRender->postJobUnmatchedState.IsValid())
		responseStateData->SetObjectField(TEXT("unprocessed_state"), m_CurrentRender->postJobUnmatchedState);

	if (m_CurrentRender->postJobTimeoutState.IsValid())
		responseStateData->SetObjectField(TEXT("timeout_state"), m_CurrentRender->postJobTimeoutState);

	// Feature plugins can append their own state fields to the response object.
	if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
	{
		LocalDelegates->OnBuildScreenshotStateNative.Broadcast(responseStateData, m_CurrentRender->world);
	}

	FString responseDataStr;
	TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&responseDataStr);
	FJsonSerializer::Serialize(responseStateData.ToSharedRef(), writer);

	auto responseDataAnsi = StringCast<ANSICHAR>(*responseDataStr);
	const char* responseDataChar = responseDataAnsi.Get();
	const int32 responseDataLen = strlen(responseDataChar);

	TArray64<uint8> responseDataBytes;
	responseDataBytes.Append(reinterpret_cast<const uint8*>(responseDataChar), responseDataLen);

	auto uidAnsi = StringCast<ANSICHAR>(*m_CurrentRender->uid);
	const char* uidChar = uidAnsi.Get();
	const int32 uidLen = strlen(uidChar);

	TArray64<uint8> uidStrBytes;
	uidStrBytes.Append(reinterpret_cast<const uint8*>(uidChar), uidLen);

	TArray64<uint8> payloadBytes = ImageBytes;
	const int32 responseLength = responseDataBytes.Num();

	// Wire format: response JSON length, response JSON, job UID, then encoded image bytes.
	payloadBytes.Insert(reinterpret_cast<const uint8*>(&responseLength), sizeof(int32), 0);
	payloadBytes.Insert(responseDataBytes, sizeof(int32));
	payloadBytes.Insert(uidStrBytes, sizeof(int32) + responseDataBytes.Num());

#ifdef SUPPORT_LEGACY_MESSAGES
	if (m_CurrentRender->isLegacyMessage)
		m_LauncherComms->SendLauncherMessageBinary("CAPTUREIMAGERESULT", payloadBytes);
	else
#endif //SUPPORT_LEGACY_MESSAGES
		m_LauncherComms->SendLauncherMessageBinary("CAPTUREMEDIARESULT", payloadBytes);

	if (Delegates)
	{
		const bool bIsPano = m_CurrentRender->type == ScreenshotType::EQUIRECT360;
		Delegates->OnContentGenerationFinished.Broadcast(bIsPano);
		Delegates->OnContentGenerationFinishedNative.Broadcast(bIsPano);
	}

	ZLJobTrace::JOBTRACE_TIMER_END("ImageDataServerResponse");
	ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
	m_CurrentRender.Reset();
	EditorRestoreLocalCapture2DODModeAfterJob();
}

void ZLScreenshot::FinalizePanoViewRestore()
{
	if (m_playerController != nullptr)
	{
		APlayerCameraManager* PlayerCamera = m_playerController->PlayerCameraManager;
		if (m_initialViewTarget != nullptr)
		{
			m_playerController->SetViewTarget(m_initialViewTarget);
		}
		if (PlayerCamera != nullptr)
		{
			PlayerCamera->UnlockFOV();
		}
	}

	if (m_panoViewTarget != nullptr)
	{
		m_panoViewTarget->Destroy();
		m_panoViewTarget = nullptr;
	}

	m_initialViewTarget = nullptr;
	m_playerController = nullptr;
}

void ZLScreenshot::SaveImageBytesToDisk(const TArray64<uint8>& ImageBytes, const TCHAR* LogContext)
{
	if (!m_CurrentRender.IsValid() || m_CurrentRender->path.IsEmpty())
		return;

	const FString dateCleaned = FDateTime::Now().ToString().Replace(TEXT("."), TEXT("_"));
	const FString filename = m_CurrentRender->uid + TEXT("_") + dateCleaned + TEXT(".") + m_CurrentRender->format;
	const FString outpath = FPaths::Combine(m_CurrentRender->path, filename);
	FFileHelper::SaveArrayToFile(ImageBytes, *outpath);
	UE_LOG(LogZLCloudPlugin, Display, TEXT("%s saved to: %s"), LogContext, *outpath);
}

void ZLScreenshot::EditorRestoreLocalCapture2DODModeAfterJob()
{
	// This function is the common success/failure cleanup and is deliberately safe after
	// m_CurrentRender has been reset.
	ResumeGameTime();
	CleanupCubemapGpuStitcher();
	FinalizePanoViewRestore();

	RestoreJobCVarOverrides();
	m_panoCubemapWarmupComplete = false;
	m_panoCubemapSweepActive = false;
	m_panoCubemapSweepStepIndex = 0;
	m_panoCubemapSweepFramesRemaining = 0;

	m_environmentSettleComplete = false;
	m_environmentSettleActive = false;
	m_environmentSettleFramesRemaining = 0;

#if WITH_EDITOR
	Set2DODMode(false);
#endif
}

bool ZLCloudPlugin::ZLScreenshot::LoadImageAsFColorArray(const FString& ImagePath, TArray<FColor>& OutColorData, int32& OutWidth, int32& OutHeight)
{
	TArray<uint8> CompressedData;
	if (!FFileHelper::LoadFileToArray(CompressedData, *ImagePath)) return false;

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(CompressedData.GetData(), CompressedData.Num());
	if (ImageFormat == EImageFormat::Invalid) return false;

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num())) return false;

	TArray64<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData)) return false;

	OutWidth = ImageWrapper->GetWidth();
	OutHeight = ImageWrapper->GetHeight();
	OutColorData.AddUninitialized(OutWidth * OutHeight);
	FMemory::Memcpy(OutColorData.GetData(), RawData.GetData(), RawData.Num());

	return true;
}

void ZLScreenshot::ApplyCVarOverridesDirect()
{
	// 2D calls this once; pano may have already applied during BeginPanoCaptureOverrides.
	ApplyMergedCaptureCVars();

	IConsoleManager& ConsoleMgr = IConsoleManager::Get();
	if (UsesCubemapPanoCapture(m_CurrentRender.Get()))
	{
		static const TCHAR* AntiAliasingCVarName = TEXT("r.AntiAliasingMethod");
		static const TCHAR* DLSSEnableCVarName = TEXT("r.NGX.DLSS.Enable");

		if (IConsoleVariable* AntiAliasingCVar = ConsoleMgr.FindConsoleVariable(AntiAliasingCVarName))
		{
			m_panoTiming.AppliedAntiAliasingMethod = AntiAliasingCVar->GetInt();
		}
		if (IConsoleVariable* DLSSEnableCVar = ConsoleMgr.FindConsoleVariable(DLSSEnableCVarName))
		{
			m_panoTiming.bAppliedDLSSEnabled = DLSSEnableCVar->GetInt() != 0;
		}

		if (IConsoleVariable* DelayCVar = ConsoleMgr.FindConsoleVariable(GHighResScreenshotDelayCVarName))
		{
			m_panoTiming.AppliedScreenshotDelay = DelayCVar->GetInt();
		}
		UE_LOG(LogZLCloudPlugin, Verbose,
			TEXT("ZLScreenshot: Cubemap GPU stitch %s=%d aa=%d dlss=%s overscan=%.2f faceCapture=%d centralFace=%u"),
			GHighResScreenshotDelayCVarName,
			m_panoTiming.AppliedScreenshotDelay,
			m_panoTiming.AppliedAntiAliasingMethod,
			m_panoTiming.bAppliedDLSSEnabled ? TEXT("on") : TEXT("off"),
			m_panoCubemapOverscan,
			GetCubemapCaptureSize(),
			m_faceSize);
	}
}

void ZLScreenshot::ClearCurrentRenderJob()
{
	RestoreStreamTransparentEnvironmentIfNeeded();
	ZLCloudPlugin::FZLCloudPluginModule::GetModule()->SetContentGenerationState(EZLContentGenerationState::None);
	m_CurrentRender.Reset();
}

void ZLScreenshot::SendImageFailureResponse(FString& errorMsg)
{
	if (m_CurrentRender.IsValid() && m_CurrentRender->bStreamImageCapture)
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Stream image capture failed: %s (requestId=%s)"),
			*errorMsg, *m_CurrentRender->uid);
		RestoreStreamTransparentEnvironmentIfNeeded();
		m_CurrentRender.Reset();
		EditorRestoreLocalCapture2DODModeAfterJob();
		return;
	}

	FString uid = m_CurrentRender->uid;
	TSharedPtr<FJsonObject> responseData = MakeShareable(new FJsonObject);
	responseData->SetStringField(TEXT("uid"), uid);

	if (m_CurrentRender->postJobCurrentState.IsValid())
		responseData->SetObjectField(TEXT("current_state"), m_CurrentRender->postJobCurrentState);

	if (m_CurrentRender->postJobTimeoutState.IsValid())
		responseData->SetObjectField(TEXT("timeout_state"), m_CurrentRender->postJobTimeoutState);

	if (m_CurrentRender->postJobUnmatchedState.IsValid())
	{
		responseData->SetObjectField(TEXT("unprocessed_state"), m_CurrentRender->postJobUnmatchedState);
		if (!m_CurrentRender->postJobTimeoutState.IsValid())
			errorMsg = "Render request rejected due to unprocessed state fields";
	}

	responseData->SetStringField(TEXT("error"), errorMsg);

	FString responseDataStr;

	TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&responseDataStr);

	FJsonSerializer::Serialize(responseData.ToSharedRef(), writer);

#ifdef SUPPORT_LEGACY_MESSAGES
	if (m_CurrentRender->isLegacyMessage)
		m_LauncherComms->SendLauncherMessage("CAPTUREIMAGEFAIL", responseDataStr);
	else
#endif //SUPPORT_LEGACY_MESSAGES
		m_LauncherComms->SendLauncherMessage("CAPTUREMEDIAFAIL", responseDataStr);
}

namespace
{
	static const TCHAR* GZLScreenshotTimingLogTag = TEXT("[ZLScreenshot]");

	void ReadCaptureRenderSettings(int32& OutScreenshotDelay, int32& OutAntiAliasingMethod, bool& bOutDLSSEnabled)
	{
		OutScreenshotDelay = 0;
		OutAntiAliasingMethod = -1;
		bOutDLSSEnabled = false;

		IConsoleManager& ConsoleMgr = IConsoleManager::Get();
		if (IConsoleVariable* DelayCVar = ConsoleMgr.FindConsoleVariable(TEXT("r.HighResScreenshotDelay")))
		{
			OutScreenshotDelay = DelayCVar->GetInt();
		}
		if (IConsoleVariable* AntiAliasingCVar = ConsoleMgr.FindConsoleVariable(TEXT("r.AntiAliasingMethod")))
		{
			OutAntiAliasingMethod = AntiAliasingCVar->GetInt();
		}
		if (IConsoleVariable* DLSSEnableCVar = ConsoleMgr.FindConsoleVariable(TEXT("r.NGX.DLSS.Enable")))
		{
			bOutDLSSEnabled = DLSSEnableCVar->GetInt() != 0;
		}
	}

	/**
	 * Measurements used to spot the rare capture that comes back with the scene rendered into a
	 * subset of columns and the rest washed out, i.e. a frame-wide vertical comb.
	 *
	 * ColumnProfileHighPass is the discriminating one. Averaging luma down the full frame height
	 * flattens natural content into a gentle curve, but a comb survives the average because its
	 * phase is identical on every row. Reference renders score under 0.2; the known corrupted
	 * captures score over 30.
	 */
	struct FZLFrameStats
	{
		double MeanAbsDeltaX = 0.0;
		double MeanAbsDeltaY = 0.0;
		double DeltaRatio = 0.0;
		double ColumnProfileHighPass = 0.0;
		/** Fraction of R/G/B samples at or above 254, i.e. how much of the frame is clipped. */
		double SaturatedFraction = 0.0;
	};

	/** Channel level at or above which a sample counts as clipped. */
	constexpr uint8 GSaturatedLevel = 254;

	FZLFrameStats MeasureFrameStats(const FColor* Pixels, int32 SizeX, int32 SizeY)
	{
		FZLFrameStats Stats;
		if (Pixels == nullptr || SizeX < 3 || SizeY < 2)
		{
			return Stats;
		}

		// Every column has to be visited to see a 1px comb, but sampling rows is plenty.
		constexpr int32 RowStep = 4;

		auto Luma = [](const FColor& C) -> int32
		{
			return (C.R * 77 + C.G * 150 + C.B * 29) >> 8;
		};

		TArray<int64> ColumnLumaSums;
		ColumnLumaSums.AddZeroed(SizeX);

		int64 DeltaXSum = 0;
		int64 DeltaXCount = 0;
		int64 DeltaYSum = 0;
		int64 DeltaYCount = 0;
		int32 SampledRows = 0;
		int64 SaturatedSamples = 0;

		auto CountSaturated = [](const FColor& C) -> int32
		{
			return (C.R >= GSaturatedLevel ? 1 : 0)
				+ (C.G >= GSaturatedLevel ? 1 : 0)
				+ (C.B >= GSaturatedLevel ? 1 : 0);
		};

		for (int32 Y = 0; Y + 1 < SizeY; Y += RowStep)
		{
			const FColor* Row = Pixels + (int64)Y * (int64)SizeX;
			const FColor* NextRow = Row + SizeX;
			++SampledRows;

			int32 PrevLuma = Luma(Row[0]);
			ColumnLumaSums[0] += PrevLuma;
			DeltaYSum += FMath::Abs(PrevLuma - Luma(NextRow[0]));
			++DeltaYCount;
			SaturatedSamples += CountSaturated(Row[0]);

			for (int32 X = 1; X < SizeX; ++X)
			{
				const int32 CurLuma = Luma(Row[X]);
				ColumnLumaSums[X] += CurLuma;

				DeltaXSum += FMath::Abs(CurLuma - PrevLuma);
				++DeltaXCount;

				DeltaYSum += FMath::Abs(CurLuma - Luma(NextRow[X]));
				++DeltaYCount;

				SaturatedSamples += CountSaturated(Row[X]);

				PrevLuma = CurLuma;
			}
		}

		if (SampledRows == 0 || DeltaXCount == 0 || DeltaYCount == 0)
		{
			return Stats;
		}

		Stats.MeanAbsDeltaX = (double)DeltaXSum / (double)DeltaXCount;
		Stats.MeanAbsDeltaY = (double)DeltaYSum / (double)DeltaYCount;
		Stats.DeltaRatio = (Stats.MeanAbsDeltaX + 1.0) / (Stats.MeanAbsDeltaY + 1.0);
		Stats.SaturatedFraction = (double)SaturatedSamples / ((double)SampledRows * (double)SizeX * 3.0);

		double HighPassSum = 0.0;
		for (int32 X = 1; X + 1 < SizeX; ++X)
		{
			const double Prev = (double)ColumnLumaSums[X - 1] / (double)SampledRows;
			const double Cur = (double)ColumnLumaSums[X] / (double)SampledRows;
			const double Next = (double)ColumnLumaSums[X + 1] / (double)SampledRows;
			HighPassSum += FMath::Abs(Cur - 0.5 * (Prev + Next));
		}
		Stats.ColumnProfileHighPass = HighPassSum / (double)(SizeX - 2);

		return Stats;
	}

	/**
	 * Thresholds sit between reference 3000x1080 renders and the two known corrupted captures.
	 * Worst real content measured 0.17 / 2.6 / 1.8; the corrupted capture measured 31.6 / 37.5 / 3.6.
	 * All three have to trip, so content that is legitimately full of vertical edges cannot be
	 * rejected on one metric alone.
	 */
	constexpr double GCombColumnProfileHighPassThreshold = 4.0;
	constexpr double GCombMeanAbsDeltaXThreshold = 8.0;
	constexpr double GCombDeltaRatioThreshold = 2.0;

	bool IsFrameCombed(const FZLFrameStats& Stats)
	{
		return Stats.ColumnProfileHighPass >= GCombColumnProfileHighPassThreshold
			&& Stats.MeanAbsDeltaX >= GCombMeanAbsDeltaXThreshold
			&& Stats.DeltaRatio >= GCombDeltaRatioThreshold;
	}

	/**
	 * Washed-out captures clip a large part of the frame. Across 895 reference renders the worst
	 * measured 0.0018, while the mildest of ten corrupted captures measured 0.1467, so this sits
	 * roughly a decade clear of both. It is deliberately only a *suspicion*: a white cyclorama or
	 * a transparent-background job clips just as hard, so nothing is rejected on this alone.
	 */
	constexpr double GBlownOutSaturatedFractionThreshold = 0.02;

	/**
	 * A re-capture of legitimately bright content clips by the same amount every time, whereas the
	 * corruption varies frame to frame. Only a measurable drop is treated as proof the earlier
	 * frame was bad; anything stable is accepted as real content.
	 */
	constexpr double GBlownOutStableRelativeTolerance = 0.10;

	bool IsFrameBlownOut(const FZLFrameStats& Stats)
	{
		return Stats.SaturatedFraction >= GBlownOutSaturatedFractionThreshold;
	}
}

void ZLScreenshot::LogDefault2DViewportTimingSummary(int32 Width, int32 Height, double CaptureWallSeconds, double FinalEncodeSeconds) const
{
	int32 ScreenshotDelay = 0;
	int32 AntiAliasingMethod = -1;
	bool bDLSSEnabled = false;
	ReadCaptureRenderSettings(ScreenshotDelay, AntiAliasingMethod, bDLSSEnabled);

	const double PostCaptureSeconds = FinalEncodeSeconds;

	UE_LOG(LogZLCloudPlugin, Display,
		TEXT("%s method=viewport_2d size=%dx%d screenshot_delay=%d aa=%d dlss=%s | capture_wall=%.2fs | final_encode=%.2fs | post_capture=%.2fs"),
		GZLScreenshotTimingLogTag,
		Width, Height,
		ScreenshotDelay,
		AntiAliasingMethod,
		bDLSSEnabled ? TEXT("on") : TEXT("off"),
		CaptureWallSeconds,
		FinalEncodeSeconds,
		PostCaptureSeconds);

	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotTimingMethod"), MakeShared<FJsonValueString>(TEXT("viewport_2d")));
	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotTimingCaptureWallSeconds"), MakeShared<FJsonValueNumber>(CaptureWallSeconds));
	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotTimingFinalEncodeSeconds"), MakeShared<FJsonValueNumber>(FinalEncodeSeconds));
}

void ZLScreenshot::LogDefault2DMRQTimingSummary(
	int32 Width,
	int32 Height,
	bool bVideoCapture,
	const FString& VideoFormat,
	double CaptureWallSeconds,
	double FinalEncodeSeconds) const
{
	const double PostCaptureSeconds = FinalEncodeSeconds;

	if (bVideoCapture)
	{
		UE_LOG(LogZLCloudPlugin, Display,
			TEXT("%s method=mrq_2d_video size=%dx%d video=%s | capture_wall=%.2fs | final_encode=%.2fs | post_capture=%.2fs"),
			GZLScreenshotTimingLogTag,
			Width, Height,
			VideoFormat.IsEmpty() ? TEXT("unknown") : *VideoFormat,
			CaptureWallSeconds,
			FinalEncodeSeconds,
			PostCaptureSeconds);
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Display,
			TEXT("%s method=mrq_2d size=%dx%d | capture_wall=%.2fs | final_encode=%.2fs | post_capture=%.2fs"),
			GZLScreenshotTimingLogTag,
			Width, Height,
			CaptureWallSeconds,
			FinalEncodeSeconds,
			PostCaptureSeconds);
	}

	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotTimingMethod"), MakeShared<FJsonValueString>(bVideoCapture ? TEXT("mrq_2d_video") : TEXT("mrq_2d")));
	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotTimingCaptureWallSeconds"), MakeShared<FJsonValueNumber>(CaptureWallSeconds));
	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotTimingFinalEncodeSeconds"), MakeShared<FJsonValueNumber>(FinalEncodeSeconds));
}

void ZLScreenshot::LogPanoCubemapTimingSummary() const
{
	const double CaptureWallSeconds = m_panoTiming.CaptureWallSeconds();
	const double FaceAvgMs = m_panoTiming.CompletedFaceCount > 0
		? (m_panoTiming.FaceRequestToCallbackAccumSeconds * 1000.0) / static_cast<double>(m_panoTiming.CompletedFaceCount)
		: 0.0;
	const double PostProcessSeconds = m_panoTiming.CompositeReadbackSeconds
		+ m_panoTiming.FinalEncodeSeconds;
	const int32 CaptureFaceSize = GetCubemapCaptureSize();

	UE_LOG(LogZLCloudPlugin, Display,
		TEXT("%s method=cubemap_gpu equirect=%dx%d faces=%d face=%dx%d central_face=%u overscan=%.2f screenshot_delay=%d aa=%d dlss=%s | capture_wall=%.2fs face_request_to_callback_avg=%.1fms face_request_to_callback_sum=%.2fs | gpu_composite_readback=%.2fs | final_encode=%.2fs | post_capture=%.2fs"),
		GZLScreenshotTimingLogTag,
		m_CurrentRender.IsValid() ? m_CurrentRender->width : 0,
		m_CurrentRender.IsValid() ? m_CurrentRender->height : 0,
		m_panoTiming.CompletedFaceCount,
		CaptureFaceSize,
		CaptureFaceSize,
		m_faceSize,
		m_panoCubemapOverscan,
		m_panoTiming.AppliedScreenshotDelay,
		m_panoTiming.AppliedAntiAliasingMethod,
		m_panoTiming.bAppliedDLSSEnabled ? TEXT("on") : TEXT("off"),
		CaptureWallSeconds,
		FaceAvgMs,
		m_panoTiming.FaceRequestToCallbackAccumSeconds,
		m_panoTiming.CompositeReadbackSeconds,
		m_panoTiming.FinalEncodeSeconds,
		PostProcessSeconds);

	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("panoTimingMethod"), MakeShared<FJsonValueString>(TEXT("cubemap_gpu")));
	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("panoTimingScreenshotDelay"), MakeShared<FJsonValueNumber>(m_panoTiming.AppliedScreenshotDelay));
	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("panoTimingCaptureWallSeconds"), MakeShared<FJsonValueNumber>(CaptureWallSeconds));
	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("panoTimingFaceAvgMs"), MakeShared<FJsonValueNumber>(FaceAvgMs));
	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("panoTimingFaceRequestToCallbackSumSeconds"), MakeShared<FJsonValueNumber>(m_panoTiming.FaceRequestToCallbackAccumSeconds));
	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("panoTimingGpuCompositeReadbackSeconds"), MakeShared<FJsonValueNumber>(m_panoTiming.CompositeReadbackSeconds));
	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("panoTimingFinalEncodeSeconds"), MakeShared<FJsonValueNumber>(m_panoTiming.FinalEncodeSeconds));
	ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("panoTimingOverscan"), MakeShared<FJsonValueNumber>(m_panoCubemapOverscan));
}

void ZLScreenshot::PauseGameTime()
{
	if (!m_gameTimePaused && m_CurrentRender.IsValid() && !m_CurrentRender->useMRQPipeline)
	{
		if (AWorldSettings* WorldSettings = m_CurrentRender->world->GetWorldSettings())
		{
			m_cacheTimeDilation = WorldSettings->TimeDilation;
			m_timeDilatedWorld = m_CurrentRender->world;
			m_gameTimePaused = true;
			WorldSettings->TimeDilation = 0.0f;
			UE_LOG(LogZLCloudPlugin, Verbose, TEXT("Game Time Paused. TimeDilation set to 0.0."));
		}
	}
}

void ZLScreenshot::ResumeGameTime()
{
	if (m_gameTimePaused)
	{
		if (UWorld* World = m_timeDilatedWorld.Get())
		{
			if (AWorldSettings* WorldSettings = World->GetWorldSettings())
			{
				WorldSettings->TimeDilation = m_cacheTimeDilation;
				UE_LOG(LogZLCloudPlugin, Verbose, TEXT("Game Time Resumed"));
			}
		}

		m_timeDilatedWorld.Reset();
		m_gameTimePaused = false;
	}
}

void ZLScreenshot::SetCubemapPanoRotation(const FRotator& Rot)
{
	if (m_panoViewTarget != nullptr)
	{
		m_panoViewTarget->SetActorRotation(Rot);
	}
}

void ZLScreenshot::ApplyFixedPanoExposure(UCameraComponent* Camera)
{
	if (Camera == nullptr)
	{
		return;
	}

	// Inherit the live view's fully-blended post-process. The 2D TakeHighResScreenShot
	// path renders the active view, so it already gets the active camera's grading and
	// exposure. The pano path spawns its own bare camera and takes over the view target,
	// which would otherwise discard all of that (a bare camera has no exposure -> black).
	// Copy the previous active camera's POV settings before we own the view.
	if (m_playerController != nullptr && m_playerController->PlayerCameraManager != nullptr)
	{
		Camera->PostProcessSettings =
			m_playerController->PlayerCameraManager->GetLastFrameCameraCacheView().PostProcessSettings;
	}

	FPostProcessSettings& PP = Camera->PostProcessSettings;

	// Exposure itself is left exactly as the inherited post-process specifies. To get a
	// seamless pano, lock exposure on the camera/level (Manual, or AutoExposureMinBrightness
	// == AutoExposureMaxBrightness): UE then forces a fixed target every frame, so all faces
	// share one exposure that is independent of each face's (overscanned, wide-FOV) content.
	// If exposure is left on Auto with a Min<Max range, each face re-meters its own content
	// and the faces will differ.

	// Local exposure is a spatially-varying tonemap driven by local scene luminance; it also
	// differs per face and is not covered by an exposure lock. Neutralize it so it cannot
	// reintroduce per-face tonemap seams (contrast scale 1.0 == off).
	PP.bOverride_LocalExposureHighlightContrastScale = true;
	PP.LocalExposureHighlightContrastScale = 1.0f;
	PP.bOverride_LocalExposureShadowContrastScale = true;
	PP.LocalExposureShadowContrastScale = 1.0f;
}

void ZLScreenshot::ApplyCubemapPanoView(const FRotator& Rot, float FovDegrees, bool bCameraCut)
{
	if (m_panoViewTarget == nullptr)
	{
		return;
	}

	m_panoViewTarget->SetActorRotation(Rot);
	if (UCameraComponent* PanoCam = m_panoViewTarget->GetCameraComponent())
	{
		PanoCam->SetWorldRotation(Rot);
		PanoCam->SetFieldOfView(FovDegrees);
	}
	if (m_playerController != nullptr)
	{
		m_playerController->SetControlRotation(Rot);
		if (APlayerCameraManager* PlayerCamera = m_playerController->PlayerCameraManager)
		{
			PlayerCamera->SetFOV(FovDegrees);
			if (bCameraCut)
			{
				PlayerCamera->SetGameCameraCutThisFrame();
			}
		}
	}
}

bool ZLScreenshot::TickCubemapPanoSweep()
{
	if (!m_CurrentRender.IsValid() || m_panoViewTarget == nullptr)
	{
		return false;
	}

	const int32 FramesPerOrientation = (m_CurrentRender->panoCubemapWarmupFrames > 0)
		? m_CurrentRender->panoCubemapWarmupFrames
		: s_panoCubemapSweepFramesPerOrientation;

	auto ApplySweepStep = [&](int32 StepIndex, bool bCameraCut)
	{
		const FQuat Rot = (StepIndex < 6)
			? m_startRotation * m_faceRotation[StepIndex]
			: m_startRotation;
		ApplyCubemapPanoView(Rot.Rotator(), m_panoCubemapCaptureFovDegrees, bCameraCut);
	};

	if (!m_panoCubemapSweepActive)
	{
		m_panoCubemapSweepStepIndex = 0;
		m_panoCubemapSweepFramesRemaining = FramesPerOrientation;
		m_panoCubemapSweepActive = true;
		ApplySweepStep(0, true);
		UE_LOG(LogZLCloudPlugin, Display,
			TEXT("[Pano] cubemap warmup sweep started (%d frames per orientation, fov=%.1f)"),
			FramesPerOrientation,
			m_panoCubemapCaptureFovDegrees);
		return false;
	}

	if (--m_panoCubemapSweepFramesRemaining > 0)
	{
		return false;
	}

	++m_panoCubemapSweepStepIndex;
	if (m_panoCubemapSweepStepIndex > 6)
	{
		ApplySweepStep(6, true);
		m_panoCubemapWarmupComplete = true;
		m_panoCubemapSweepActive = false;
		UE_LOG(LogZLCloudPlugin, Display,
			TEXT("[Pano] cubemap warmup sweep complete (%d frames x 7 orientations)"),
			FramesPerOrientation);
		return true;
	}

	ApplySweepStep(m_panoCubemapSweepStepIndex, true);
	m_panoCubemapSweepFramesRemaining = FramesPerOrientation;
	return false;
}

bool ZLScreenshot::TickEnvironmentChangeSettle()
{
	if (m_environmentSettleComplete)
	{
		return true;
	}

	if (!m_CurrentRender.IsValid() || !m_CurrentRender->environmentChangedInStateRequest || m_CurrentRender->useMRQPipeline)
	{
		m_environmentSettleComplete = true;
		return true;
	}

	const UZLCloudPluginSettings* Settings = GetDefault<UZLCloudPluginSettings>();
	const int32 SettleFrames = Settings ? Settings->environmentSettleFrames : 0;
	if (SettleFrames <= 0)
	{
		m_environmentSettleComplete = true;
		return true;
	}

	if (!m_environmentSettleActive)
	{
		m_environmentSettleActive = true;
		m_environmentSettleFramesRemaining = SettleFrames;

		if (UWorld* World = m_CurrentRender->world)
		{
			World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		}

		const int32 PendingStreamingRequests = IStreamingManager::Get().StreamAllResources(0.0f);

		ZLJobTrace::JOBTRACE_ADD_DATA("environmentChangeSettleFrames", MakeShared<FJsonValueNumber>(SettleFrames));

		UE_LOG(LogZLCloudPlugin, Display,
			TEXT("ZLScreenshot: environment change settle - streaming flushed (%d request(s) still pending), waiting %d frame(s) before capture"),
			PendingStreamingRequests,
			SettleFrames);

		return false;
	}

	if (--m_environmentSettleFramesRemaining > 0)
	{
		return false;
	}

	m_environmentSettleActive = false;
	m_environmentSettleComplete = true;

	UE_LOG(LogZLCloudPlugin, Display, TEXT("ZLScreenshot: environment change settle complete (%d frame(s))"), SettleFrames);

	return true;
}

void ZLScreenshot::OnScreenshotComplete(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData)
{
	check(IsInGameThread());

	if (m_CurrentRender.IsValid())
	{
		if (m_CurrentRender->useMRQPipeline)
			return;

		// FImageView and the pano stitcher both index InSizeX*InSizeY pixels, so a short readback
		// would be an out-of-bounds read rather than a merely wrong-looking image.
		const int64 ExpectedPixelCount = (int64)InSizeX * (int64)InSizeY;
		if (InSizeX <= 0 || InSizeY <= 0 || (int64)InImageData.Num() != ExpectedPixelCount)
		{
			UE_LOG(LogZLCloudPlugin, Error,
				TEXT("Screenshot buffer size mismatch: received %lld pixel(s), expected %lld (%ix%i)"),
				(int64)InImageData.Num(), ExpectedPixelCount, InSizeX, InSizeY);

			ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotBufferPixelCount"), MakeShared<FJsonValueNumber>((double)InImageData.Num()));
			ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotBufferExpectedPixelCount"), MakeShared<FJsonValueNumber>((double)ExpectedPixelCount));

			FString errorMsg = FString::Printf(
				TEXT("Screenshot buffer size mismatch: received %lld pixel(s), expected %lld (%ix%i)."),
				(int64)InImageData.Num(), ExpectedPixelCount, InSizeX, InSizeY);

			ResumeGameTime();
			CleanupCubemapGpuStitcher();
			FinalizePanoViewRestore();
			ZLScreenshot::SendImageFailureResponse(errorMsg);
			ClearCurrentRenderJob();
			EditorRestoreLocalCapture2DODModeAfterJob();
			ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
			return;
		}

		double capEndTime = FPlatformTime::Seconds();
		double elapsed = capEndTime - m_CurrentRender->captureStartTime;

		TArray64<uint8> imageBytes;

		if (m_CurrentRender->type == ScreenshotType::DEFAULT2D)
		{
			const double CaptureWallSeconds = elapsed;

			const FZLFrameStats FrameStats = MeasureFrameStats(InImageData.GetData(), InSizeX, InSizeY);

			// Clipping alone never rejects a frame, because a white cyclorama or a
			// transparent-background job clips just as hard as a corrupt one. A re-capture is the
			// arbiter: real content clips by the same amount again, corruption does not.
			if (IsFrameBlownOut(FrameStats))
			{
				const double PreviousSaturated = m_CurrentRender->lastSaturatedFraction;
				const bool bHasPrevious = PreviousSaturated >= 0.0;
				const bool bStableAcrossCaptures = bHasPrevious
					&& FMath::Abs(FrameStats.SaturatedFraction - PreviousSaturated)
						<= GBlownOutStableRelativeTolerance * FMath::Max(PreviousSaturated, KINDA_SMALL_NUMBER);

				if (bStableAcrossCaptures)
				{
					UE_LOG(LogZLCloudPlugin, Verbose,
						TEXT("%s %ix%i capture clips %.2f%% of samples but re-capture matched (%.2f%%), treating as real content"),
						GZLScreenshotTimingLogTag,
						InSizeX, InSizeY,
						100.0 * FrameStats.SaturatedFraction,
						100.0 * PreviousSaturated);
				}
				else if (m_CurrentRender->blownOutRejectRetryCount < s_maxBlownOutRejectRetries)
				{
					m_CurrentRender->blownOutRejectRetryCount++;
					m_CurrentRender->lastSaturatedFraction = FrameStats.SaturatedFraction;

					UE_LOG(LogZLCloudPlugin, Warning,
						TEXT("%s %ix%i capture clips %.2f%% of samples, re-capturing to confirm (attempt %d of %d)"),
						GZLScreenshotTimingLogTag,
						InSizeX, InSizeY,
						100.0 * FrameStats.SaturatedFraction,
						m_CurrentRender->blownOutRejectRetryCount,
						s_maxBlownOutRejectRetries);

					ZLJobTrace::JOBTRACE_TIMER_END("Render");
					m_CurrentRender->jobCaptureStarted = false;
					return;
				}
				else
				{
					// Never strand a job on a heuristic. The measurements go into the trace so the
					// image can be identified after the fact.
					UE_LOG(LogZLCloudPlugin, Error,
						TEXT("%s %ix%i capture still clips %.2f%% of samples after %d re-capture(s), sending image anyway"),
						GZLScreenshotTimingLogTag,
						InSizeX, InSizeY,
						100.0 * FrameStats.SaturatedFraction,
						s_maxBlownOutRejectRetries);
				}
			}

			if (IsFrameCombed(FrameStats))
			{
				if (m_CurrentRender->combRejectRetryCount < s_maxCombRejectRetries)
				{
					// Re-issue instead of encoding: the comb is already in the FColor buffer, so
					// no encoder setting can recover it. Game time stays paused, so the retry
					// renders the same scene.
					m_CurrentRender->combRejectRetryCount++;

					UE_LOG(LogZLCloudPlugin, Warning,
						TEXT("%s vertical comb detected in %ix%i capture (colProfileHP=%.2f dx=%.2f dy=%.2f dx/dy=%.2f), re-capturing (attempt %d of %d)"),
						GZLScreenshotTimingLogTag,
						InSizeX, InSizeY,
						FrameStats.ColumnProfileHighPass,
						FrameStats.MeanAbsDeltaX,
						FrameStats.MeanAbsDeltaY,
						FrameStats.DeltaRatio,
						m_CurrentRender->combRejectRetryCount,
						s_maxCombRejectRetries);

					ZLJobTrace::JOBTRACE_TIMER_END("Render");
					m_CurrentRender->jobCaptureStarted = false;
					return;
				}

				// Out of retries. A false positive must never strand a job, so send the image and
				// leave the measurements in the trace to identify it afterwards.
				UE_LOG(LogZLCloudPlugin, Error,
					TEXT("%s vertical comb still present after %d re-capture(s) (colProfileHP=%.2f dx=%.2f dy=%.2f dx/dy=%.2f), sending image anyway"),
					GZLScreenshotTimingLogTag,
					s_maxCombRejectRetries,
					FrameStats.ColumnProfileHighPass,
					FrameStats.MeanAbsDeltaX,
					FrameStats.MeanAbsDeltaY,
					FrameStats.DeltaRatio);
			}

			ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotCombColumnProfileHighPass"), MakeShared<FJsonValueNumber>(FrameStats.ColumnProfileHighPass));
			ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotCombDeltaRatio"), MakeShared<FJsonValueNumber>(FrameStats.DeltaRatio));
			ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotSaturatedFraction"), MakeShared<FJsonValueNumber>(FrameStats.SaturatedFraction));
			ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotBlownOutRetryCount"), MakeShared<FJsonValueNumber>((double)m_CurrentRender->blownOutRejectRetryCount));
			ZLJobTrace::JOBTRACE_ADD_DATA(TEXT("screenshotCombRetryCount"), MakeShared<FJsonValueNumber>((double)m_CurrentRender->combRejectRetryCount));

			ResumeGameTime();
			ZLJobTrace::JOBTRACE_TIMER_END("Render");
			ZLJobTrace::JOBTRACE_TIMER_START("Encode");
			//Render is done, allow next request
			const double EncodeStartSeconds = FPlatformTime::Seconds();
			FImageView image((const FColor*)InImageData.GetData(), InSizeX, InSizeY);

			if (!FImageUtils::CompressImage(imageBytes, *m_CurrentRender->format, image))
			{
				UE_LOG(LogZLCloudPlugin, Error, TEXT("CompressImage failed: %ix%i"), InSizeX, InSizeY);
				FString errorMsg = "CompressImage failed.";
				ZLScreenshot::SendImageFailureResponse(errorMsg);
				ClearCurrentRenderJob();
				EditorRestoreLocalCapture2DODModeAfterJob();
				ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
				return;
			}

			SaveImageBytesToDisk(imageBytes, TEXT("Screen shot"));

			const double FinalEncodeSeconds = FPlatformTime::Seconds() - EncodeStartSeconds;
			LogDefault2DViewportTimingSummary(InSizeX, InSizeY, CaptureWallSeconds, FinalEncodeSeconds);
			ZLJobTrace::JOBTRACE_TIMER_END("Encode");
			if (m_CurrentRender->bStreamImageCapture)
			{
				SendStreamImageCaptureResponse(imageBytes);
			}
			else
			{
				SendCapturedImageResponse(imageBytes, FIntPoint(InSizeX, InSizeY));
			}
			return;
		}
		else if (m_CurrentRender->type == ScreenshotType::EQUIRECT360
			&& UsesCubemapPanoCapture(m_CurrentRender.Get())
			&& m_panoCubemapGpuStitcher.IsValid())
		{
			const int32 CompletedCaptureIndex = m_CurrentRender->faceID - 1;
			m_CurrentRender->lastFaceCompletedID = CompletedCaptureIndex;

			FString FormattedString = FString::Printf(TEXT("CaptureFaceID%d"), CompletedCaptureIndex);
			ZLJobTrace::JOBTRACE_TIMER_END(FormattedString);

			if (m_panoFaceRequestTimeSeconds > 0.0)
			{
				m_panoTiming.FaceRequestToCallbackAccumSeconds += capEndTime - m_panoFaceRequestTimeSeconds;
				m_panoFaceRequestTimeSeconds = 0.0;
			}
			m_panoTiming.CompletedFaceCount++;

			// The face is captured with an overscanned FOV (2*atan(1+overscan)). Upload the
			// full overscanned image and let the composite shader map it via OverscanTan —
			// cropping to the central 90° would discard the very margin the shader expects,
			// producing wrong faces whenever padding is enabled.
			m_panoCubemapGpuStitcher->UploadFace(CompletedCaptureIndex, InImageData, InSizeX, InSizeY, m_panoCubemapFaceRigRot[CompletedCaptureIndex]);

			// Rotate to the next face orientation only after this face has finished capturing.
			// TakeHighResScreenShot is deferred (~r.HighResScreenshotDelay frames); rotating on
			// issue smears TSR/Lumen across the accumulation window (motion blur / noise).
			if (CompletedCaptureIndex < 5)
			{
				SetCubemapPanoRotation((m_startRotation * m_faceRotation[CompletedCaptureIndex]).Rotator());
			}

			if (m_CurrentRender->faceID < 6)
			{
				return;
			}

			m_panoTiming.CaptureWallEndSeconds = capEndTime;
			if (ZLJobTrace::JOBTRACE_IS_CURRENTLY_TIMING("Render"))
			{
				ZLJobTrace::JOBTRACE_TIMER_END("Render");
			}

			TArray<FColor> EquirectColors;
			int32 OutW = 0;
			int32 OutH = 0;
			double CompositeSeconds = 0.0;
			if (!m_panoCubemapGpuStitcher->CompositeToEquirect(EquirectColors, OutW, OutH, CompositeSeconds))
			{
				FString errorMsg = TEXT("Cubemap GPU composite failed.");
				ZLScreenshot::SendImageFailureResponse(errorMsg);
				CleanupCubemapGpuStitcher();
				m_CurrentRender.Reset();
				EditorRestoreLocalCapture2DODModeAfterJob();
				ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
				return;
			}

			m_panoTiming.CompositeReadbackSeconds = CompositeSeconds;
			CleanupCubemapGpuStitcher();
			ResumeGameTime();
			ZLJobTrace::JOBTRACE_TIMER_START("Encode");

			const double EncodeStartSeconds = FPlatformTime::Seconds();
			FImageView EquirectImage((const FColor*)EquirectColors.GetData(), OutW, OutH);
			if (!FImageUtils::CompressImage(imageBytes, *m_CurrentRender->format, EquirectImage))
			{
				UE_LOG(LogZLCloudPlugin, Error, TEXT("CompressImage failed: %ix%i"), OutW, OutH);
				FString errorMsg = TEXT("CompressImage failed.");
				ZLScreenshot::SendImageFailureResponse(errorMsg);
				m_CurrentRender.Reset();
				EditorRestoreLocalCapture2DODModeAfterJob();
				ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
				return;
			}

			SaveImageBytesToDisk(imageBytes, TEXT("GPU-composite pano"));

			m_panoTiming.FinalEncodeSeconds = FPlatformTime::Seconds() - EncodeStartSeconds;
			LogPanoCubemapTimingSummary();
			ZLJobTrace::JOBTRACE_TIMER_END("Encode");
			FinalizePanoViewRestore();
			if (m_CurrentRender->bStreamImageCapture)
			{
				SendStreamImageCaptureResponse(imageBytes);
			}
			else
			{
				SendCapturedImageResponse(imageBytes, FIntPoint(OutW, OutH));
			}
			return;
		}
	}
}

#if UNREAL_5_3_OR_NEWER
/** Deletes the MRQ frame images written for a job. Returns the number that could not be removed. */
static int32 DeleteMRQJobFrameFiles(const FString& OutputDir, const FString& Uid, const FString& FrameExtension)
{
	FString SearchExtension = FrameExtension;
	if (SearchExtension.Equals(TEXT("jpg"), ESearchCase::IgnoreCase) ||
		SearchExtension.Equals(TEXT("jpeg"), ESearchCase::IgnoreCase))
	{
		SearchExtension = TEXT("jp*g");
	}

	const FString FrameWildcard = FPaths::Combine(OutputDir, Uid + TEXT(".*.") + SearchExtension);
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *FrameWildcard, true, false);

	int32 FailedDeleteCount = 0;
	for (const FString& FileName : FoundFiles)
	{
		const FString FilePath = FPaths::Combine(OutputDir, FileName);
		if (!IFileManager::Get().Delete(*FilePath, false, false, true))
		{
			++FailedDeleteCount;
		}
	}

	return FailedDeleteCount;
}

void ZLScreenshot::OnMoviePipelineFinished(FMoviePipelineOutputData Results)
{
	UE_LOG(LogZLCloudPlugin, Display, TEXT("OnMoviePipelineFinished called. Success=%s CurrentRenderValid=%s ResultsJob=%s"),
		Results.bSuccess ? TEXT("true") : TEXT("false"),
		m_CurrentRender.IsValid() ? TEXT("true") : TEXT("false"),
		(Results.Job != nullptr) ? *Results.Job->JobName : TEXT("<null>"));

	if (Results.bSuccess && m_CurrentRender.IsValid() && m_CurrentRender->useMRQPipeline && !m_CurrentRender->videoFormat.IsEmpty())
	{
		const FString progress = TEXT("100");
		m_LauncherComms->SendLauncherMessage("CAPTUREMEDIAPROGRESS", progress);
		GMRQVideoProgressState.LastPercentSent = 100;
	}

	if (bCreatedCamera && CameraActor)
	{
		CameraActor->Destroy();
		CameraActor = nullptr;
	}

	if (m_CurrentRender.IsValid())
	{
		if (Results.Job != nullptr && m_CurrentRender->ActiveRenderJob != nullptr && Results.Job != m_CurrentRender->ActiveRenderJob)
		{
			UE_LOG(LogZLCloudPlugin, Warning, TEXT("OnMoviePipelineFinished received unexpected job. Expected=%s Actual=%s"),
				*m_CurrentRender->ActiveRenderJob->JobName,
				*Results.Job->JobName);
		}

		if (m_CurrentRender->transparent)
			RestoreTransparentGlassMaterials(m_CurrentRender.Get());
		UE_LOG(LogZLCloudPlugin, Display, TEXT("OnMoviePipelineFinished invoking RevertSettings for render uid=%s"), *m_CurrentRender->uid);
		m_CurrentRender->RevertSettings();

		const bool isVideoCapture = !m_CurrentRender->videoFormat.IsEmpty();
		if (isVideoCapture && m_CurrentRender->world && m_CurrentRender->videoCaptureSequenceAsset)
		{
			TArray<TWeakObjectPtr<ALevelSequenceActor>> ActorsToDestroy;
			for (TActorIterator<ALevelSequenceActor> It(m_CurrentRender->world); It; ++It)
			{
				ALevelSequenceActor* SequenceActor = *It;
				if (SequenceActor && SequenceActor->GetSequence() == m_CurrentRender->videoCaptureSequenceAsset)
				{
					ActorsToDestroy.Add(SequenceActor);
				}
			}

			for (const TWeakObjectPtr<ALevelSequenceActor>& SequenceActorPtr : ActorsToDestroy)
			{
				if (ALevelSequenceActor* SequenceActor = SequenceActorPtr.Get())
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Destroying LevelSequenceActor after video job. Actor=%s Sequence=%s"),
						*SequenceActor->GetPathName(),
						*m_CurrentRender->videoCaptureSequenceAsset->GetPathName());
					SequenceActor->Destroy();
				}
			}

			UE_LOG(LogZLCloudPlugin, Display, TEXT("Destroyed %d LevelSequenceActor instance(s) referencing completed video sequence %s."),
				ActorsToDestroy.Num(),
				*m_CurrentRender->videoCaptureSequenceAsset->GetPathName());
		}
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("OnMoviePipelineFinished had no active render. Nothing to restore."));
	}

	if (Results.bSuccess && m_CurrentRender.IsValid())
	{
		FString Extension = m_CurrentRender->format;
		if (Extension.IsEmpty() || !(Extension.Equals("png", ESearchCase::IgnoreCase) || Extension.Equals("jpg", ESearchCase::IgnoreCase)))
		{
			Extension = TEXT("png");
		}

		if (Extension.Equals("jpg", ESearchCase::IgnoreCase))
		{
			Extension = TEXT("jpeg");
		}

		TArray64<uint8> imageBytes;

		UMoviePipelineOutputSetting* OutputSetting = Results.Job->GetConfiguration()->FindSetting<UMoviePipelineOutputSetting>();

		FString outputDir = OutputSetting->OutputDirectory.Path;
		FString outputFileName = m_CurrentRender->uid + TEXT(".") + Extension;
		FString outputFilePath = FPaths::Combine(outputDir, outputFileName);

		bool cleanupFile = m_CurrentRender->path == "";

		bool isVideoCapture = !m_CurrentRender->videoFormat.IsEmpty();
		if (isVideoCapture)
		{
			cleanupFile = !m_CurrentRender->debug && !m_CurrentRender->keepSourceFrames;
			FString videoOutputPath = FPaths::Combine(outputDir, m_CurrentRender->uid + TEXT(".") + m_CurrentRender->videoFormat);
			videoOutputPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*videoOutputPath);

			FString pluginDir = FPaths::ProjectPluginsDir();
			FString exePath = FPaths::Combine(pluginDir, "ZLCloudPlugin/Binaries/ThirdParty/ffmpeg/ffmpeg.exe");
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			// Discover frames on disk. Don't assume they start at 0000 or are contiguous — shot gaps
			// (e.g. Commute 264000-265600) break ffmpeg sequence pattern even with a correct start offset.
			FString SearchExtension = Extension;
			if (SearchExtension.Equals(TEXT("jpg"), ESearchCase::IgnoreCase) ||
				SearchExtension.Equals(TEXT("jpeg"), ESearchCase::IgnoreCase))
			{
				SearchExtension = TEXT("jp*g");
			}
			const FString FrameWildcard = FPaths::Combine(outputDir, m_CurrentRender->uid + TEXT(".*.") + SearchExtension);
			TArray<FString> FoundFrameFiles;
			IFileManager::Get().FindFiles(FoundFrameFiles, *FrameWildcard, true, false);

			struct FFrameFile
			{
				int32 FrameNumber = 0;
				FString FileName;
			};
			TArray<FFrameFile> FrameFiles;
			FrameFiles.Reserve(FoundFrameFiles.Num());
			const FString FramePrefix = m_CurrentRender->uid + TEXT(".");
			for (const FString& FileName : FoundFrameFiles)
			{
				if (!FileName.StartsWith(FramePrefix))
				{
					continue;
				}
				const FString Rest = FileName.Mid(FramePrefix.Len());
				FString NumberStr;
				FString ExtStr;
				if (!Rest.Split(TEXT("."), &NumberStr, &ExtStr) || !NumberStr.IsNumeric())
				{
					continue;
				}
				FFrameFile Entry;
				Entry.FrameNumber = FCString::Atoi(*NumberStr);
				Entry.FileName = FileName;
				FrameFiles.Add(Entry);
			}
			FrameFiles.Sort([](const FFrameFile& A, const FFrameFile& B) { return A.FrameNumber < B.FrameNumber; });

			FString ConcatListPath;
			if (PlatformFile.FileExists(*exePath))
			{
				UE_LOG(LogZLCloudPlugin, Display, TEXT("ffmpeg.exe found"));

				if (FrameFiles.Num() == 0)
				{
					UE_LOG(LogZLCloudPlugin, Error, TEXT("ffmpeg skipped: no frame files found matching %s"), *FrameWildcard);
				}
				else
				{
					int32 intFrameRate = (int32)m_CurrentRender->videoFrameRate;
					FString frameRateStr = FString::FromInt(intFrameRate);

					int32 milliFrameRate = (int32)(1000.0f * (m_CurrentRender->videoFrameRate - intFrameRate));
					if (milliFrameRate > 0)
					{
						FString milliFrameRateStr = FString::FromInt(milliFrameRate);
						while (milliFrameRateStr.Len() < 3)
							milliFrameRateStr = TEXT("0") + milliFrameRateStr;
						frameRateStr = frameRateStr + TEXT(".") + milliFrameRateStr;
					}

					FString codecStr;
					if (!m_CurrentRender->videoCodec.IsEmpty())
					{
						codecStr = TEXT("-codec:v ") + m_CurrentRender->videoCodec;	//use specified video codec; ffmpeg -codecs will list available
					}

					if (!m_CurrentRender->videoPixelFormat.IsEmpty())
					{
						codecStr += TEXT(" -pix_fmt ") + m_CurrentRender->videoPixelFormat;	//use specified pixel format; ffmpeg -pix_fmts will list available
					}

					for (FString videoEncodeOption : m_CurrentRender->videoEncodeOptions)
					{
						codecStr += TEXT(" ");
						codecStr += videoEncodeOption;
					}

					const int32 StartNumber = FrameFiles[0].FrameNumber;
					bool bContiguous = true;
					for (int32 FrameIdx = 1; FrameIdx < FrameFiles.Num(); ++FrameIdx)
					{
						if (FrameFiles[FrameIdx].FrameNumber != FrameFiles[FrameIdx - 1].FrameNumber + 1)
						{
							bContiguous = false;
							break;
						}
					}

					FString commandArgs;
					FString ResolvedExtension = Extension;
					{
						FString DummyNum;
						FrameFiles[0].FileName.Split(TEXT("."), &DummyNum, &ResolvedExtension, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
					}

					if (bContiguous)
					{
						FString ffmpegSourcePath = FPaths::Combine(outputDir, m_CurrentRender->uid + TEXT(".%04d.") + ResolvedExtension);
						ffmpegSourcePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*ffmpegSourcePath);
						commandArgs = FString::Printf(
							TEXT("-framerate %s -start_number %d -pattern_type sequence -i \"%s\" %s -y \"%s\""),
							*frameRateStr, StartNumber, *ffmpegSourcePath, *codecStr, *videoOutputPath);
						UE_LOG(LogZLCloudPlugin, Display, TEXT("ffmpeg input: %d contiguous frames starting at %d"), FrameFiles.Num(), StartNumber);
					}
					else
					{
						// Timeline gaps between shots leave holes in {frame_number}; concat accepts sparse sets.
						ConcatListPath = FPaths::Combine(outputDir, m_CurrentRender->uid + TEXT("_ffmpeg_concat.txt"));
						FString ConcatContents;
						ConcatContents.Reserve(FrameFiles.Num() * 128);
						for (const FFrameFile& FrameFile : FrameFiles)
						{
							FString AbsPath = FPaths::Combine(outputDir, FrameFile.FileName);
							AbsPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*AbsPath);
							AbsPath.ReplaceInline(TEXT("\\"), TEXT("/"));
							AbsPath.ReplaceInline(TEXT("'"), TEXT("'\\''"));
							ConcatContents += FString::Printf(TEXT("file '%s'\n"), *AbsPath);
						}
						FFileHelper::SaveStringToFile(ConcatContents, *ConcatListPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
						FString ConcatListAbs = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*ConcatListPath);
						commandArgs = FString::Printf(
							TEXT("-f concat -safe 0 -r %s -i \"%s\" %s -y \"%s\""),
							*frameRateStr, *ConcatListAbs, *codecStr, *videoOutputPath);
						UE_LOG(LogZLCloudPlugin, Display, TEXT("ffmpeg input: %d frames with gaps (first=%d last=%d); using concat demuxer"),
							FrameFiles.Num(), StartNumber, FrameFiles.Last().FrameNumber);
					}

					FMonitoredProcess ffmpegProcess(*exePath, *commandArgs, true);
					ffmpegProcess.OnOutput().BindLambda([](FString outputLine) { UE_LOG(LogZLCloudPlugin, Display, TEXT("%s"), *outputLine); });

					if (ffmpegProcess.Launch())
					{
						UE_LOG(LogZLCloudPlugin, Display, TEXT("ffmpeg spawned with args %s"), *commandArgs);
						while (ffmpegProcess.Update())
						{
							FPlatformProcess::Sleep(0.02f);
						}

						int32 ffmpegReturnCode = ffmpegProcess.GetReturnCode();
						if (ffmpegReturnCode != 0)
						{
							UE_LOG(LogZLCloudPlugin, Warning, TEXT("ffmpeg failed to complete (%d)"), ffmpegReturnCode);
						}

						FString ffmpegOutput = ffmpegProcess.GetFullOutputWithoutDelegate();
						if (!ffmpegOutput.IsEmpty())
							UE_LOG(LogZLCloudPlugin, Display, TEXT("ffmpeg final output %s"), *ffmpegOutput);
					}
					else
					{
						UE_LOG(LogZLCloudPlugin, Warning, TEXT("ffmpeg failed to spawn with args %s"), *commandArgs);
					}
				}
			}
			else
			{
				UE_LOG(LogZLCloudPlugin, Warning, TEXT("ffmpeg.exe not found"));
			}

			if (m_CurrentRender->transparentSourceFrames)
			{
				int32 ConvertedFrameCount = 0;
				int32 FailedFrameCount = 0;
				for (const FFrameFile& FrameFile : FrameFiles)
				{
					const FString FramePath = FPaths::Combine(outputDir, FrameFile.FileName);
					TArray<FColor> FramePixels;
					int32 FrameWidth = 0;
					int32 FrameHeight = 0;
					if (!LoadImageAsFColorArray(FramePath, FramePixels, FrameWidth, FrameHeight))
					{
						++FailedFrameCount;
						continue;
					}

					for (FColor& Pixel : FramePixels)
					{
						UnpremultiplyAlphaPixel(Pixel);
					}

					TArray64<uint8> StraightAlphaPng;
					FImageView FrameImage(FramePixels.GetData(), FrameWidth, FrameHeight);
					if (!FImageUtils::CompressImage(StraightAlphaPng, TEXT("png"), FrameImage) ||
						!FFileHelper::SaveArrayToFile(StraightAlphaPng, *FramePath))
					{
						++FailedFrameCount;
						continue;
					}
					++ConvertedFrameCount;
				}

				if (FailedFrameCount == 0)
				{
					UE_LOG(LogZLCloudPlugin, Display,
						TEXT("Transparent video source frames post-processed after MP4 encoding. Converted=%d Failed=%d"),
						ConvertedFrameCount, FailedFrameCount);
				}
				else
				{
					UE_LOG(LogZLCloudPlugin, Warning,
						TEXT("Transparent video source frames post-processed after MP4 encoding. Converted=%d Failed=%d"),
						ConvertedFrameCount, FailedFrameCount);
				}
			}

			if (cleanupFile)	//delete all the source files
			{
				const int32 FailedFrameDeletes = DeleteMRQJobFrameFiles(outputDir, m_CurrentRender->uid, Extension);
				if (FailedFrameDeletes > 0)
				{
					UE_LOG(LogZLCloudPlugin, Warning, TEXT("Could not delete %d frame file(s) in %s; retrying once the video has been read."),
						FailedFrameDeletes, *outputDir);
				}
			}
			else
			{
				UE_LOG(LogZLCloudPlugin, Display, TEXT("Keeping source frames in %s for video job %s"),
					*outputDir, *m_CurrentRender->uid);
			}

			if (!ConcatListPath.IsEmpty())
			{
				IFileManager::Get().Delete(*ConcatListPath, true);
			}

			outputFilePath = videoOutputPath;	//we'll send this and delete it later
		}

		if (m_CurrentRender->type == ScreenshotType::DEFAULT2D)
		{
			const double CaptureWallSeconds = FMath::Max(0.0, FPlatformTime::Seconds() - m_CurrentRender->captureStartTime);
			// Video jobs deliver the muxed file; source stills are deleted unless keepSourceFrames/debug is set.
			const FString FilePath = isVideoCapture
				? outputFilePath
				: ResolveMRQOutputImagePath(outputDir, m_CurrentRender->uid, Extension);

			if (FPaths::FileExists(FilePath))
			{
				UE_LOG(LogZLCloudPlugin, Display,
					TEXT("MRQ stream capture loading output file: %s (requested %dx%d)"),
					*FilePath, m_CurrentRender->width, m_CurrentRender->height);

				TArray<FColor> UnpremultipliedData;
				FIntPoint ImageSize(m_CurrentRender->width, m_CurrentRender->height);
				double FinalEncodeSeconds = 0.0;

				if (isVideoCapture)
				{
					const double EncodeStartSeconds = FPlatformTime::Seconds();
					if (!FFileHelper::LoadFileToArray(imageBytes, *FilePath))
						imageBytes.Empty();
					FinalEncodeSeconds = FPlatformTime::Seconds() - EncodeStartSeconds;

					// Frames can still be flushing to disk during the first sweep, so clear any stragglers.
					if (cleanupFile)
					{
						const int32 FailedFrameDeletes = DeleteMRQJobFrameFiles(outputDir, m_CurrentRender->uid, Extension);
						if (FailedFrameDeletes > 0)
						{
							UE_LOG(LogZLCloudPlugin, Warning, TEXT("Left %d frame file(s) behind in %s"), FailedFrameDeletes, *outputDir);
						}
					}
				}
				else
				{
					imageBytes.Empty();
					int32 ImgWidth = 0;
					int32 ImgHeight = 0;
					if (LoadImageAsFColorArray(FilePath, UnpremultipliedData, ImgWidth, ImgHeight))
					{
						if (ImgWidth != m_CurrentRender->width || ImgHeight != m_CurrentRender->height)
						{
							UE_LOG(LogZLCloudPlugin, Error,
								TEXT("MRQ output size mismatch for %s: expected %dx%d, got %dx%d"),
								*FilePath,
								m_CurrentRender->width,
								m_CurrentRender->height,
								ImgWidth,
								ImgHeight);
						}
						else
						{
							ZLJobTrace::JOBTRACE_TIMER_END("Render");
							ZLJobTrace::JOBTRACE_TIMER_START("Encode");

							const double EncodeStartSeconds = FPlatformTime::Seconds();
							if (m_CurrentRender->transparent)
							{
								for (FColor& Pixel : UnpremultipliedData)
									UnpremultiplyAlphaPixel(Pixel);
							}

							FImageView ImageView(UnpremultipliedData.GetData(), ImgWidth, ImgHeight);
							FImageUtils::CompressImage(imageBytes, *Extension, ImageView);
							ImageSize = FIntPoint(ImgWidth, ImgHeight);
							FinalEncodeSeconds = FPlatformTime::Seconds() - EncodeStartSeconds;
							ZLJobTrace::JOBTRACE_TIMER_END("Encode");
						}
					}
				}

				if (!imageBytes.IsEmpty())
				{
					if (isVideoCapture && ZLJobTrace::JOBTRACE_IS_CURRENTLY_TIMING("Render"))
						ZLJobTrace::JOBTRACE_TIMER_END("Render");

					LogDefault2DMRQTimingSummary(
						ImageSize.X,
						ImageSize.Y,
						isVideoCapture,
						m_CurrentRender->videoFormat,
						CaptureWallSeconds,
						FinalEncodeSeconds);

					// An explicit output path means the caller wants the result on disk, so only
					// the transient copy written for the server response is removed.
					const bool bKeepFinalOutputOnDisk = !m_CurrentRender->path.IsEmpty();
					if (cleanupFile && !bKeepFinalOutputOnDisk)
					{
						if (isVideoCapture)
						{
							IFileManager::Get().Delete(*FilePath, true);
						}
						else
						{
							DeleteMRQOutputImages(outputDir, m_CurrentRender->uid, Extension);
						}
					}
					else if (bKeepFinalOutputOnDisk)
					{
						UE_LOG(LogZLCloudPlugin, Display, TEXT("Capture output written to %s"), *FilePath);
					}

					if (m_CurrentRender->bStreamImageCapture)
					{
						SendStreamImageCaptureResponse(imageBytes);
					}
					else
					{
						SendCapturedImageResponse(imageBytes, ImageSize);
					}
				}
				else
				{
					FString errorMsg = "Failed to load rendered image from disk.";
					ZLScreenshot::SendImageFailureResponse(errorMsg);
					ClearCurrentRenderJob();
					EditorRestoreLocalCapture2DODModeAfterJob();
					ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
				}
			}
			else
			{
				UE_LOG(LogZLCloudPlugin, Error, TEXT("MRQ output missing on disk: %s"), *FilePath);
				FString errorMsg = isVideoCapture
					? TEXT("ffmpeg did not produce the expected video file.")
					: TEXT("Movie Render Pipeline failed to render.");
				ZLScreenshot::SendImageFailureResponse(errorMsg);
				ClearCurrentRenderJob();
				EditorRestoreLocalCapture2DODModeAfterJob();
				ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
			}
		}
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("OnMoviePipelineFinished did not enter success path. Success=%s CurrentRenderValid=%s"),
			Results.bSuccess ? TEXT("true") : TEXT("false"),
			m_CurrentRender.IsValid() ? TEXT("true") : TEXT("false"));
	}
#if WITH_EDITOR
	if (!Results.bSuccess)
	{
		if (m_CurrentRender.IsValid())
		{
			ClearCurrentRenderJob();
		}
		else
		{
			ZLCloudPlugin::FZLCloudPluginModule::GetModule()->SetContentGenerationState(EZLContentGenerationState::None);
		}
		EditorRestoreLocalCapture2DODModeAfterJob();
	}
#else
	if (!Results.bSuccess)
	{
		if (m_CurrentRender.IsValid())
		{
			ClearCurrentRenderJob();
		}
		else
		{
			ZLCloudPlugin::FZLCloudPluginModule::GetModule()->SetContentGenerationState(EZLContentGenerationState::None);
		}
	}
#endif

	ResetMRQVideoProgressState();
}
#endif

