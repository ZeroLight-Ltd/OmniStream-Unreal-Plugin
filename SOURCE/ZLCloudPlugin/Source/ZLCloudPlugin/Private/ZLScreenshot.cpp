// Copyright ZeroLight ltd. All Rights Reserved.
#include "ZLScreenshot.h"
#include "LauncherComms.h"
#include "ZLCloudPluginPrivate.h"
#include "Engine/GameViewportClient.h"

#if UNREAL_5_3_OR_NEWER
#include "HighResScreenshot.h"
#include "ImageUtils.h"
#else
#include "Engine/Public/HighResScreenshot.h"
#include "Engine/Public/ImageUtils.h"
#endif

#include "Widgets/SViewport.h"
#include <EditorZLCloudPluginSettings.h>

using namespace ZLCloudPlugin;

ZLScreenshotJob::ZLScreenshotJob(int32 InWidth, int32 InHeight, FString& InFormat, FString& InPath, FString& InUID, UWorld* InWorld, const TSharedPtr<FJsonObject>* stateRequestData, ScreenshotType InScreenshotType)
: width(InWidth), height(InHeight), world(InWorld), format(InFormat), path(InPath), uid(InUID), type(InScreenshotType)
{
	if (stateRequestData != nullptr)
	{
		stateData = MakeShared<FJsonObject>();
		FJsonObject::Duplicate(*stateRequestData, stateData);
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

			m_LauncherComms->SendLauncherMessageBinary("CAPTUREIMAGERESULT", imageBytes);
			m_CurrentRender.Reset();

			m_equirect360JobFinished = false;
			m_playerController->SetViewTarget(m_initialViewTarget);
			m_panoViewTarget->Destroy();

			if (Delegates)
			{
				Delegates->OnPostPanoCapture.Broadcast();
				Delegates->OnPostPanoCaptureNative.Broadcast();
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
				FString stateDataStr;

				TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&stateDataStr);

				TSharedRef<FJsonObject> stateData = m_CurrentRender->stateData.ToSharedRef();

				if (!FJsonSerializer::Serialize(stateData, writer))
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Error broadcasting OnReceiveData for state request"));
				}

				if (Delegates)
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("OnReceiveData Screenshot Request"));
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
			}
			else
			{
				if (m_CurrentRender->jobStateRequestFinished) //If state request finished but was not fully successful need to pass on rejection here and inform IM of what failed
				{
					FString errorMsg = "Timeout reached waiting for state data to match request.";

					ZLScreenshot::SendImageFailureResponse(errorMsg);

					m_CurrentRender.Reset();
				}
			}
		}

		if (captureScreenshot && !m_CurrentRender->jobCaptureStarted || m_CurrentRender->type == ScreenshotType::EQUIRECT360)
		{
			if (UGameViewportClient* GameViewport = m_CurrentRender->world->GetGameViewport())
			{
				FSceneViewport* SceneViewport = GameViewport->GetGameViewport();
				FViewport* Viewport = GameViewport->Viewport;
				if (Viewport != nullptr)
				{
					if (m_CurrentRender->type == ScreenshotType::DEFAULT2D)
					{
						//HighResScreenshotConfig.SetFilename(Filename);
						//HighResScreenshotConfig.SetMaskEnabled(bMaskEnabled);
						//HighResScreenshotConfig.SetHDRCapture(bCaptureHDR);
						m_CurrentRender->captureStartTime = FPlatformTime::Seconds();

						double currentTime = m_CurrentRender->world->TimeSeconds;
						double lastRenderTime = m_CurrentRender->world->LastRenderTime;
						float f_lastRenderTime = (float)lastRenderTime;
						float timeDelta = currentTime - f_lastRenderTime;
						const bool bFirstFrameOrTimeWasReset = timeDelta < -0.0001f || currentTime < 0.0001f;

						if (!bFirstFrameOrTimeWasReset)
						{
							FHighResScreenshotConfig& HighResScreenshotConfig = GetHighResScreenshotConfig();
							HighResScreenshotConfig.SetResolution(m_CurrentRender->width, m_CurrentRender->height);

							UE_LOG(LogZLCloudPlugin, Display, TEXT("TakeHighResScreenShot - Start Time %f - Start Frame %11d"), m_CurrentRender->captureStartTime, GFrameCounter);
							Viewport->TakeHighResScreenShot();
							m_CurrentRender->jobCaptureStarted = true;
						}
						else
						{
							UE_LOG(LogZLCloudPlugin, Display, TEXT("Wait to take screenshot - Time Delta %f"), timeDelta);
						}
					}
					else if (m_CurrentRender->type == ScreenshotType::EQUIRECT360)
					{
						if (GEngine)
						{
							if (UWorld* World = m_CurrentRender->world)
							{
								if (m_CurrentRender->faceID == -1)
								{
									m_playerController = World->GetFirstPlayerController();

									FVector startLoc(0, 0, 0);
									FRotator startRot(0, 0, 0);

									m_playerController->GetPlayerViewPoint(startLoc, startRot);
									m_startRotation = startRot.Quaternion();
									m_faceSize = 8 * ((uint32)floor(2 * (double)m_CurrentRender->height / PI / 8));

									m_initialViewTarget = m_playerController->GetViewTarget();
									m_panoViewTarget = World->SpawnActor<class ACameraActor>(startLoc, startRot);

									UCameraComponent* cam = m_panoViewTarget->GetCameraComponent();
									cam->SetAspectRatio(1.0f);

									m_playerController->SetViewTarget(m_panoViewTarget);
								}

								if (m_CurrentRender->faceID == 0)
								{
									FHighResScreenshotConfig& HighResScreenshotConfig = GetHighResScreenshotConfig();
									HighResScreenshotConfig.SetResolution(m_faceSize, m_faceSize);

									FIntPoint res = FIntPoint(m_faceSize, m_faceSize);
									GEngine->GameUserSettings->SetScreenResolution(res);

									APlayerCameraManager* PlayerCamera = m_playerController->PlayerCameraManager;
									PlayerCamera->SetFOV(90.0f);

									int32 count = m_CurrentRender->world->PostProcessVolumes.Num();

									for (int32 x = 0; x < count; ++x)
									{
										FPostProcessVolumeProperties volume = m_CurrentRender->world->PostProcessVolumes[x]->GetProperties();
										if (volume.bIsUnbound)
										{
											FPostProcessSettings* settings = (FPostProcessSettings*)volume.Settings;

											settings->bOverride_AutoExposureMaxBrightness = true;
											settings->bOverride_AutoExposureMinBrightness = true;

											m_cacheAutoExposureMaxBrightness = settings->AutoExposureMaxBrightness;
											m_cacheAutoExposureMinBrightness = settings->AutoExposureMinBrightness;
											m_cacheVignetteIntensity = settings->VignetteIntensity;

											settings->AutoExposureMaxBrightness = 4;
											settings->AutoExposureMinBrightness = 4;

											settings->bOverride_VignetteIntensity = true;
											settings->VignetteIntensity = 0;
										}
									}

									if (Delegates)
									{
										Delegates->OnPrePanoCapture.Broadcast();
										Delegates->OnPrePanoCaptureNative.Broadcast();
									}
								}

								if (m_CurrentRender->faceID >= 0 && m_CurrentRender->faceID < 6)
								{
									Viewport->TakeHighResScreenShot();

									FQuat quat = m_startRotation * m_faceRotation[m_CurrentRender->faceID];

									if (m_panoViewTarget)
										m_panoViewTarget->SetActorRotation(quat.Rotator());
									else
										UE_LOG(LogZLCloudPlugin, Error, TEXT("m_panoViewTarget is a nullptr"));
								}

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

bool ZLScreenshot::RequestScreenshot(const char* settingsJson, UWorld* InWorld, FString& errorMsgOut)
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

		//Optional: requires state-match complete callback before screenshot is taken
		if (!JsonParsed->TryGetObjectField(FString("state"), stateRequestData))
		{

		}
		else if (stateRequestData->IsValid() && stateRequestData->Get()->Values.Num() == 0)
		{
			stateRequestData = nullptr;
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
		}
		else
			SetFrameCountCmd = FString::Printf(TEXT("r.HighResScreenshotDelay %d"), (isRaytracing) ? s_hiresScreenshotFrameCountRaytracing : s_hiresScreenshotFrameCount);
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

	m_LauncherComms->SendLauncherMessage("CAPTUREIMAGEFAIL", responseDataStr);
}

void ZLScreenshot::OnScreenshotComplete(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData)
{
	check(IsInGameThread());

	if (m_CurrentRender.IsValid())
	{
		double capEndTime = FPlatformTime::Seconds();
		double elapsed = capEndTime - m_CurrentRender->captureStartTime;

		UE_LOG(LogZLCloudPlugin, Display, TEXT("Call OnScreenshotComplete: %ix%i - Duration %f seconds - End Frame %11d"), InSizeX, InSizeY, elapsed, GFrameCounter);

		TArray64<uint8> imageBytes;

		if (m_CurrentRender->type == ScreenshotType::DEFAULT2D)
		{
			//Render is done, allow next request
			FImageView image((const FColor*)InImageData.GetData(), InSizeX, InSizeY);
			if (!FImageUtils::CompressImage(imageBytes, *m_CurrentRender->format, image))
			{
				UE_LOG(LogZLCloudPlugin, Error, TEXT("CompressImage failed: %ix%i"), InSizeX, InSizeY);
				FString errorMsg = "CompressImage failed.";
				ZLScreenshot::SendImageFailureResponse(errorMsg);
				m_CurrentRender.Reset();
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

			int dataSize = m_CurrentRender->width * m_CurrentRender->width * 2;

			if (m_CurrentRender->faceID == 1)
			{
				m_outBuffer = new unsigned char[dataSize];
				m_imageData = new int*[6];
				m_numPixels = m_faceSize * m_faceSize;

				for (int i = 0; i < 6; i++)
					m_imageData[i] = new int[m_numPixels];
			}

			memcpy(m_imageData[m_CurrentRender->faceID - 1], (void*)InImageData.GetData(), m_numPixels * 4);

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
						m_playerController->SetViewTarget(m_initialViewTarget);
						m_panoViewTarget->Destroy();
						
						if (Delegates)
						{
							Delegates->OnPostPanoCapture.Broadcast();
							Delegates->OnPostPanoCaptureNative.Broadcast();
						}

						FString errorMsg = "Compute shader in Cloudstream2 plug-in has taken over 5 seconds to respond.";
						UE_LOG(LogZLCloudPlugin, Error, TEXT("%s"), *errorMsg);
						ZLScreenshot::SendImageFailureResponse(errorMsg);
						break;
					}
				}

				int time = attempts * 10;

				for (int j = 0; j < 6; j++)
				{
					if (m_imageData[j])
						delete[] m_imageData[j];
				}

				delete[] m_imageData;

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
					m_playerController->SetViewTarget(m_initialViewTarget);
					m_panoViewTarget->Destroy();

					if (Delegates)
					{
						Delegates->OnPostPanoCapture.Broadcast();
						Delegates->OnPostPanoCaptureNative.Broadcast();
					}

					m_CurrentRender.Reset();

					return;
				}

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
			}
		}

		if (m_CurrentRender->type == ScreenshotType::DEFAULT2D || (m_CurrentRender->type == ScreenshotType::EQUIRECT360 && m_equirect360JobFinished))
		{
			TSharedPtr<FJsonObject> responseStateData = MakeShareable(new FJsonObject);

			if (m_CurrentRender->postJobCurrentState.IsValid())
				responseStateData->SetObjectField("current_state", m_CurrentRender->postJobCurrentState);

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

			m_LauncherComms->SendLauncherMessageBinary("CAPTUREIMAGERESULT", imageBytes);

			if (m_CurrentRender->type == ScreenshotType::EQUIRECT360)
			{
				m_equirect360JobFinished = false;
				m_playerController->SetViewTarget(m_initialViewTarget);
				m_panoViewTarget->Destroy();

				if (Delegates)
				{
					Delegates->OnPostPanoCapture.Broadcast();
					Delegates->OnPostPanoCaptureNative.Broadcast();
				}
			}

			m_CurrentRender.Reset();
		}
	}
}
