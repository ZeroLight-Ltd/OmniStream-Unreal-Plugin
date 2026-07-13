// Copyright ZeroLight ltd. All Rights Reserved.

#include "CloudStream2.h"
#include "ZLCloudPluginPrivate.h"
#include "Interfaces/IPluginManager.h"
#include "Utils.h"
#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameEngine.h"
#include "RenderingThread.h"
#include "FeatureNodesSyncGPUFence.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <VersionHelpers.h>
#include <d3d11.h>
#include <d3d12.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if WITH_EDITOR
#include "Interfaces/IMainFrameModule.h"
#include "LevelEditorViewport.h"
#include "LevelEditor.h"
#include "Slate/SceneViewport.h"
#include "SLevelViewport.h"
#endif

#include "EditorZLCloudPluginSettings.h"

using namespace ZLCloudPlugin;

void* CloudStream2::m_CloudStream2DLLHandle = nullptr;
bool CloudStream2::m_pluginInitialised = false;
bool CloudStream2::m_pluginReady = false;
bool CloudStream2::m_cloudSettingsSet = false;
bool CloudStream2::m_inputDeactivate = false;
bool CloudStream2::m_inputIgnore = false;
bool CloudStream2::m_messageHandlerIgnore = false;

int CloudStream2::m_TargetFPS = 0;
int CloudStream2::m_LatencyValue = 0;
int CloudStream2::m_ForcedFrames = 0;
int CloudStream2::m_SentFramesCount = 0;
bool CloudStream2::m_GotMouseData = false;
FVector3f CloudStream2::m_MousePosition = FVector3f::ZeroVector;

bool CloudStream2::m_LastCameraMoved = true;
ZLStopwatch CloudStream2::m_StreamTimer;

bool CloudStream2::m_isFeatureNodesSyncCapable = false;

#if UNREAL_5_5_OR_NEWER
FTextureRHIRef CloudStream2::m_IntermediateTexture = nullptr;
#else
FTexture2DRHIRef CloudStream2::m_IntermediateTexture = nullptr;
#endif

FGPUFenceRHIRef CloudStream2::m_TextureReleaseFence = nullptr;

bool CloudStream2::m_NeedsResolutionChange = false;
bool CloudStream2::m_BackToInitialCameraConfig = false;
int CloudStream2::m_FramesToSkipAfterResolutionChange = 0;
bool CloudStream2::m_IsChangingTexture = false;

InterruptionReason CloudStream2::m_Interruption = InterruptionReason::NO_INTERRUPTION;
FCriticalSection CloudStream2::m_InterruptionMutex;

int CloudStream2::m_defaultStreamWidth = 1280;
int CloudStream2::m_defaultStreamHeight = 720;

CloudStream2DLL::EncoderRequirements CloudStream2::m_FrameRequirements;

TSharedPtr<IZLCloudPluginInputHandler> CloudStream2::m_InputHandler = nullptr;

TSharedPtr<ZLAudioSubmixCapturer> CloudStream2::m_audioSubmixCapturer = nullptr;
bool CloudStream2::m_audioInitialised = false;

bool CloudStream2::IsDLLValid()
{
	return m_pluginInitialised && m_CloudStream2DLLHandle != nullptr;
}

void CloudStream2::InitPlugin()
{
	if (m_pluginInitialised)
	{
		// Already initialized, verify handle is still valid
		if (!m_CloudStream2DLLHandle)
		{
			UE_LOG(LogZLCloudPlugin, Warning, TEXT("CloudStream2 plugin marked as initialized but DLL handle is null, reinitializing..."));
			m_pluginInitialised = false;
		}
		else
		{
			return;
		}
	}

	// Get the base directory of this plugin
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin("ZLCloudPlugin");
	if (!Plugin.IsValid())
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Failed to find ZLCloudPlugin in plugin manager"));
		m_pluginInitialised = false;
		m_CloudStream2DLLHandle = nullptr;
		return;
	}

	FString BaseDir = Plugin->GetBaseDir();
	if (BaseDir.IsEmpty())
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("ZLCloudPlugin base directory is empty"));
		m_pluginInitialised = false;
		m_CloudStream2DLLHandle = nullptr;
		return;
	}

	FString LibraryPath;
#if PLATFORM_WINDOWS
	LibraryPath = FPaths::Combine(*BaseDir, TEXT("Binaries/ThirdParty/CloudStream2/Win64/CloudStream2.dll"));
#elif PLATFORM_MAC
	LibraryPath = FPaths::Combine(*BaseDir, TEXT("Source/ThirdParty/CloudStream2/Mac/Release/CloudStream2.dylib"));
#elif PLATFORM_LINUX
	LibraryPath = FPaths::Combine(*BaseDir, TEXT("Binaries/ThirdParty/CloudStream2/Linux/x86_64-unknown-linux-gnu/CloudStream2.so"));
#endif // PLATFORM_WINDOWS

	if (LibraryPath.IsEmpty())
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("CloudStream2 library path is empty for current platform"));
		m_pluginInitialised = false;
		m_CloudStream2DLLHandle = nullptr;
		return;
	}

	// Check if file exists before attempting to load
	if (!FPaths::FileExists(LibraryPath))
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("CloudStream2 DLL not found at path: %s"), *LibraryPath);
		m_pluginInitialised = false;
		m_CloudStream2DLLHandle = nullptr;
		return;
	}

	m_CloudStream2DLLHandle = FPlatformProcess::GetDllHandle(*LibraryPath);

	if (m_CloudStream2DLLHandle)
	{
		//CloudStream2DLL::CloudStream2DebugPopup();
		m_pluginInitialised = true;

		CloudStream2DLL::SetUEDebugFunction(&CloudStream2::PluginPrint);

#if WITH_EDITOR
		if (GIsEditor) //Catch because standalone level play annoyingly runs with WITH_EDITOR defined
		{
			//Set to true when PIE event happens
			SetInputHandling(false);
			//SetMessageHandling(false);
		}
#endif
		UE_LOG(LogZLCloudPlugin, Display, TEXT("CloudStream2 DLL loaded successfully from: %s"), *LibraryPath);
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Failed to load CloudStream2 DLL from: %s"), *LibraryPath);
		m_pluginInitialised = false;
		m_CloudStream2DLLHandle = nullptr;
		return;
	}
}

void CloudStream2::FreePlugin()
{
	if (m_pluginInitialised && m_CloudStream2DLLHandle)
	{
		if (IsDLLValid())
		{
			CloudStream2DLL::Destroy();
		}
		
		FPlatformProcess::FreeDllHandle(m_CloudStream2DLLHandle);
		UE_LOG(LogZLCloudPlugin, Display, TEXT("CloudStream2 : Target FPS %d"), m_TargetFPS);
		m_CloudStream2DLLHandle = nullptr;
	}
	else if (m_CloudStream2DLLHandle)
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("Freeing CloudStream2 DLL handle that was not properly initialized"));
		FPlatformProcess::FreeDllHandle(m_CloudStream2DLLHandle);
		m_CloudStream2DLLHandle = nullptr;
	}

	if (m_audioSubmixCapturer)
	{
		if (m_audioInitialised)
			m_audioSubmixCapturer->Uninitialise();

		m_audioSubmixCapturer.Reset();
		m_audioSubmixCapturer = nullptr;
	}
	m_audioInitialised = false;

	m_pluginReady = false;
	m_cloudSettingsSet = false;
	m_pluginInitialised = false;
	m_inputDeactivate = false;
}

void CloudStream2::PluginPrint(bool error, const TCHAR* string)
{
	if (error)
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("%s"), *FString(string));
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("%s"), *FString(string));
	}
}

void CloudStream2::ConnectInputHandler()
{
	if (!m_InputHandler.IsValid())
	{
		TSharedPtr<FZLCloudPluginApplicationWrapper> ZLCloudPluginApplicationWrapper = MakeShareable(new FZLCloudPluginApplicationWrapper(FSlateApplication::Get().GetPlatformApplication()));
		TSharedPtr<FGenericApplicationMessageHandler> BaseHandler = FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler();
		m_InputHandler = MakeShared<FZLCloudPluginInputHandler>(ZLCloudPluginApplicationWrapper, BaseHandler);

#if WITH_EDITOR
		if (GIsEditor)
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
			TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
			if (!ActiveLevelViewport.IsValid())
			{
				return;
			}

			FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
			FSceneViewport* SceneViewport = static_cast<FSceneViewport*>(LevelViewportClient.Viewport);
			m_InputHandler->SetTargetViewport(SceneViewport->GetViewportWidget());
			m_InputHandler->SetTargetWindow(SceneViewport->FindWindow());
		}
		else
#endif
		{
			// default to the scene viewport if we have a game engine
			if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
			{
				TSharedPtr<FSceneViewport> TargetViewport = GameEngine->SceneViewport;
				if (TargetViewport.IsValid())
				{
					m_InputHandler->SetTargetViewport(TargetViewport->GetViewportWidget());
					m_InputHandler->SetTargetWindow(TargetViewport->FindWindow());
				}
				else
				{
					UE_LOG(LogZLCloudPlugin, Error, TEXT("Cannot set target viewport/window - target viewport is not valid."));
				}
			}
		}
	}
}

void CloudStream2::DisconnectInputHandler()
{
	if (m_InputHandler.IsValid())
	{
		m_InputHandler->SetTargetViewport(nullptr);
		m_InputHandler->SetTargetWindow(nullptr);
		m_InputHandler.Reset();
		m_inputDeactivate = false;
	}
}

void CloudStream2::InitCloudStreamSettings(const char* settingsJson)
{
	if (!IsDLLValid())
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("CloudStream2 DLL not initialised or invalid, cannot set settings"));
		return;
	}
	
	if (!settingsJson)
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("CloudStream2 settings JSON is null"));
		return;
	}

	//Input handler
	ConnectInputHandler();

	m_FrameRequirements.Clear();

	bool runningLocally = false;
#if WITH_EDITOR
	runningLocally = true;
#endif

	//Set all settings to the cloud plugin
	CloudStream2DLL::SetSettingsJSON(settingsJson, runningLocally);

	if (GDynamicRHI)
	{
		FString RHIName = GDynamicRHI->GetName();

		if (RHIName == TEXT("D3D11"))
		{
			ID3D11Device* DeviceD3D11 = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
			CloudStream2DLL::UnrealPluginLoad((void*)DeviceD3D11, false);
			UE_LOG(LogZLCloudPlugin, Display, TEXT("CloudStream2 - Init with D3D11 Device"));
		}
		else if (RHIName == TEXT("D3D12"))
		{
			ID3D12Device* DeviceD3D12 = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
			CloudStream2DLL::UnrealPluginLoad((void*)DeviceD3D12, true);
			UE_LOG(LogZLCloudPlugin, Display, TEXT("CloudStream2 - Init with D3D12 Device"));
		}
		else
		{
			UE_LOG(LogZLCloudPlugin, Error, TEXT("CloudStream2 - Unsupported rendering device"));
			return;
		}
	}		

	UpdateFPS();

	UE_LOG(LogZLCloudPlugin, Display, TEXT("Updating filtered keycodes"));
	UpdateFilteredKeys();

	m_cloudSettingsSet = true;
}

void CloudStream2::UpdateFPS()
{
	if (!IsDLLValid())
	{
		return; // Silently return if DLL not initialized
	}

	const UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();
	if (!Settings)
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("CloudStream2 settings not available, cannot update FPS"));
		return;
	}

	int fps = Settings->FramesPerSecond;

	if (m_TargetFPS != fps)
	{
		//Set FPS
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
		if (CVar)
		{
			CVar->Set(fps);
		}
		
		CloudStream2DLL::SetFPS(fps);
		m_TargetFPS = fps;
		UE_LOG(LogZLCloudPlugin, Display, TEXT("CloudStream2 : Target FPS %d"), m_TargetFPS);
	}


}

void CloudStream2::UpdateFilteredKeys()
{
	UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();
	
	if (Settings)
	{
		FString CommaList = Settings->filteredKeyList;

		UE_LOG(LogZLCloudPlugin, Display, TEXT("Filtered keys list %s"), *CommaList);

		Settings->UpdateFilteredKeys();
	}
}

void CloudStream2::SetReadyToStream() 
{ 
	m_pluginReady = true; 

	// In editor/PIE this may be called before the first Update() tick where we lazily create audio capture.
	if (!m_audioSubmixCapturer)
	{
		m_audioSubmixCapturer = MakeShared<ZLAudioSubmixCapturer>();
	}

	if (m_audioSubmixCapturer)
	{
		m_audioSubmixCapturer->m_pluginReady = true;
	}
}

bool CloudStream2::PluginStreamConnected()
{
	if (IsReady() && IsDLLValid())
	{
		if (CloudStream2DLL::GetNumConnections() > 0)
		{
			return true;
		}
	}
	return false;
}

void CloudStream2::SetAppInfo()
{
	if (!IsDLLValid())
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("CloudStream2 DLL not valid, cannot set app info"));
		return;
	}

	//Set info to plugin that can be shared to the browser (don't add any : )
	FString appInfo = "";

	int cs2_major = 0;
	int cs2_minor = 0;
	int cs2_rev = 0;
	int cs2_build = 0;
	CloudStream2DLL::GetPluginVersion(cs2_major, cs2_minor, cs2_rev, cs2_build);

	m_isFeatureNodesSyncCapable = cs2_major > 2 || cs2_minor > 0 || cs2_rev > 3;

	if (!m_isFeatureNodesSyncCapable)
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("CloudStream2 plug-in version 2.0.4 or above is required to support feature nodes frame synchronisation."));
	}

	FString pluginVersion = "";
	IPluginManager& PluginManager = IPluginManager::Get();
	TSharedPtr<IPlugin> ZlCloudStreamPlugin = PluginManager.FindPlugin("ZLCloudPlugin");
	if (ZlCloudStreamPlugin)
	{
		const FPluginDescriptor& Descriptor = ZlCloudStreamPlugin->GetDescriptor();
		pluginVersion = *Descriptor.VersionName;
	}

	FString unrealVersion = ENGINE_VERSION_STRING;
	FString cloudStreamVersion = FString::Printf(TEXT("%d.%d.%d.%d"), cs2_major, cs2_minor, cs2_rev, cs2_build);

	appInfo = "\r\nZLCloudStream Plugin Version - " + pluginVersion + "\r\nCloudStream2.dll - " + cloudStreamVersion;
	appInfo += "\r\nUnreal Version - " + unrealVersion + "\r\nApp Name - " + FApp::GetName() + "\r\nApp Version - " + FApp::GetBuildVersion();

	auto ansiString = StringCast<ANSICHAR>(*appInfo);
	const char* charMessage = ansiString.Get();
	CloudStream2DLL::SetAppVersionInfo(charMessage);
	UE_LOG(LogZLCloudPlugin, Display, TEXT("Set App Info: %s"), *appInfo);
}

void CloudStream2::InitCloudStreamCallbacks()
{
	if (!IsDLLValid())
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("CloudStream2 DLL not initialised or invalid, cannot initialize callbacks"));
		return;
	}

	//ZL Set up cloud plugin callbacks
	CloudStream2DLL::Initialize(OnFrameRequirementsChanged, OnMousePositionChanged, OnClientsDisconnected, OnRichDataStreamConfig, OnFrameMetadataConfig, OnForceImageChanging, OnPluginMessage, OnSendFrames);
}

void CloudStream2::Update(UWorld* World)
{
	if (IsReady())
	{
		UpdateFPS();

		ConnectInputHandler();
		if(m_inputDeactivate)
		{
			DisconnectInputHandler();
		}

		if (m_NeedsResolutionChange)
		{
			if (m_BackToInitialCameraConfig)
			{
				UE_LOG(LogZLCloudPlugin, Display, TEXT("CloudStream2 : Clients disconnected,  changing to startup camera quality settings"));
				//GEngine->GameUserSettings()->SetScreenResolution(FIntPoint(m_defaultStreamWidth, m_defaultStreamHeight));

				if (World == nullptr)
				{
#if WITH_EDITOR
					//ZLTODO...
					
					////in editor just keep aspect
					//float aspectRatio = m_defaultStreamWidth / m_defaultStreamHeight;
					//TSharedPtr<SWindow> ParentWindow = IMainFrameModule::Get().GetParentWindow();
					//int32 x, y, width, height;
					//ParentWindow->GetNativeWindow()->GetFullScreenInfo(x, y, width, height);
					//height = width * aspectRatio;

					//ParentWindow->Resize(FVector2D(width, height));
					//FSlateApplication::Get().OnSizeChanged(ParentWindow->GetNativeWindow().ToSharedRef(), width, height);
					
#endif
				}
				else
				{
					// Flush rendering commands before changing resolution to ensure GPU has finished
					// all operations with the current backbuffer.
					FlushRenderingCommands();
					
					//Send command to update resolution
					FString ChangeResCommand = FString::Printf(TEXT("r.SetRes %dx%d"), m_defaultStreamWidth, m_defaultStreamHeight);
					GEngine->Exec(World, *ChangeResCommand);
				}

				m_BackToInitialCameraConfig = false;
			}
			else
			{
				UE_LOG(LogZLCloudPlugin, Display, TEXT("CloudStream2 : Camera Resolution change: %dx%d"), m_FrameRequirements.Width, m_FrameRequirements.Height);

				if (World == nullptr)
				{
#if WITH_EDITOR
					//ZLTODO...
					/*
					//in editor just keep aspect
					float aspectRatio = m_FrameRequirements.Width / m_FrameRequirements.Height;
					TSharedPtr<SWindow> ParentWindow = IMainFrameModule::Get().GetParentWindow();
					int32 x, y, width, height;
					ParentWindow->GetNativeWindow()->GetFullScreenInfo(x, y, width, height);
					height = width * aspectRatio;

					ParentWindow->Resize(FVector2D(width, height));
					FSlateApplication::Get().OnSizeChanged(ParentWindow->GetNativeWindow().ToSharedRef(), width, height);
					*/
#endif
				}
				else
				{
					// Flush rendering commands before changing resolution to ensure GPU has finished
					// all operations with the current backbuffer. This prevents GPU crashes when
					// resolution change triggers new PSO compilation.
					FlushRenderingCommands();
					
					//Send command to update resolution
					FString ChangeResCommand = FString::Printf(TEXT("r.SetRes %dx%d"), m_FrameRequirements.Width, m_FrameRequirements.Height);
					GEngine->Exec(World, *ChangeResCommand);
				}
			}

			m_NeedsResolutionChange = false;
			// Skip a few frames after resolution change to allow PSO compilation to complete
			// and prevent GPU resource conflicts during the transition
			m_FramesToSkipAfterResolutionChange = 3;
		}

		if (IsDLLValid())
		{
			if (!m_audioSubmixCapturer)
			{
				m_audioSubmixCapturer = MakeShared<ZLAudioSubmixCapturer>();
				m_audioSubmixCapturer->m_pluginReady = m_pluginReady;
			}
			else
			{
				m_audioSubmixCapturer->m_pluginReady = m_pluginReady;
			}

			if (CloudStream2DLL::ShouldSendAudio())
			{
				if (!m_audioInitialised)
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Initialising audio."));

					m_audioInitialised = m_audioSubmixCapturer->Initialise();
					if (!m_audioInitialised)
						UE_LOG(LogZLCloudPlugin, Error, TEXT("The audio submix capturer failed to initialise."));
				}
			}
			else
			{
				if (m_audioInitialised)
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Uninitialising audio."));

					m_audioInitialised = !m_audioSubmixCapturer->Uninitialise();
					if (m_audioInitialised)
						UE_LOG(LogZLCloudPlugin, Error, TEXT("The audio submix capturer failed to uninitialise."));
				}
			}
		}
	}
}

void CloudStream2::CheckInterruptions()
{
	check(IsInRenderingThread());
	
	InterruptionReason CurrentInterruption = InterruptionReason::NO_INTERRUPTION;
	CloudStream2DLL::EncoderRequirements FrameReqs = {};
	bool bHasInterruption = false;
	
	{
		FScopeLock ScopeLock(&m_InterruptionMutex);
		if (m_Interruption != InterruptionReason::NO_INTERRUPTION)
		{
			CurrentInterruption = m_Interruption;
			FrameReqs = m_FrameRequirements;
			bHasInterruption = true;
			m_Interruption = InterruptionReason::NO_INTERRUPTION;
		}
	}
	
	if (bHasInterruption)
	{
		if (CurrentInterruption == InterruptionReason::FRAME_REQUIREMENTS)
		{
			m_IsChangingTexture = true;
			
			if (m_IntermediateTexture)
			{
				FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
				
				if (m_TextureReleaseFence)
				{
					const int32 MaxWaitIterations = 1000;
					int32 WaitIterations = 0;
					while (!m_TextureReleaseFence->Poll() && WaitIterations < MaxWaitIterations)
					{
						FPlatformProcess::Sleep(0.001f);
						++WaitIterations;
					}
					if (WaitIterations >= MaxWaitIterations)
					{
						UE_LOG(LogZLCloudPlugin, Warning, TEXT("CloudStream2 : GPU fence wait timeout"));
					}
				}
				
				RHICmdList.ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
				
				m_IntermediateTexture.SafeRelease();
				m_TextureReleaseFence = nullptr;
			}
			
			if (FrameReqs.Width > 0 && FrameReqs.Height > 0)
			{
				m_IntermediateTexture = ZLCloudPluginUtils::CreateTexture(FrameReqs.Width, FrameReqs.Height);
				
				if (m_IntermediateTexture && m_IntermediateTexture->GetNativeResource() && IsDLLValid())
				{
					CloudStream2DLL::SetTexture((ID3D12Resource*)m_IntermediateTexture->GetNativeResource());
					UE_LOG(LogZLCloudPlugin, Display, TEXT("CloudStream2 : IntermediateTex change: %dx%d"), FrameReqs.Width, FrameReqs.Height);
				}
				else
				{
					UE_LOG(LogZLCloudPlugin, Error, TEXT("CloudStream2 : Failed to create intermediate texture %dx%d"), FrameReqs.Width, FrameReqs.Height);
				}
			}
			else
			{
				UE_LOG(LogZLCloudPlugin, Warning, TEXT("CloudStream2 : Invalid frame requirements for texture creation: %dx%d"), FrameReqs.Width, FrameReqs.Height);
			}
			
			m_IsChangingTexture = false;
		}
		else if (CurrentInterruption == InterruptionReason::CLIENTS_DISCONNECTED)
		{
			m_SentFramesCount = 0;

			m_IsChangingTexture = true;
			
			if (m_TextureReleaseFence)
			{
				FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
				
				const int32 MaxWaitIterations = 1000;
				int32 WaitIterations = 0;
				while (!m_TextureReleaseFence->Poll() && WaitIterations < MaxWaitIterations)
				{
					FPlatformProcess::Sleep(0.001f);
					++WaitIterations;
				}
				if (WaitIterations >= MaxWaitIterations)
				{
					UE_LOG(LogZLCloudPlugin, Warning, TEXT("CloudStream2 : GPU fence wait timeout during cleanup"));
				}
				
				RHICmdList.ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
				m_TextureReleaseFence = nullptr;
			}

			m_NeedsResolutionChange = true;
			m_BackToInitialCameraConfig = true;

			m_FrameRequirements.Clear();
			m_inputDeactivate = true;
			
			m_IsChangingTexture = false;
		}
	}
}

/*
void CloudStream2::UpdateMJPEGQuality()
{
	int JPEGQualityMin = 0;
	int JPEGQualityFastMove = 0;
	int JPEGQualityMax = 0;
	CloudStream2DLL.GetMjpegQualityLevels(ref JPEGQualityMin, ref JPEGQualityFastMove, ref JPEGQualityMax);


	int minQuality = (int)Mathf.Lerp((float)JPEGQualityMin, (float)JPEGQualityFastMove, Mathf.InverseLerp(0.05f, 0.25f, ImageChangedManager.Instance.CameraDistanceThisFrame));
	int mjpegQuality = ImageChangedManager.Instance.IsImageChanged(ImageChangedManager.ImageChangedTypes.Camera) ? minQuality : JPEGQualityMax;

	CloudStream2DLL.SetMjpegQuality(mjpegQuality);
}
*/

bool CloudStream2::GetFeatureNodesSyncCapability()
{
	return m_isFeatureNodesSyncCapable;
}

#if UNREAL_5_5_OR_NEWER
void CloudStream2::OnFrame(const FTextureRHIRef& BackBuffer)
#else
void CloudStream2::OnFrame(const FTexture2DRHIRef& BackBuffer)
#endif
{
	//check(IsInRenderingThread());
	if (IsReady())
	{
		CheckInterruptions();

		// Skip frames during resolution transition to prevent GPU resource conflicts
		if (m_FramesToSkipAfterResolutionChange > 0)
		{
			--m_FramesToSkipAfterResolutionChange;
		}
		// Additional validation: ensure texture is valid, not being changed, and resolution change is not in progress
		else if (m_IntermediateTexture && m_IntermediateTexture->GetNativeResource() && !m_NeedsResolutionChange && !m_IsChangingTexture && IsDLLValid())
		{
			// Validate back buffer is valid
			if (!BackBuffer || !BackBuffer->GetNativeResource())
			{
				UE_LOG(LogZLCloudPlugin, Warning, TEXT("CloudStream2 : Invalid back buffer, skipping frame"));
				return;
			}
			
			FGPUFenceRHIRef CopyFence = GDynamicRHI->RHICreateGPUFence(*FString::Printf(TEXT("CloudStream2CopyTexture")));
			
			if (CopyFence)
			{
#if UNREAL_5_1_OR_NEWER
				FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
				ZLCloudPluginUtils::CopyTexture(RHICmdList, BackBuffer, m_IntermediateTexture, CopyFence);
#else
				ZLCloudPluginUtils::CopyTexture(BackBuffer, m_IntermediateTexture, CopyFence);
#endif

				// Store the fence so we can wait on it before releasing the texture
				m_TextureReleaseFence = CopyFence;

				// Validate graphics queue before using it
				void* GraphicsQueue = GDynamicRHI->RHIGetNativeGraphicsQueue();
				if (GraphicsQueue)
				{
					CloudStream2DLL::OnFrameUE(0, GraphicsQueue, (uint32_t)FeatureNodesSyncGPUFence::GetLatestFrameID());
					++m_SentFramesCount;
				}
				else
				{
					UE_LOG(LogZLCloudPlugin, Warning, TEXT("CloudStream2 : Invalid graphics queue, skipping frame"));
				}
			}
			else
			{
				UE_LOG(LogZLCloudPlugin, Warning, TEXT("CloudStream2 : Failed to create GPU fence, skipping frame"));
			}
		}

		/*
		if (m_IntermediateTexture && !m_NeedsResolutionChange)
		{
			//ZLCloudStreamFrameMetadata.BlitFrameMetadata(currentFrame, m_FrameRequirements.Width, m_FrameRequirements.Height, m_StreamTimer.ElapsedMilliseconds);

			//CloudBlitMat.SetFloat(ShaderProps.s_ID__fadeAmount, m_zlPostPost.m_fadeAmount);
			//CloudBlitMat.SetColor(ShaderProps.s_ID__fadeColour, m_zlPostPost.m_fadeColour);
			//BlitHelper.Blit(currentFrame, m_IntermediateTexture, CloudBlitMat, 0, new Rect(0.0f, 0.0f, 1.0f, 1.0f), true);

			CloudStream2DLL.SetAverageFrameTime(RenderingTimer.instance.GPUProfiler.GetFrameAverageTime());

			
			UpdateMJPEGQuality();

			if (m_FrameRequirements.UseDynamicResolution)
			{
				if (ImageChangedManager.Instance.IsFullImageChanged())
					m_ForcedFrames = Math.Max(m_ForcedFrames, 4);

				bool cameraMoved = m_ForcedFrames > 0;
				--m_ForcedFrames;

				if (cameraMoved)
				{
					GL.IssuePluginEvent(CloudStream2DLL.OnFrame(), kDynamicFrame);
					++SentFramesCount;
				}
				else if (m_LastCameraMoved)
				{
					GL.IssuePluginEvent(CloudStream2DLL.OnFrame(), kStaticFrame);
					++SentFramesCount;
				}

				if (cameraMoved != m_LastCameraMoved)
					m_LastCameraMoved = cameraMoved;
			}
			else if (m_FrameRequirements.UseStreamPausing)
			{
				if (ImageChangedManager.Instance.IsFullImageChanged())
					m_ForcedFrames = Math.Max(m_ForcedFrames, 4);

				bool cameraMoved = m_ForcedFrames > 0;
				--m_ForcedFrames;

				if (cameraMoved)
				{
					GL.IssuePluginEvent(CloudStream2DLL.OnFrame(), kStaticFrame);
					++SentFramesCount;
				}

				if (cameraMoved != m_LastCameraMoved)
					m_LastCameraMoved = cameraMoved;
			}
			else
			{
				GL.IssuePluginEvent(CloudStream2DLL.OnFrame(), kStaticFrame);
				++SentFramesCount;
			}

			BlitHelper.Blit(m_IntermediateTexture, null, true);
		}
		else
		{
			BlitHelper.Blit(currentFrame, null);
		}
		*/

	}
}

void CloudStream2::OnFrameRequirementsChanged(CloudStream2DLL::EncoderRequirements requirements)
{
	FScopeLock ScopeLock(&m_InterruptionMutex);
	
	m_LastCameraMoved = true;

	m_StreamTimer.Reset();
	m_StreamTimer.Start();

	if (m_FrameRequirements.FrameType != requirements.FrameType)
		m_FrameRequirements.FrameType = requirements.FrameType;

	if (requirements.Width != m_FrameRequirements.Width || requirements.Height != m_FrameRequirements.Height)
	{
		m_Interruption = InterruptionReason::FRAME_REQUIREMENTS;
		m_FrameRequirements.Height = requirements.Height;
		m_FrameRequirements.Width = requirements.Width;
		m_NeedsResolutionChange = true;
		m_BackToInitialCameraConfig = false;
	}

	if (requirements.UseDynamicResolution != m_FrameRequirements.UseDynamicResolution)
		m_FrameRequirements.UseDynamicResolution = requirements.UseDynamicResolution;

	if (requirements.UseStreamPausing != m_FrameRequirements.UseStreamPausing)
		m_FrameRequirements.UseStreamPausing = requirements.UseStreamPausing;

	m_ForcedFrames = FMath::Max<int32>(m_ForcedFrames, 4);
}

int CloudStream2::MouseLatencyValue()
{
	return m_LatencyValue;
}

FVector3f& CloudStream2::GetMousePosition()
{
	return m_MousePosition;
}

void CloudStream2::OnMousePositionChanged(float x, float y, float z, int latencyValue)
{
	//UE_LOG(LogZLCloudPlugin, Display, TEXT("Mouse: %f %f %f %d"), x, y, z, latencyValue);

	m_MousePosition.X = x;
	m_MousePosition.Y = y;
	m_MousePosition.Z = z;
	m_LatencyValue = latencyValue;
	m_GotMouseData = true;
}

void CloudStream2::OnClientsDisconnected()
{
	FScopeLock ScopeLock(&m_InterruptionMutex);
	m_Interruption = InterruptionReason::CLIENTS_DISCONNECTED;
	m_StreamTimer.Stop();
}

void CloudStream2::SendCommand(const char* id, const char* data)
{
	if (IsReady() && IsDLLValid())
	{
		if (!id)
		{
			UE_LOG(LogZLCloudPlugin, Warning, TEXT("CloudStream2 : SendCommand called with null id"));
			return;
		}
		CloudStream2DLL::SendCommand(id, data);
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Verbose, TEXT("CloudStream2 : SendCommand called but plugin not ready or DLL invalid"));
	}
}

void CloudStream2::OnPluginMessage(const char* data)
{
	if (m_InputHandler.IsValid())
	{
		m_InputHandler->OnMessage(data);
	}
}

void CloudStream2::OnRichDataStreamConfig(int richDataSourceID, bool state)
{
	//ZLTODO....
	/*
	if (!Enum.IsDefined(typeof(RichDataStream.DataSourceID), richDataSourceID))
	{
		Debug.LogWarning(() = > "SetRichDataStreamConfig: Invalid data source ID: " + richDataSourceID);
		return;
	}

	Debug.Log(() = > "SetRichDataStreamConfig: " + richDataSourceID + "=" + state);

	try
	{
		if (state)
			RichDataStream.Instance.Subscribe((RichDataStream.DataSourceID)richDataSourceID, OnRichDataStream);
		else
			RichDataStream.Instance.Unsubscribe((RichDataStream.DataSourceID)richDataSourceID, OnRichDataStream);
	}
	catch (Exception e)
	{
		Debug.LogError(() = > "SetRichDataStreamConfig: " + e);
	}
	*/
}

void CloudStream2::OnForceImageChanging(float duration)
{
	//ImageChangedManager.Instance.OnCloudPluginForce(duration);
}

void CloudStream2::OnFrameMetadataConfig(int version, int type, int cellSize, int colsPerRow, int orientation)
{
	//ZLTODO....
	/*
	Debug.Log(() = > "SetFrameMetadataConfig: " + version + ", " + type + ", " + cellSize + ", " + colsPerRow + ", " + orientation, Debug.DebugPriority.HIGH);

	try
	{
		if (type != -1)
			ZLCloudStreamFrameMetadata.SetConfig(version, type, cellSize, colsPerRow, orientation);
		else
			ZLCloudStreamFrameMetadata.Reset();
	}
	catch (Exception e)
	{
		Debug.LogError(() = > "SetFrameMetadataConfig: " + e);
	}
	*/
}

void CloudStream2::OnSendFrames(int numFrames)
{

}