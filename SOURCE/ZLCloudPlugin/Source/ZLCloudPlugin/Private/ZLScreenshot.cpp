// Copyright ZeroLight ltd. All Rights Reserved.
#include "ZLScreenshot.h"
#include "LauncherComms.h"
#include "ZLCloudPluginPrivate.h"
#include "ZLJobTrace.h"
#include "Engine/GameViewportClient.h"
#include "Interfaces/IPluginManager.h"

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
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "LevelSequence.h"
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
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

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
		Job->TransparentGlassMaterialSwapOuter = NewObject<UZLTransientMaterialSwapOuter>(GetTransientPackage(), NAME_None, RF_Transient);

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
						DynMID->CopyParameterOverrides(SourceMI);

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
	if (m_injectMetadata)
	{
		std::string outpathStr = std::string(TCHAR_TO_UTF8(*m_finalOutpath));
		std::ifstream input(outpathStr, std::ios::in | std::ios::binary);
		std::ifstream::pos_type pos = input.tellg();

		std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(input), {});

		int bufferSize = (int)buffer.size();

		if (bufferSize != 0 && bufferSize > m_imageBytesSize)
		{
			m_injectMetadata = false;

			TArray64<uint8> imageBytes;
			imageBytes.Append(buffer.data(), (int64)buffer.size());

			TSharedPtr<FJsonObject> responseStateData = MakeShareable(new FJsonObject);

			if (m_CurrentRender->postJobCurrentState.IsValid())
				responseStateData->SetObjectField("current_state", m_CurrentRender->postJobCurrentState);

			if (m_CurrentRender->postJobUnmatchedState.IsValid())
				responseStateData->SetObjectField("unprocessed_state", m_CurrentRender->postJobUnmatchedState);

			if (m_CurrentRender->postJobTimeoutState.IsValid())
				responseStateData->SetObjectField("timeout_state", m_CurrentRender->postJobTimeoutState);

			FString responseDataStr;

			TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&responseDataStr);

			FJsonSerializer::Serialize(responseStateData.ToSharedRef(), writer);

			auto responseDataAnsi = StringCast<ANSICHAR>(*responseDataStr);
			const char* responseDataChar = responseDataAnsi.Get();
			int32 responseDataLen = strlen(responseDataChar);

			TArray64<uint8> responseDataBytes;
			responseDataBytes.Append((uint8*)responseDataChar, responseDataLen);

			auto uidAnsi = StringCast<ANSICHAR>(*m_CurrentRender->uid);
			const char* uidChar = uidAnsi.Get();
			int32 uidLen = strlen(uidChar);

			TArray64<uint8> uidStrBytes;
			uidStrBytes.Append((uint8*)uidChar, uidLen);

			int32 responseLength = responseDataBytes.Num();

			imageBytes.Insert((uint8*)&responseLength, sizeof(int32), 0);
			imageBytes.Insert(responseDataBytes, sizeof(int32));
			imageBytes.Insert(uidStrBytes, sizeof(int32) + responseDataBytes.Num());

#ifdef SUPPORT_LEGACY_MESSAGES
			if (m_CurrentRender->isLegacyMessage)
				m_LauncherComms->SendLauncherMessageBinary("CAPTUREIMAGERESULT", imageBytes);
			else
#endif //SUPPORT_LEGACY_MESSAGES
				m_LauncherComms->SendLauncherMessageBinary("CAPTUREMEDIARESULT", imageBytes);

			m_CurrentRender.Reset();

			m_equirect360JobFinished = false;
			m_playerController->SetViewTarget(m_initialViewTarget);
			m_panoViewTarget->Destroy();

			if (Delegates)
			{
				Delegates->OnContentGenerationFinished.Broadcast(true);
				Delegates->OnContentGenerationFinishedNative.Broadcast(true);
			}
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

				//If we submit a state request as part of the job we need to wait till its done to capture, otherwise we can capture immediately
			}
			else
			{
				captureScreenshot = true;
			}
		}
		else
		{
			if (m_CurrentRender->jobStateRequestFinished && m_CurrentRender->jobStateRequestSuccess) //State matches current render state data
			{
				captureScreenshot = true;
				ZLJobTrace::JOBTRACE_TIMER_END("StateProcessing");
			}
			else
			{
				if (m_CurrentRender->jobStateRequestFinished) //If state request finished but was not fully successful need to pass on rejection here and inform IM of what failed
				{
					if (s_rejectUnmatchedStateJobs)
					{
						FString errorMsg = "Timeout reached waiting for state data to match request.";
						ZLJobTrace::JOBTRACE_TIMER_END("StateProcessing");

						ZLScreenshot::SendImageFailureResponse(errorMsg);

						m_CurrentRender.Reset();
						ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
						return;
					}
					else //Continue capture but just add unmatched state data to end response
					{
						captureScreenshot = true;
						ZLJobTrace::JOBTRACE_TIMER_END("StateProcessing");
					}
				}
			}
		}

		if ((captureScreenshot && !m_CurrentRender->jobCaptureStarted) || m_CurrentRender->type == ScreenshotType::EQUIRECT360)
		{
			if (UGameViewportClient* GameViewport = m_CurrentRender->world->GetGameViewport())
			{
				FSceneViewport* SceneViewport = GameViewport->GetGameViewport();
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

						//Removing this as it may cause temporal issues
						//PauseGameTime();

						if (m_CurrentRender->useMRQPipeline)
						{
							if (m_CurrentRender->transparent)
								SwapTransparentGlassMaterialsForAlphaScreenshot(m_CurrentRender.Get());
							PerformMRQCapture(m_CurrentRender->width, m_CurrentRender->height, ContentGenOutputPath, m_CurrentRender->uid);
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
									if (m_initialViewTarget)
										m_initialViewTarget->GetActorEyesViewPoint(startLoc, startRot); //More reliable truth

									m_startRotation = startRot.Quaternion();
									m_faceSize = 8 * ((uint32)floor(2 * (double)m_CurrentRender->height / PI / 8));

									m_panoViewTarget = World->SpawnActor<class ACameraActor>(startLoc, startRot);

									FViewTargetTransitionParams TransitionParams;
									TransitionParams.BlendTime = 0.0f;

									UCameraComponent* cam = m_panoViewTarget->GetCameraComponent();
									cam->SetAspectRatio(1.0f);

									m_playerController->SetViewTarget(m_panoViewTarget, TransitionParams);

									int32 count = m_CurrentRender->world->PostProcessVolumes.Num();

									for (int32 x = 0; x < count; ++x)
									{
										FPostProcessVolumeProperties volume = m_CurrentRender->world->PostProcessVolumes[x]->GetProperties();
										if (volume.bIsUnbound)
										{
											FPostProcessSettings* settings = (FPostProcessSettings*)volume.Settings;

											if (!settings->bOverride_AutoExposureMaxBrightness || !settings->bOverride_AutoExposureMinBrightness) //Fallback clamp but project may already specify a setting using pre/post capture events
											{
												settings->bOverride_AutoExposureMaxBrightness = true;
												settings->bOverride_AutoExposureMinBrightness = true;

												m_cacheAutoExposureMaxBrightness = settings->AutoExposureMaxBrightness;
												m_cacheAutoExposureMinBrightness = settings->AutoExposureMinBrightness;

												settings->AutoExposureMaxBrightness = 4;
												settings->AutoExposureMinBrightness = 4;
											}

											m_cacheVignetteIntensity = settings->VignetteIntensity;
											settings->bOverride_VignetteIntensity = true;
											settings->VignetteIntensity = 0;
										}
									}

									if (count == 0)
									{
										m_CurrentRender->needsCameraPostprocessAdjustmentPerFace = true;
									}
								}

								if (m_CurrentRender->faceID == 0)
								{
									if (!m_CurrentRender->useMRQPipeline)
										PauseGameTime();

									FHighResScreenshotConfig& HighResScreenshotConfig = GetHighResScreenshotConfig();
									HighResScreenshotConfig.SetResolution(m_faceSize, m_faceSize);

									FIntPoint res = FIntPoint(m_faceSize, m_faceSize);
									GEngine->GameUserSettings->SetScreenResolution(res);

									APlayerCameraManager* PlayerCamera = m_playerController->PlayerCameraManager;

									PlayerCamera->SetFOV(90.0f);

									int dataSize = m_CurrentRender->width * m_CurrentRender->width * 2;

									m_outBuffer = new unsigned char[dataSize];
									m_imageData = new int* [6];
									m_numPixels = m_faceSize * m_faceSize;

									for (int i = 0; i < 6; i++)
										m_imageData[i] = new int[m_numPixels];

									ApplyCVarOverridesDirect();
								}

								if (m_CurrentRender->needsCameraPostprocessAdjustmentPerFace)
								{
									if (APlayerController* PlayerController = m_CurrentRender->world->GetFirstPlayerController())
									{
										if (AActor* ViewTarget = PlayerController->GetViewTarget())
										{
											if (UCameraComponent* CameraComponent = ViewTarget->FindComponentByClass<UCameraComponent>())
											{
												UE_LOG(LogTemp, Log, TEXT("Found active Camera Component, modifying its Post Process Settings."));

												m_cacheVignetteIntensity = CameraComponent->PostProcessSettings.VignetteIntensity;

												CameraComponent->PostProcessSettings.bOverride_VignetteIntensity = true;
												CameraComponent->PostProcessSettings.VignetteIntensity = 0.0f;
											}
										}
									}
								}

								if (m_CurrentRender->faceID >= 0 && m_CurrentRender->faceID < 6)
								{
									if (m_CurrentRender->lastFaceCompletedID != m_CurrentRender->faceID - 1) //We are still waiting for last faceID to complete
										return;

									// Notify plugins to prepare/update their data before capturing the screenshot (only on first face)
									if (m_CurrentRender->faceID == 0)
									{
										if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
										{
											FZLPrepareScreenshotParams PrepareParams;
											PrepareParams.CaptureWidth = m_faceSize;
											PrepareParams.CaptureHeight = m_faceSize;
											LocalDelegates->OnPrepareScreenshotNative.Broadcast(PrepareParams);
										}
									}

									FString FormattedString = FString::Printf(TEXT("CaptureFaceID%d"), m_CurrentRender->faceID);
									ZLJobTrace::JOBTRACE_TIMER_START(FormattedString);

									if (m_CurrentRender->useMRQPipeline)
									{
										PerformMRQCapture(m_faceSize, m_faceSize, ContentGenOutputPath, m_CurrentRender->uid + "_FACE_" + FString::FromInt(m_CurrentRender->faceID));
									}
									else
									{
										if (m_CurrentRender->faceID == 0)
										{
											if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
											{
												LocalDelegates->OnScreenshotRequestedNative.Broadcast(m_faceSize, m_faceSize, m_CurrentRender->world);
											}
										}
										Viewport->TakeHighResScreenShot();
									}

									if (!m_CurrentRender->useMRQPipeline)
									{
										FQuat quat = m_startRotation * m_faceRotation[m_CurrentRender->faceID];

										if (m_panoViewTarget)
											m_panoViewTarget->SetActorRotation(quat.Rotator());
										else
											UE_LOG(LogZLCloudPlugin, Error, TEXT("m_panoViewTarget is a nullptr"));
									}

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

bool ZLScreenshot::PerformMRQCapture(int width, int height, FString outputPath, FString jobName)
{
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
					startFrame = m_CurrentRender->videoStartFrame;
					renderedFrames = m_CurrentRender->videoNumFrames;	//use supplied limit, or default (-1) will play the whole sequence
					break;
				}
			}
		}

		// Create a new Level Sequence at runtime
		if (LevelSequence == NULL)
		{
			LevelSequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transient);
			LevelSequence->Initialize(); // Important to initialize the sequence
			renderedFrames = 1;
		}

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

			MovieScene->SetDisplayRate(FFrameRate((int32)(m_CurrentRender->videoFrameRate*1000.0), 1000));	//i.e. accurate to 3 decimal places

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
			}
		}

		UMoviePipelineExecutorJob* Job = Subsystem->AllocateJob(LevelSequence);

		Job->SetSequence(LevelSequence);

#if UNREAL_5_2_OR_NEWER
		UMoviePipelinePrimaryConfig* MasterConfig = NewObject<UMoviePipelinePrimaryConfig>(GetTransientPackage());
#else
		UMoviePipelineMasterConfig* MasterConfig = NewObject<UMoviePipelineMasterConfig>(GetTransientPackage());
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
			OutputSetting->ZeroPadFrameNumbers = 4;	//4 (default) gives 166 seconds @ 60fps, matches %04d used by ffmpeg; increase to 5 (and change to %5d) if more is needed
			OutputSetting->FrameNumberOffset = -startFrame;
		}
		Job->JobName = jobName;


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

	if (!m_onScreenshotCapturedDelegateHandle.IsValid()) //Locally testing 2DOD may not have set this delegate as no adopt needed
		Set2DODMode(true);

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

		if (!JsonParsed->TryGetStringField(FString("width"), jsonOut))
			return false;
		width = FCString::Atoi(*jsonOut);

		if (!JsonParsed->TryGetStringField(FString("height"), jsonOut))
			return false;
		height = FCString::Atoi(*jsonOut);

		if (!JsonParsed->TryGetStringField(FString("format"), format))
			return false;

		TSharedPtr<FJsonObject> AdvancedRenderConfig;

		//Optional: requires state-match complete callback before screenshot is taken
		if (!JsonParsed->TryGetObjectField(FString("state"), stateRequestData))
		{

		}
		else if (stateRequestData->IsValid() && stateRequestData->Get()->Values.Num() == 0)
		{
			stateRequestData = nullptr;
		}
		else if (stateRequestData->IsValid())
		{
			const TSharedPtr<FJsonObject>* OriginalObjectPtr = nullptr;
			if (stateRequestData->Get()->TryGetObjectField("zerolightAdvancedRenderConfig", OriginalObjectPtr) && OriginalObjectPtr && OriginalObjectPtr->IsValid())
			{
				FString JsonString;

				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);

				FJsonSerializer::Serialize(OriginalObjectPtr->ToSharedRef(), Writer);

				AdvancedRenderConfig = MakeShared<FJsonObject>();

				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
				FJsonSerializer::Deserialize(Reader, AdvancedRenderConfig);

				stateRequestData->Get()->RemoveField("zerolightAdvancedRenderConfig"); //Ensure this doesnt go to StateManager
			}
		}

		//Optional: only writes to disk if dir path supplied
		path = "";
		JsonParsed->TryGetStringField(FString("path"), path);

		if (!JsonParsed->TryGetStringField(FString("uid"), uid))
		{
			//Should return but i dont have a ID at the moment and this is easier for debugging
			uid = FGuid::NewGuid().ToString();
			UE_LOG(LogZLCloudPlugin, Error, TEXT("No ID specified, using default: %s"), *uid);
		}

		if (!JsonParsed->TryGetNumberField(FString("type"), type))
			return false;

		m_NextRender = MakeShared<ZLScreenshotJob>(width, height, format, path, uid, InWorld, stateRequestData, (ScreenshotType)type);

#ifdef SUPPORT_LEGACY_MESSAGES
		m_NextRender->isLegacyMessage = isLegacyMessage;
#endif //SUPPORT_LEGACY_MESSAGES

		bool isVideoCapture = false;
		if(JsonParsed->TryGetBoolField("videoCapture", isVideoCapture) && isVideoCapture)
		{
			m_NextRender->videoFormat = m_NextRender->format;
			SanitizeParam(m_NextRender->videoFormat);
			if (!JsonParsed->TryGetStringField(FString("videoFrameFormat"), m_NextRender->format))
			{
				m_NextRender->format = "jpg";
				UE_LOG(LogTemp, Warning, TEXT("No video frame format, defaulted to %s"), *m_NextRender->format);
			}
			
			if (!JsonParsed->TryGetNumberField(FString("videoFrameRate"), m_NextRender->videoFrameRate))
			{
				m_NextRender->videoFrameRate = 30.0;	//default to 30fps
				UE_LOG(LogTemp, Display, TEXT("No video frame rate, defaulted to %f fps"), m_NextRender->videoFrameRate);
			}
			if (!JsonParsed->TryGetNumberField(FString("videoNumFrames"), m_NextRender->videoNumFrames))
			{
				m_NextRender->videoNumFrames = -1;	//default to playing all of specified sequence
				UE_LOG(LogTemp, Display, TEXT("No video frame frame count, defaulted to %d"), m_NextRender->videoNumFrames);
			}
			if (!JsonParsed->TryGetNumberField(FString("videoStartFrame"), m_NextRender->videoStartFrame))
			{
				m_NextRender->videoStartFrame = 0;	//default to playing from beginning of specified sequence
				UE_LOG(LogTemp, Display, TEXT("No video start frame, defaulted to %d"), m_NextRender->videoStartFrame);
			}
			if(!JsonParsed->TryGetStringField(FString("videoSequence"), m_NextRender->videoSequenceName))
			{
				UE_LOG(LogTemp, Warning, TEXT("No video sequence specified, will generate single frame video"));
			}

			if (JsonParsed->TryGetStringField(FString("videoCodec"), m_NextRender->videoCodec))
			{
				SanitizeParam(m_NextRender->videoCodec);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("No video codec specified, will use default for video format %s"), *m_NextRender->videoFormat);
			}

			if (JsonParsed->TryGetStringField(FString("videoPixelFormat"), m_NextRender->videoPixelFormat))
			{
				SanitizeParam(m_NextRender->videoPixelFormat);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("No video pixel format specified, will use default for video format %s"), *m_NextRender->videoFormat);
			}

			if (JsonParsed->TryGetStringArrayField(FString("videoEncodeOptions"), m_NextRender->videoEncodeOptions))
			{
				for(FString &param: m_NextRender->videoEncodeOptions)
					SanitizeParam(param);
			}
			else
			{
				m_NextRender->videoEncodeOptions.Empty();	//shouldn't be necessary
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
			AdvancedRenderConfig->TryGetBoolField("useMovieRenderQueue", m_NextRender->useMRQPipeline);

			const TArray<TSharedPtr<FJsonValue>>* CVarArrayValue;
			if (AdvancedRenderConfig->TryGetArrayField("cVarOverrides", CVarArrayValue))
			{
				TArray<FString> cVarOverrides;
				for (TSharedPtr<FJsonValue> Value : *CVarArrayValue)
				{
					cVarOverrides.Add(Value.Get()->AsString());
				}

				m_NextRender->cVarOverrides = cVarOverrides;
			}

			FString aaModeString;
			if (AdvancedRenderConfig->TryGetStringField("aaMode", aaModeString))
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
			if (AdvancedRenderConfig->TryGetNumberField("spatialSampleCount", spatialSamples))
			{
				m_NextRender->spatialSampleCount = spatialSamples;
			}

			int temporalSamples;
			if (AdvancedRenderConfig->TryGetNumberField("temporalSampleCount", temporalSamples))
			{
				m_NextRender->temporalSampleCount = temporalSamples;
			}
		}

		bool transparent = false;
		if (JsonParsed->TryGetBoolField(FString("transparent"), transparent))
		{
			m_NextRender->transparent = transparent;
			if (transparent)
			{
				m_NextRender->useMRQPipeline = true;
				m_NextRender->cVarOverrides.Add("r.PostProcessing.PropagateAlpha=true");
			}
		}

		bool isRaytracing = false;
		static const auto CVarRayTracing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing"));
		if (CVarRayTracing && CVarRayTracing->GetInt() != 0)
		{
			isRaytracing = true;
		}

		const UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();
		check(Settings);

		FString SetFrameCountCmd = "";
		if (Settings->screenshotFrameWaitCountOverride != 0)
		{
			SetFrameCountCmd = FString::Printf(TEXT("r.HighResScreenshotDelay %d"), Settings->screenshotFrameWaitCountOverride);
			ZLJobTrace::JOBTRACE_ADD_DATA("r.HighResScreenshotDelay", MakeShared<FJsonValueNumber>(Settings->screenshotFrameWaitCountOverride));
		}
		else
		{
			int delayFrameCount = (isRaytracing) ? s_hiresScreenshotFrameCountRaytracing : s_hiresScreenshotFrameCount;
			ZLJobTrace::JOBTRACE_ADD_DATA("r.HighResScreenshotDelay", MakeShared<FJsonValueNumber>(delayFrameCount));
			SetFrameCountCmd = FString::Printf(TEXT("r.HighResScreenshotDelay %d"), delayFrameCount);
		}
		GEngine->Exec(InWorld, *SetFrameCountCmd);
	}
	else
		return false;

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
	m_CurrentRender->m_SavedCVarStates.Empty();

	IConsoleManager& ConsoleMgr = IConsoleManager::Get();

	if (m_CurrentRender->cVarOverrides.Num() > 0)
	{
		for (const FString& CommandEntry : m_CurrentRender->cVarOverrides)
		{
			FString CVarName;
			FString CVarValueStr;

			if (CommandEntry.Split(TEXT(" "), &CVarName, &CVarValueStr))
			{
				if (IConsoleVariable* CVar = ConsoleMgr.FindConsoleVariable(*CVarName))
				{
					m_CurrentRender->m_SavedCVarStates.Add(CVarName, CVar->GetString());

					CVar->Set(*CVarValueStr, ECVF_SetByCode);

					UE_LOG(LogZLCloudPlugin, Log, TEXT("ZLScreenshot: Applied CVar Override: %s = %s (Saved Old Value: %s)"), *CVarName, *CVarValueStr, *m_CurrentRender->m_SavedCVarStates[CVarName]);
				}
				else
				{
					UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLScreenshot: Could not find CVar '%s' to override."), *CVarName);
				}
			}
		}
	}
}

void ZLScreenshot::SendImageFailureResponse(FString& errorMsg)
{
	FString uid = m_CurrentRender->uid;
	TSharedPtr<FJsonObject> responseData = MakeShareable(new FJsonObject);
	responseData->SetStringField("uid", uid);

	if (m_CurrentRender->postJobCurrentState.IsValid())
		responseData->SetObjectField("current_state", m_CurrentRender->postJobCurrentState);

	if (m_CurrentRender->postJobTimeoutState.IsValid())
		responseData->SetObjectField("timeout_state", m_CurrentRender->postJobTimeoutState);

	if (m_CurrentRender->postJobUnmatchedState.IsValid())
	{
		responseData->SetObjectField("unprocessed_state", m_CurrentRender->postJobUnmatchedState);
		if (!m_CurrentRender->postJobTimeoutState.IsValid())
			errorMsg = "Render request rejected due to unprocessed state fields";
	}

	responseData->SetStringField("error", errorMsg);

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

void ZLScreenshot::PauseGameTime()
{
	if (m_CurrentRender.IsValid() && !m_CurrentRender->useMRQPipeline)
	{
		if (AWorldSettings* WorldSettings = m_CurrentRender->world->GetWorldSettings())
		{
			m_cacheTimeDilation = WorldSettings->TimeDilation;
			WorldSettings->TimeDilation = 0.0f;
			UE_LOG(LogZLCloudPlugin, Display, TEXT("Game Time Paused. TimeDilation set to 0.0."));
		}
	}
}

void ZLScreenshot::ResumeGameTime()
{
	if (m_CurrentRender.IsValid() && !m_CurrentRender->useMRQPipeline)
	{
		if (AWorldSettings* WorldSettings = m_CurrentRender->world->GetWorldSettings())
		{
			WorldSettings->TimeDilation = m_cacheTimeDilation;
			UE_LOG(LogZLCloudPlugin, Display, TEXT("Game Time Resumed"));
		}
	}
}

void ZLScreenshot::OnScreenshotComplete(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData)
{
	check(IsInGameThread());

	if (m_CurrentRender.IsValid())
	{
		if (m_CurrentRender->useMRQPipeline)
			return;

		double capEndTime = FPlatformTime::Seconds();
		double elapsed = capEndTime - m_CurrentRender->captureStartTime;

		UE_LOG(LogZLCloudPlugin, Display, TEXT("Call OnScreenshotComplete: %ix%i - Duration %f seconds - End Frame %11d"), InSizeX, InSizeY, elapsed, GFrameCounter);

		TArray64<uint8> imageBytes;

		bool finaliseCleanupEquirectJob = false;

		if (m_CurrentRender->type == ScreenshotType::DEFAULT2D)
		{
			ResumeGameTime();
			ZLJobTrace::JOBTRACE_TIMER_END("Render");
			ZLJobTrace::JOBTRACE_TIMER_START("Encode");
			//Render is done, allow next request
			FImageView image((const FColor*)InImageData.GetData(), InSizeX, InSizeY);

			if (!FImageUtils::CompressImage(imageBytes, *m_CurrentRender->format, image))
			{
				UE_LOG(LogZLCloudPlugin, Error, TEXT("CompressImage failed: %ix%i"), InSizeX, InSizeY);
				FString errorMsg = "CompressImage failed.";
				ZLScreenshot::SendImageFailureResponse(errorMsg);
				m_CurrentRender.Reset();
				ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
				return;
			}

			if (m_CurrentRender->path != "")
			{
				FString timestamp = FDateTime::Now().ToString();
				FString dateCleaned = timestamp.Replace(TEXT("."), TEXT("_"));
				FString filename = m_CurrentRender->uid + "_" + dateCleaned + "." + m_CurrentRender->format;
				FString outpath = FPaths::Combine(m_CurrentRender->path, filename);
				FFileHelper::SaveArrayToFile(imageBytes, *outpath);
				UE_LOG(LogZLCloudPlugin, Display, TEXT("Screen shot saved to: %s"), *outpath);
			}

			ZLJobTrace::JOBTRACE_TIMER_END("Encode");
		}
		else if (m_CurrentRender->type == ScreenshotType::EQUIRECT360)
		{
			if (m_CurrentRender->path != "")
			{
				//Render is done for this face
				FImageView image((const FColor*)InImageData.GetData(), InSizeX, InSizeY);
				if (!FImageUtils::CompressImage(imageBytes, *m_CurrentRender->format, image))
				{
					UE_LOG(LogZLCloudPlugin, Error, TEXT("CompressImage failed: %ix%i"), InSizeX, InSizeY);
					FString errorMsg = "CompressImage failed.";
					ZLScreenshot::SendImageFailureResponse(errorMsg);
					m_CurrentRender.Reset();
					ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
					return;
				}

				std::string faceID = std::to_string(m_CurrentRender->faceID);
				FString FfaceID(faceID.c_str());

				FString timestamp = FDateTime::Now().ToString();
				FString dateCleaned = timestamp.Replace(TEXT("."), TEXT("_"));
				FString filename = m_CurrentRender->uid + "_" + dateCleaned + "_" + FfaceID + "." + m_CurrentRender->format;
				FString outpath = FPaths::Combine(m_CurrentRender->path, filename);
				FFileHelper::SaveArrayToFile(imageBytes, *outpath);
				UE_LOG(LogZLCloudPlugin, Display, TEXT("Screen shot saved to: %s"), *outpath);
			}

			m_CurrentRender->lastFaceCompletedID = m_CurrentRender->faceID - 1;

			FString FormattedString = FString::Printf(TEXT("CaptureFaceID%d"), m_CurrentRender->faceID - 1);
			ZLJobTrace::JOBTRACE_TIMER_END(FormattedString);

			if (!ZLJobTrace::JOBTRACE_IS_CURRENTLY_TIMING("RunComputeFaces"))
				ZLJobTrace::JOBTRACE_TIMER_START("RunComputeFaces");

			memcpy(m_imageData[m_CurrentRender->faceID - 1], (void*)InImageData.GetData(), m_numPixels * 4);

			int dataSize = m_CurrentRender->width * m_CurrentRender->width * 2;

			void* textureData = (void*)m_imageData[m_CurrentRender->faceID - 1];
			int width = m_CurrentRender->width;
			int faceSize = m_faceSize;
			int faceID = m_CurrentRender->faceID;
			void* outBuffer = (void*)m_outBuffer;

			ENQUEUE_RENDER_COMMAND(RunPanoComputeShader)
				([=](FRHICommandListImmediate& RHICmdList) {
				CloudStream2DLL::TextureToPlugin(textureData, dataSize, width, faceSize, faceID, (unsigned char*)outBuffer);
			});

			if (m_CurrentRender->faceID == 6)
			{
				ResumeGameTime();
				ZLJobTrace::JOBTRACE_TIMER_END("Render");
				int attempts = 0;
				const int max_attempts = 5000;

				// Wait for queued compute shader jobs on render thread to complete for all cube faces.
				while (!CloudStream2DLL::IsPanoImageReady())
				{
					FPlatformProcess::Sleep(0.01f);

					attempts++;

					if (attempts >= max_attempts)
					{
						m_equirect360JobFinished = false;
						finaliseCleanupEquirectJob = true;

						FString errorMsg = "Compute shader in Cloudstream2 plug-in has taken over 50 seconds to respond.";
						UE_LOG(LogZLCloudPlugin, Error, TEXT("%s"), *errorMsg);
						ZLScreenshot::SendImageFailureResponse(errorMsg);
						break;
					}
				}

				ZLJobTrace::JOBTRACE_TIMER_END("RunComputeFaces");
				ZLJobTrace::JOBTRACE_TIMER_START("Encode");

				int time = attempts * 10;

				for (int j = 0; j < 6; j++)
				{
					if (m_imageData[j])
						delete[] m_imageData[j];
				}

				delete[] m_imageData;

				bool doEncode = true;

				if (attempts >= max_attempts)
				{
					delete[] m_outBuffer;
					m_outBuffer = nullptr;

					int32 count = m_CurrentRender->world->PostProcessVolumes.Num();

					for (int32 x = 0; x < count; ++x)
					{
						FPostProcessVolumeProperties volume = m_CurrentRender->world->PostProcessVolumes[x]->GetProperties();
						if (volume.bIsUnbound)
						{
							FPostProcessSettings* settings = (FPostProcessSettings*)volume.Settings;

							settings->AutoExposureMaxBrightness = m_cacheAutoExposureMaxBrightness;
							settings->AutoExposureMinBrightness = m_cacheAutoExposureMinBrightness;
							settings->VignetteIntensity = m_cacheVignetteIntensity;
						}
					}

					m_equirect360JobFinished = false;
					finaliseCleanupEquirectJob = true;

					m_CurrentRender.Reset();
					ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");

					doEncode = false;
				}

				if (doEncode)
				{
					FImageView finalImage((const FColor*)m_outBuffer, m_CurrentRender->width, m_CurrentRender->width / 2);

					if (!FImageUtils::CompressImage(imageBytes, *m_CurrentRender->format, finalImage))
					{
						UE_LOG(LogZLCloudPlugin, Error, TEXT("CompressImage failed: %ix%i"), m_CurrentRender->width, m_CurrentRender->width / 2);
						return;
					}

					delete[] m_outBuffer;
					m_outBuffer = nullptr;

					FString pluginDir = FPaths::ProjectPluginsDir();
					FString filePath = FPaths::Combine(pluginDir, "ZLCloudPlugin/Resources/exiftool.exe");
					IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

					UE_LOG(LogZLCloudPlugin, Display, TEXT("exiftool.exe should be placed in %s"), *filePath); // TODO: Ed - delete this once correct directory is established for built project

					if (PlatformFile.FileExists(*filePath))
					{
						UE_LOG(LogZLCloudPlugin, Display, TEXT("exiftool.exe found"));

						m_finalOutpath = FPaths::Combine(m_CurrentRender->path, "FinishedPano.png");
						FFileHelper::SaveArrayToFile(imageBytes, *m_finalOutpath);

						m_imageBytesSize = imageBytes.Num();

						FString MyCommandString = filePath + " -ProjectionType=\"equirectangular\" \"" + m_finalOutpath + "\" -overwrite_original_in_place -preserve";//-quiet
						FString MyCommandString1 = "-ProjectionType=\"equirectangular\" \"" + m_finalOutpath + "\" -overwrite_original_in_place -preserve";//-quiet

						FProcHandle procHandle = FPlatformProcess::CreateProc(*filePath, *MyCommandString1, true, false, false, nullptr, 0, nullptr, nullptr);

						m_injectMetadata = true;
					}
					else
					{
						UE_LOG(LogZLCloudPlugin, Display, TEXT("exiftool.exe not found"));
						m_equirect360JobFinished = true;
					}

					int32 count = m_CurrentRender->world->PostProcessVolumes.Num();

					for (int32 x = 0; x < count; ++x)
					{
						FPostProcessVolumeProperties volume = m_CurrentRender->world->PostProcessVolumes[x]->GetProperties();
						if (volume.bIsUnbound)
						{
							FPostProcessSettings* settings = (FPostProcessSettings*)volume.Settings;

							settings->AutoExposureMaxBrightness = m_cacheAutoExposureMaxBrightness;
							settings->AutoExposureMinBrightness = m_cacheAutoExposureMinBrightness;
							settings->VignetteIntensity = m_cacheVignetteIntensity;
						}
					}

					ZLJobTrace::JOBTRACE_TIMER_END("Encode");
				}
			}
		}

		if (m_CurrentRender->type == ScreenshotType::DEFAULT2D || (m_CurrentRender->type == ScreenshotType::EQUIRECT360 && m_equirect360JobFinished))
		{
			if (m_CurrentRender->m_SavedCVarStates.Num() > 0)
			{
				IConsoleManager& ConsoleMgr = IConsoleManager::Get();

				for (const TPair<FString, FString>& Pair : m_CurrentRender->m_SavedCVarStates)
				{
					if (IConsoleVariable* CVar = ConsoleMgr.FindConsoleVariable(*Pair.Key))
					{
						CVar->Set(*Pair.Value, ECVF_SetByCode);

						UE_LOG(LogZLCloudPlugin, Verbose, TEXT("ZLScreenshot: Restored CVar: %s to %s"), *Pair.Key, *Pair.Value);
					}
				}

				m_CurrentRender->m_SavedCVarStates.Empty();
			}

			ZLJobTrace::JOBTRACE_TIMER_START("ImageDataServerResponse");
			// Allow plugins to drain occlusion results for the capture frame before we build state (e.g. ProcessAccumulatedResults + SetImmediateReadback(false))
			if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
			{
				LocalDelegates->OnBeforeBuildScreenshotStateNative.Broadcast(m_CurrentRender->world);
			}
			// Get capture frame view from plugins (e.g. scene view extension) so we use exact screenshot view for projection
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
				PrepareParams.CaptureWidth = (m_CurrentRender->type == ScreenshotType::DEFAULT2D) ? InSizeX : m_CurrentRender->width;
				PrepareParams.CaptureHeight = (m_CurrentRender->type == ScreenshotType::DEFAULT2D) ? InSizeY : (m_CurrentRender->width / 2);
			}
			if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
			{
				LocalDelegates->OnPrepareScreenshotNative.Broadcast(PrepareParams);
			}

			TSharedPtr<FJsonObject> responseStateData = MakeShareable(new FJsonObject);

			if (m_CurrentRender->postJobCurrentState.IsValid())
				responseStateData->SetObjectField("current_state", m_CurrentRender->postJobCurrentState);

			if (m_CurrentRender->postJobUnmatchedState.IsValid())
				responseStateData->SetObjectField("unprocessed_state", m_CurrentRender->postJobUnmatchedState);

			if (m_CurrentRender->postJobTimeoutState.IsValid())
				responseStateData->SetObjectField("timeout_state", m_CurrentRender->postJobTimeoutState);

			// Allow other plugins (e.g. FeatureNodes) to augment the screenshot response JSON (pass world so only matching manager adds)
			if (UZLCloudPluginDelegates* LocalDelegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
			{
				LocalDelegates->OnBuildScreenshotStateNative.Broadcast(responseStateData, m_CurrentRender->world);
			}

			FString responseDataStr;

			TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&responseDataStr);

			FJsonSerializer::Serialize(responseStateData.ToSharedRef(), writer);

			auto responseDataAnsi = StringCast<ANSICHAR>(*responseDataStr);
			const char* responseDataChar = responseDataAnsi.Get();
			int32 responseDataLen = strlen(responseDataChar);

			TArray64<uint8> responseDataBytes;
			responseDataBytes.Append((uint8*)responseDataChar, responseDataLen);

			auto uidAnsi = StringCast<ANSICHAR>(*m_CurrentRender->uid);
			const char* uidChar = uidAnsi.Get();
			int32 uidLen = strlen(uidChar);

			TArray64<uint8> uidStrBytes;
			uidStrBytes.Append((uint8*)uidChar, uidLen);

			int32 responseLength = responseDataBytes.Num();

			imageBytes.Insert((uint8*)&responseLength, sizeof(int32), 0);
			imageBytes.Insert(responseDataBytes, sizeof(int32));
			imageBytes.Insert(uidStrBytes, sizeof(int32) + responseDataBytes.Num());

#ifdef SUPPORT_LEGACY_MESSAGES
			if(m_CurrentRender->isLegacyMessage)
				m_LauncherComms->SendLauncherMessageBinary("CAPTUREIMAGERESULT", imageBytes);
			else
#endif //SUPPORT_LEGACY_MESSAGES
				m_LauncherComms->SendLauncherMessageBinary("CAPTUREMEDIARESULT", imageBytes);

			if (m_CurrentRender->type == ScreenshotType::EQUIRECT360)
			{
				m_equirect360JobFinished = false;
				finaliseCleanupEquirectJob = true;
			}

			if (Delegates)
			{
				Delegates->OnContentGenerationFinished.Broadcast(m_CurrentRender->type == ScreenshotType::EQUIRECT360);
				Delegates->OnContentGenerationFinishedNative.Broadcast(m_CurrentRender->type == ScreenshotType::EQUIRECT360);
			}

			ZLJobTrace::JOBTRACE_TIMER_END("ImageDataServerResponse");
			ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
			m_CurrentRender.Reset();
		}

		if (finaliseCleanupEquirectJob)
		{
			APlayerCameraManager* PlayerCamera = m_playerController->PlayerCameraManager;
			m_playerController->SetViewTarget(m_initialViewTarget);
			PlayerCamera->UnlockFOV();
			m_panoViewTarget->Destroy();
		}
	}
}

#if UNREAL_5_3_OR_NEWER
void ZLScreenshot::OnMoviePipelineFinished(FMoviePipelineOutputData Results)
{
	if (bCreatedCamera && CameraActor)
	{
		CameraActor->Destroy();
		CameraActor = nullptr;
	}

	if (m_CurrentRender.IsValid())
	{
		if (m_CurrentRender->transparent)
			RestoreTransparentGlassMaterials(m_CurrentRender.Get());
		m_CurrentRender->RevertSettings();
	}

	if (Results.bSuccess && m_CurrentRender.IsValid())
	{
		bool finaliseCleanupEquirectJob = false;

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

		if (m_CurrentRender->type == ScreenshotType::EQUIRECT360)
		{
			m_CurrentRender->lastFaceCompletedID = m_CurrentRender->faceID - 1;

			FQuat quat = m_startRotation * m_faceRotation[m_CurrentRender->lastFaceCompletedID];

			if (m_panoViewTarget)
				m_panoViewTarget->SetActorRotation(quat.Rotator());
			else
				UE_LOG(LogZLCloudPlugin, Error, TEXT("m_panoViewTarget is a nullptr"));
		}

		FString outputDir = OutputSetting->OutputDirectory.Path;
		FString outputFileName = (m_CurrentRender->type == ScreenshotType::EQUIRECT360) ? m_CurrentRender->uid + TEXT("_FACE_") + FString::FromInt(m_CurrentRender->lastFaceCompletedID) + TEXT(".") + Extension : m_CurrentRender->uid + TEXT(".") + Extension;
		FString outputFilePath = FPaths::Combine(outputDir, outputFileName);

		bool cleanupFile = m_CurrentRender->path == "";


		bool isVideoCapture = !m_CurrentRender->videoFormat.IsEmpty();
		if (isVideoCapture)
		{
			FString ffmpegSourcePath = FPaths::Combine(outputDir, m_CurrentRender->uid + TEXT(".%04d.") + Extension);	//%04d must match ZeroPadFrameNumbers in settings
			FString videoOutputPath = FPaths::Combine(outputDir, m_CurrentRender->uid + TEXT(".") + m_CurrentRender->videoFormat);
			
			FString pluginDir = FPaths::ProjectPluginsDir();
			FString exePath = FPaths::Combine(pluginDir, "ZLCloudPlugin/Resources/ffmpeg.exe");
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			if (PlatformFile.FileExists(*exePath))
			{
				UE_LOG(LogZLCloudPlugin, Display, TEXT("ffmpeg.exe found"));

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

				FString commandArgs = "-framerate " + frameRateStr + " -pattern_type sequence -i \"" + ffmpegSourcePath + "\" " + codecStr + " -y \"" + videoOutputPath + "\"";

				FMonitoredProcess ffmpegProcess(*exePath, *commandArgs, true);
			//	ffmpegProcess.OnCompleted().BindLambda([&ffmpegReturnCode](int32 _ReturnCode) { ffmpegReturnCode = _ReturnCode; });
				ffmpegProcess.OnOutput().BindLambda([](FString outputLine) { UE_LOG(LogZLCloudPlugin, Display, TEXT("%s"), *outputLine); });

				if (ffmpegProcess.Launch())
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("ffmpeg spawned with args %s"), *commandArgs);
					while (ffmpegProcess.Update())
					{
						// Poll until process has finished
						FPlatformProcess::Sleep(0.02f);
					}

					int32 ffmpegReturnCode = ffmpegProcess.GetReturnCode();
					if (ffmpegReturnCode != 0)
					{
						UE_LOG(LogZLCloudPlugin, Warning, TEXT("ffmpeg failed to complete (%d)"), ffmpegReturnCode);
					}

					FString ffmpegOutput = ffmpegProcess.GetFullOutputWithoutDelegate();
					if(!ffmpegOutput.IsEmpty())	//almost certainly empty if we supplied a callback for OnOutput
						UE_LOG(LogZLCloudPlugin, Display, TEXT("ffmpeg final output %s"), *ffmpegOutput);
				}
				else
				{
					UE_LOG(LogZLCloudPlugin, Warning, TEXT("ffmpeg failed to spawn with args %s"), *commandArgs);
				}
			}
			else
			{
				UE_LOG(LogZLCloudPlugin, Warning, TEXT("ffmpeg.exe not found"));
			}

			if (cleanupFile)	//delete all the source files
			{
				FString wildcardFilePath = m_CurrentRender->uid + TEXT(".*.") + Extension;
				wildcardFilePath = FPaths::Combine(outputDir, wildcardFilePath);
				TArray<FString> FoundFiles;
				IFileManager::Get().FindFiles(FoundFiles, *wildcardFilePath, true, false);
				for (FString fileName : FoundFiles)
				{
					FString FilePath = FPaths::Combine(outputDir, fileName);
					IFileManager::Get().Delete(*FilePath, true);
				}
			}

			outputFilePath = videoOutputPath;	//we'll send this and delete it later
		}




		if (m_CurrentRender->type == ScreenshotType::DEFAULT2D)
		{
			if (Delegates)
			{
				Delegates->OnContentGenerationFinished.Broadcast(m_CurrentRender->type == ScreenshotType::EQUIRECT360);
				Delegates->OnContentGenerationFinishedNative.Broadcast(m_CurrentRender->type == ScreenshotType::EQUIRECT360);
			}

			TArray<FString> FoundFiles;
			IFileManager::Get().FindFiles(FoundFiles, *outputFilePath, true, false);

			if (FoundFiles.Num() > 0)
			{
				FString FilePath = outputFilePath;
				if (FFileHelper::LoadFileToArray(imageBytes, *FilePath))
				{
					ZLJobTrace::JOBTRACE_TIMER_END("Render");
					ZLJobTrace::JOBTRACE_TIMER_START("Encode");

					ZLJobTrace::JOBTRACE_TIMER_START("ImageDataServerResponse");
					TSharedPtr<FJsonObject> responseStateData = MakeShareable(new FJsonObject);

					if (m_CurrentRender->postJobCurrentState.IsValid())
						responseStateData->SetObjectField("current_state", m_CurrentRender->postJobCurrentState);

					if (m_CurrentRender->postJobUnmatchedState.IsValid())
						responseStateData->SetObjectField("unprocessed_state", m_CurrentRender->postJobUnmatchedState);

					if (m_CurrentRender->postJobTimeoutState.IsValid())
						responseStateData->SetObjectField("timeout_state", m_CurrentRender->postJobTimeoutState);

					FString responseDataStr;

					TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&responseDataStr);

					FJsonSerializer::Serialize(responseStateData.ToSharedRef(), writer);

					auto responseDataAnsi = StringCast<ANSICHAR>(*responseDataStr);
					const char* responseDataChar = responseDataAnsi.Get();
					int32 responseDataLen = strlen(responseDataChar);

					TArray64<uint8> responseDataBytes;
					responseDataBytes.Append((uint8*)responseDataChar, responseDataLen);

					auto uidAnsi = StringCast<ANSICHAR>(*m_CurrentRender->uid);
					const char* uidChar = uidAnsi.Get();
					int32 uidLen = strlen(uidChar);

					TArray64<uint8> uidStrBytes;
					uidStrBytes.Append((uint8*)uidChar, uidLen);

					int32 responseLength = responseDataBytes.Num();

					imageBytes.Insert((uint8*)&responseLength, sizeof(int32), 0);
					imageBytes.Insert(responseDataBytes, sizeof(int32));
					imageBytes.Insert(uidStrBytes, sizeof(int32) + responseDataBytes.Num());

#ifdef SUPPORT_LEGACY_MESSAGES
					if (m_CurrentRender->isLegacyMessage)
						m_LauncherComms->SendLauncherMessageBinary("CAPTUREIMAGERESULT", imageBytes);
					else
#endif //SUPPORT_LEGACY_MESSAGES
						m_LauncherComms->SendLauncherMessageBinary("CAPTUREMEDIARESULT", imageBytes);

					ZLJobTrace::JOBTRACE_TIMER_END("ImageDataServerResponse");
					ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
					m_CurrentRender.Reset();

					if (cleanupFile)
						IFileManager::Get().Delete(*FilePath, true);
				}
				else
				{
					FString errorMsg = "Failed to load rendered image from disk.";
					ZLScreenshot::SendImageFailureResponse(errorMsg);
					m_CurrentRender.Reset();
					ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
				}
			}
			else
			{
				FString errorMsg = "Movie Render Pipeline failed to render.";
				ZLScreenshot::SendImageFailureResponse(errorMsg);
				m_CurrentRender.Reset();
				ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
			}
		}
		else if (m_CurrentRender->type == ScreenshotType::EQUIRECT360)
		{
			TArray<FColor> InImageData;
			int32 FaceWidth, FaceHeight;
			if (!LoadImageAsFColorArray(outputFilePath, InImageData, FaceWidth, FaceHeight))
			{
				FString errorMsg = "Failed to load rendered MRQ image from disk for stitching.";
				ZLScreenshot::SendImageFailureResponse(errorMsg);
				m_CurrentRender.Reset();
				return;
			}

			if (m_CurrentRender->path != "")
			{
				//Render is done for this face
				FImageView image((const FColor*)InImageData.GetData(), m_faceSize, m_faceSize);
				if (!FImageUtils::CompressImage(imageBytes, *m_CurrentRender->format, image))
				{
					UE_LOG(LogZLCloudPlugin, Error, TEXT("CompressImage failed: %ix%i"), m_faceSize, m_faceSize);
					FString errorMsg = "CompressImage failed.";
					ZLScreenshot::SendImageFailureResponse(errorMsg);
					m_CurrentRender.Reset();
					ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
					return;
				}

				std::string faceID = std::to_string(m_CurrentRender->lastFaceCompletedID);
				FString FfaceID(faceID.c_str());

				FString timestamp = FDateTime::Now().ToString();
				FString dateCleaned = timestamp.Replace(TEXT("."), TEXT("_"));
				FString filename = m_CurrentRender->uid + "_" + dateCleaned + "_" + FfaceID + "." + m_CurrentRender->format;
				FString outpath = FPaths::Combine(m_CurrentRender->path, filename);
				FFileHelper::SaveArrayToFile(imageBytes, *outpath);
				UE_LOG(LogZLCloudPlugin, Display, TEXT("Screen shot saved to: %s"), *outpath);
			}

			FString FormattedString = FString::Printf(TEXT("CaptureFaceID%d"), m_CurrentRender->lastFaceCompletedID);
			ZLJobTrace::JOBTRACE_TIMER_END(FormattedString);

			if (!ZLJobTrace::JOBTRACE_IS_CURRENTLY_TIMING("RunComputeFaces"))
				ZLJobTrace::JOBTRACE_TIMER_START("RunComputeFaces");

			memcpy(m_imageData[m_CurrentRender->lastFaceCompletedID], (void*)InImageData.GetData(), m_numPixels * 4);

			int dataSize = m_CurrentRender->width * m_CurrentRender->width * 2;

			void* textureData = (void*)m_imageData[m_CurrentRender->lastFaceCompletedID];
			int width = m_CurrentRender->width;
			int faceSize = m_faceSize;
			int faceID = m_CurrentRender->faceID;
			void* outBuffer = (void*)m_outBuffer;

			ENQUEUE_RENDER_COMMAND(RunPanoComputeShader)
				([=](FRHICommandListImmediate& RHICmdList) {
				CloudStream2DLL::TextureToPlugin(textureData, dataSize, width, faceSize, faceID, (unsigned char*)outBuffer);
			});

			if (m_CurrentRender->faceID == 6)
			{
				ZLJobTrace::JOBTRACE_TIMER_END("Render");
				int attempts = 0;
				const int max_attempts = 5000;

				// Wait for queued compute shader jobs on render thread to complete for all cube faces.
				while (!CloudStream2DLL::IsPanoImageReady())
				{
					FPlatformProcess::Sleep(0.01f);

					attempts++;

					if (attempts >= max_attempts)
					{
						finaliseCleanupEquirectJob = true;
						m_equirect360JobFinished = false;

						FString errorMsg = "Compute shader in Cloudstream2 plug-in has taken over 50 seconds to respond.";
						UE_LOG(LogZLCloudPlugin, Error, TEXT("%s"), *errorMsg);
						ZLScreenshot::SendImageFailureResponse(errorMsg);
						break;
					}
				}

				ZLJobTrace::JOBTRACE_TIMER_END("RunComputeFaces");
				ZLJobTrace::JOBTRACE_TIMER_START("Encode");

				int time = attempts * 10;

				for (int j = 0; j < 6; j++)
				{
					if (m_imageData[j])
						delete[] m_imageData[j];
				}

				delete[] m_imageData;

				bool doEncode = true;

				if (attempts >= max_attempts)
				{
					delete[] m_outBuffer;
					m_outBuffer = nullptr;

					int32 count = m_CurrentRender->world->PostProcessVolumes.Num();

					for (int32 x = 0; x < count; ++x)
					{
						FPostProcessVolumeProperties volume = m_CurrentRender->world->PostProcessVolumes[x]->GetProperties();
						if (volume.bIsUnbound)
						{
							FPostProcessSettings* settings = (FPostProcessSettings*)volume.Settings;

							settings->AutoExposureMaxBrightness = m_cacheAutoExposureMaxBrightness;
							settings->AutoExposureMinBrightness = m_cacheAutoExposureMinBrightness;
							settings->VignetteIntensity = m_cacheVignetteIntensity;
						}
					}

					m_equirect360JobFinished = false;
					finaliseCleanupEquirectJob = true;

					m_CurrentRender.Reset();
					ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");

					doEncode = false;
				}

				if (doEncode)
				{
					FImageView finalImage((const FColor*)m_outBuffer, m_CurrentRender->width, m_CurrentRender->width / 2);

					if (!FImageUtils::CompressImage(imageBytes, *m_CurrentRender->format, finalImage))
					{
						UE_LOG(LogZLCloudPlugin, Error, TEXT("CompressImage failed: %ix%i"), m_CurrentRender->width, m_CurrentRender->width / 2);
						return;
					}

					delete[] m_outBuffer;
					m_outBuffer = nullptr;

					FString pluginDir = FPaths::ProjectPluginsDir();
					FString filePath = FPaths::Combine(pluginDir, "ZLCloudPlugin/Resources/exiftool.exe");
					IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

					UE_LOG(LogZLCloudPlugin, Display, TEXT("exiftool.exe should be placed in %s"), *filePath); // TODO: Ed - delete this once correct directory is established for built project

					if (PlatformFile.FileExists(*filePath))
					{
						UE_LOG(LogZLCloudPlugin, Display, TEXT("exiftool.exe found"));

						m_finalOutpath = FPaths::Combine(m_CurrentRender->path, "FinishedPano.png");
						FFileHelper::SaveArrayToFile(imageBytes, *m_finalOutpath);

						m_imageBytesSize = imageBytes.Num();

						FString MyCommandString = filePath + " -ProjectionType=\"equirectangular\" \"" + m_finalOutpath + "\" -overwrite_original_in_place -preserve";//-quiet
						FString MyCommandString1 = "-ProjectionType=\"equirectangular\" \"" + m_finalOutpath + "\" -overwrite_original_in_place -preserve";//-quiet

						FProcHandle procHandle = FPlatformProcess::CreateProc(*filePath, *MyCommandString1, true, false, false, nullptr, 0, nullptr, nullptr);

						m_injectMetadata = true;
					}
					else
					{
						UE_LOG(LogZLCloudPlugin, Display, TEXT("exiftool.exe not found"));
						m_equirect360JobFinished = true;
						finaliseCleanupEquirectJob = true;
					}

					int32 count = m_CurrentRender->world->PostProcessVolumes.Num();

					for (int32 x = 0; x < count; ++x)
					{
						FPostProcessVolumeProperties volume = m_CurrentRender->world->PostProcessVolumes[x]->GetProperties();
						if (volume.bIsUnbound)
						{
							FPostProcessSettings* settings = (FPostProcessSettings*)volume.Settings;

							settings->AutoExposureMaxBrightness = m_cacheAutoExposureMaxBrightness;
							settings->AutoExposureMinBrightness = m_cacheAutoExposureMinBrightness;
							settings->VignetteIntensity = m_cacheVignetteIntensity;
						}
					}

					ZLJobTrace::JOBTRACE_TIMER_END("Encode");
				}

				if (m_equirect360JobFinished)
				{
					if (imageBytes.Num() > 0)
					{
						ZLJobTrace::JOBTRACE_TIMER_START("ImageDataServerResponse");
						TSharedPtr<FJsonObject> responseStateData = MakeShareable(new FJsonObject);

						if (m_CurrentRender->postJobCurrentState.IsValid())
							responseStateData->SetObjectField("current_state", m_CurrentRender->postJobCurrentState);

						if (m_CurrentRender->postJobUnmatchedState.IsValid())
							responseStateData->SetObjectField("unprocessed_state", m_CurrentRender->postJobUnmatchedState);

						if (m_CurrentRender->postJobTimeoutState.IsValid())
							responseStateData->SetObjectField("timeout_state", m_CurrentRender->postJobTimeoutState);

						FString responseDataStr;

						TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&responseDataStr);

						FJsonSerializer::Serialize(responseStateData.ToSharedRef(), writer);

						auto responseDataAnsi = StringCast<ANSICHAR>(*responseDataStr);
						const char* responseDataChar = responseDataAnsi.Get();
						int32 responseDataLen = strlen(responseDataChar);

						TArray64<uint8> responseDataBytes;
						responseDataBytes.Append((uint8*)responseDataChar, responseDataLen);

						auto uidAnsi = StringCast<ANSICHAR>(*m_CurrentRender->uid);
						const char* uidChar = uidAnsi.Get();
						int32 uidLen = strlen(uidChar);

						TArray64<uint8> uidStrBytes;
						uidStrBytes.Append((uint8*)uidChar, uidLen);

						int32 responseLength = responseDataBytes.Num();

						imageBytes.Insert((uint8*)&responseLength, sizeof(int32), 0);
						imageBytes.Insert(responseDataBytes, sizeof(int32));
						imageBytes.Insert(uidStrBytes, sizeof(int32) + responseDataBytes.Num());

#ifdef SUPPORT_LEGACY_MESSAGES
						if (m_CurrentRender->isLegacyMessage)
							m_LauncherComms->SendLauncherMessageBinary("CAPTUREIMAGERESULT", imageBytes);
						else
#endif //SUPPORT_LEGACY_MESSAGES
							m_LauncherComms->SendLauncherMessageBinary("CAPTUREMEDIARESULT", imageBytes);

						if (m_CurrentRender->type == ScreenshotType::EQUIRECT360)
						{
							finaliseCleanupEquirectJob = true;
							m_equirect360JobFinished = false;
						}

						if (Delegates)
						{
							Delegates->OnContentGenerationFinished.Broadcast(m_CurrentRender->type == ScreenshotType::EQUIRECT360);
							Delegates->OnContentGenerationFinishedNative.Broadcast(m_CurrentRender->type == ScreenshotType::EQUIRECT360);
						}

						ZLJobTrace::JOBTRACE_TIMER_END("ImageDataServerResponse");
						ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
						m_CurrentRender.Reset();
					}
					else
					{
						FString errorMsg = "Movie Render Pipeline failed to render.";
						ZLScreenshot::SendImageFailureResponse(errorMsg);
						m_CurrentRender.Reset();
						ZLJobTrace::JOBTRACE_TIMER_END("TotalJobTime");
					}
				}
			}

			if (cleanupFile)
				IFileManager::Get().Delete(*outputFilePath, true);
		}

		if (finaliseCleanupEquirectJob)
		{
			APlayerCameraManager* PlayerCamera = m_playerController->PlayerCameraManager;
			m_playerController->SetViewTarget(m_initialViewTarget);
			PlayerCamera->UnlockFOV();
			m_panoViewTarget->Destroy();
		}
	}
}
#endif

