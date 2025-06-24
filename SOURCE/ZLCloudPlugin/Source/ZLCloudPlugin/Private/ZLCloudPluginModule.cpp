// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginModule.h"
//ZL #include "Streamer.h"
#include "InputDevice.h"
#include "ZLCloudPluginInputComponent.h"
#include "ZLCloudPluginDelegates.h"
//ZL #include "SignallingServerConnection.h"
//ZL #include "Settings.h"
#include "ZLCloudPluginPrivate.h"
#include "ZLScreenshot.h"
#include "ZLCloudPluginStateManager.h"
//ZL #include "PlayerSession.h"
//ZL #include "AudioSink.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Texture2D.h"
#include "Slate/SceneViewport.h"
#include "Utils.h"
#include "HAL/IConsoleManager.h"


#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_LINUX
	#include "CudaModule.h"
#endif

#if PLATFORM_WINDOWS
	#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
	#include <VersionHelpers.h>
THIRD_PARTY_INCLUDES_END
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "RenderingThread.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RendererInterface.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/GameModeBase.h"
#include "Dom/JsonObject.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Async/Async.h"
#include "Engine/Engine.h"

#include "ZLCloudPluginVideoInputBackBuffer.h"
#include "VideoSourceGroup.h"

#include "ZLCloudPluginApplicationWrapper.h"
#include "ZLCloudPluginInputHandler.h"
#include "InputHandlers.h"

#if WITH_EDITOR
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "Slate/SceneViewport.h"
#include "SLevelViewport.h"
#include "ZLCloudPluginVideoInputViewport.h"
#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Interfaces/IProjectManager.h"
#include "DesktopPlatformModule.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#endif

#if !UE_BUILD_SHIPPING
	#include "DrawDebugHelpers.h"
#endif

DEFINE_LOG_CATEGORY(LogZLCloudPlugin);

IZLCloudPluginModule* ZLCloudPlugin::FZLCloudPluginModule::ZLCloudPluginModule = nullptr;

typedef ZLProtocol::EZLCloudPluginMessageTypes EType;

void ZLCloudPlugin::FZLCloudPluginModule::InitStreamer()
{

#if UNREAL_5_1_OR_NEWER
	//Works in editor by streaming viewport
#else
	if (GIsEditor)
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLCloudStream Editor streaming supported in Unreal 5.1 +"));
		return;
	}
#endif

	// Check to see if we can use the ZLCloud plugin on this platform.
	// If not then we avoid setting up our delegates to prevent access to the
	// plugin.
	if (!IsPlatformCompatible())
	{
		return;
	}

	if (!ensure(GEngine != nullptr))
	{
		return;
	}

	//init server comms
	m_LauncherComms.InitComms();
	ZLScreenshot::Get()->Init(&m_LauncherComms);

#if UNREAL_5_1_OR_NEWER

	if (!m_RunningCloudXRMode)
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			ExternalVideoSourceGroup->SetVideoInput(FZLCloudPluginVideoInputViewport::Create());
		}
		else
#endif
		{
			ExternalVideoSourceGroup->SetVideoInput(FZLCloudPluginVideoInputBackBuffer::Create());
		}

		ExternalVideoSourceGroup->CreateVideoSource([]() { return true; });
		ExternalVideoSourceGroup->Start();
	}
	
#else
	
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddRaw(this, &ZLCloudPlugin::FZLCloudPluginModule::OnBackBufferReady_RenderThread);
	}
	
#endif	

	FGameModeEvents::GameModePostLoginEvent.AddRaw(this, &ZLCloudPlugin::FZLCloudPluginModule::OnGameModePostLogin);
	FGameModeEvents::GameModeLogoutEvent.AddRaw(this, &ZLCloudPlugin::FZLCloudPluginModule::OnGameModeLogout);

	PopulateProtocol();

	FApp::SetUnfocusedVolumeMultiplier(1.0f);

	// Allow ZLCloudStream to broadcast to various delegates bound in the
	// application-specific blueprint.
	UZLCloudPluginDelegates::CreateInstance();

	verify(FModuleManager::Get().LoadModule(FName("ImageWrapper")));

	// Streamer has been created, so module is now "ready" for external use.
	ReadyEvent.Broadcast(*this);
}



void ZLCloudPlugin::FZLCloudPluginModule::CompleteInitStreamer()
{
	if (CloudStream2::SettingsSet())
	{
		CloudStream2::InitCloudStreamCallbacks();
		CloudStream2::SetReadyToStream();
		m_streamerInitComplete = true;
	}
}

void ZLCloudPlugin::FZLCloudPluginModule::OnBeginPIE(bool bIsSimulating)
{
	CloudStream2::InitPlugin();

	if (!m_LauncherComms.IsReadRunning())
	{
		m_LauncherComms.InitComms();
		m_LauncherComms.Update(); //Flush a versionmatch message through at start of PIE if we are restarting comms because we need cloudstreamsettings refresh
	}

	CloudStream2::SetMessageHandling(true);
	CloudStream2::SetInputHandling(true);

	if(!m_streamerInitComplete)
		InitStreamer();

	m_isRunningPIE = true;
	m_ApplicationReady = false;
}

void ZLCloudPlugin::FZLCloudPluginModule::OnBeginStandalonePlay(uint32 processID)
{
	/*FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
	if (!ActiveLevelViewport.IsValid())
	{
		return;
	}

	FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
	FSceneViewport* SceneViewport = static_cast<FSceneViewport*>(LevelViewportClient.Viewport);
	m_InputHandler->SetTargetViewport(SceneViewport->GetViewportWidget());
	m_InputHandler->SetTargetWindow(SceneViewport->FindWindow());*/

	CloudStream2::FreePlugin();
	m_ApplicationReady = false;
}

void ZLCloudPlugin::FZLCloudPluginModule::OnEndPIE(bool bIsSimulating)
{
	m_isRunningPIE = false;
	CloudStream2::SetMessageHandling(false);
	CloudStream2::SetInputHandling(false);
	m_LauncherComms.SendLauncherMessage("NVIFRPLUGIN_NOTREADY");
	m_LauncherComms.Shutdown();

	UZLCloudPluginStateManager* stateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
	if (stateManager != nullptr)
	{
		stateManager->SetStreamConnected(false);
		stateManager->ClearProcessingState();
		stateManager->ResetCurrentAppState("");
		stateManager->DestroyDebugUI();
	}
}

/** IModuleInterface implementation */
void ZLCloudPlugin::FZLCloudPluginModule::StartupModule()
{
	RegisterCustomizations();
	bool noInitCloudPlugin = FParse::Param(FCommandLine::Get(), TEXT("nozlplugin"));

	if (noInitCloudPlugin) //For testing without zl plugin or scripts running
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("ZLCloudStream not loaded, running in NoZLPlugin mode"));
		return;
	}

	// ZLCloudStream does not make sense without an RHI so we don't run in commandlets without one.
	if (IsRunningCommandlet() && !IsAllowCommandletRendering())
	{
		return;
	}

	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	//Check if we are running in CloudXR mode or force skipping plugin setup
	m_RunningCloudXRMode = FParse::Param(FCommandLine::Get(), TEXT("VR"));

	if (m_RunningCloudXRMode)
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("ZLCloudStream Running in CloudXR mode"));
	}

#if WITH_EDITOR
	if(!GIsEditor)
#endif
		CloudStream2::InitPlugin();

#if UNREAL_5_1_OR_NEWER

	ExternalVideoSourceGroup = FVideoSourceGroup::Create();

	const ERHIInterfaceType RHIType = GDynamicRHI ? RHIGetInterfaceType() : ERHIInterfaceType::Hidden;

	// only D3D11/D3D12/Vulkan is supported
	if (RHIType == ERHIInterfaceType::D3D11 || RHIType == ERHIInterfaceType::D3D12)//todo... || RHIType == ERHIInterfaceType::Vulkan)
	{
#if WITH_EDITOR
		//Standalone exit delegate does not exist? Need bStreamInEditor false when running standalone process
		if (GIsEditor)
		{
			FEditorDelegates::BeginPIE.AddRaw(this, &FZLCloudPluginModule::OnBeginPIE);
			FEditorDelegates::BeginStandaloneLocalPlay.AddRaw(this, &FZLCloudPluginModule::OnBeginStandalonePlay);
			FEditorDelegates::EndPIE.AddRaw(this, &FZLCloudPluginModule::OnEndPIE);

			if (CloudStream2::IsPluginInitialised())
			{
				CloudStream2::SetMessageHandling(false);
				CloudStream2::SetInputHandling(false);
			}
		}
		else
		{
			//InitStreamer called in a PIE event or in standalone play
			FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]() {
				InitStreamer();
			});
		}

		// Update editor settings so that editor won't slow down if not in focus
		UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
		Settings->bThrottleCPUWhenNotForeground = false;
		Settings->PostEditChange();
		Settings->SaveConfig();
#else

		//InitStreamer called in a PIE event or in standalone play
		FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]() {
			InitStreamer();
		});
#endif
		IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);

		
	}
	else
	{
#if !WITH_DEV_AUTOMATION_TESTS
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("Only D3D11/D3D12 Dynamic RHI is supported. Detected %s"), GDynamicRHI != nullptr ? GDynamicRHI->GetName() : TEXT("[null]"));
#endif
	}
	
#else

	// only D3D11/D3D12 is supported
	if (GDynamicRHI == nullptr || !(GDynamicRHI->GetName() == FString(TEXT("D3D11")) || GDynamicRHI->GetName() == FString(TEXT("D3D12")) || GDynamicRHI->GetName() == FString(TEXT("Vulkan"))))
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("Only D3D11/D3D12/Vulkan Dynamic RHI is supported. Detected %s"), GDynamicRHI != nullptr ? GDynamicRHI->GetName() : TEXT("[null]"));
		return;
	}
	else if (GDynamicRHI->GetName() == FString(TEXT("D3D11")) || GDynamicRHI->GetName() == FString(TEXT("D3D12")) || GDynamicRHI->GetName() == FString(TEXT("Vulkan")))
	{
		// By calling InitStreamer post engine init we can use ZLCloudStream in standalone editor mode
		FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &ZLCloudPlugin::FZLCloudPluginModule::InitStreamer);
	}
	
#endif



#if WITH_EDITOR
	if (GIsEditor)
	{
		MainButton = MakeShared<FZeroLightMainButton>();
		MainButton->Initialize();

		FString BuildPath;
		bool IsPortalUpload = true;

		FString CommandLine(FCommandLine::Get());

		FString Prefix(TEXT("-omnistreamBuildAndDeploy"));
		int32 Index = CommandLine.Find(Prefix);
		if (Index != INDEX_NONE)
		{
			FString ProjectDir = FPaths::ProjectDir();
			BuildPath = FPaths::Combine(ProjectDir, TEXT("Build"));

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			if (!PlatformFile.DirectoryExists(*BuildPath))
			{
				bool bCreated = PlatformFile.CreateDirectoryTree(*BuildPath);
				if (!bCreated)
				{
					UE_LOG(LogZLCloudPlugin, Error, TEXT("Failed to create Build directory at: %s"), *BuildPath);
					FPlatformMisc::RequestExitWithStatus(true, 1);
				}
				else
				{
					UE_LOG(LogZLCloudPlugin, Warning, TEXT("Created Build directory at: %s"), *BuildPath);
				}
			}

			UE_LOG(LogZLCloudPlugin, Warning, TEXT("OmniStream Build & Upload CI - Build path: %s"), *BuildPath);
		
			FZeroLightMainButton::s_isCIBuild = true;
			if (BuildPath.StartsWith("=")) //Catch some cmd line FParse bug here
			{
				BuildPath.RemoveAt(0);
			}
			s_CIMode = true;
			UE_LOG(LogZLCloudPlugin, Warning, TEXT("OmniStream Build & Upload CI triggered - Output to %s"), *BuildPath);
			
			if(!MainButton->CI_HasValidBuildSettingsAndAuthorised(IsPortalUpload))
			{
				UE_LOG(LogZLCloudPlugin, Error, TEXT("OmniStream Build & Upload CI - No valid AssetRef/Portal Asset Line ID was found in project settings, or machine did not have a valid Portal auth token"));
				FPlatformMisc::RequestExitWithStatus(true, 1);
			}
			else
			{
				if (IsPortalUpload)
				{
					UE_LOG(LogZLCloudPlugin, Warning, TEXT("OmniStream Build & Upload CI - Deploying to Portal Asset-Line %s"), *MainButton->GetPortalAssetLineID());
				}
				else
				{
					UE_LOG(LogZLCloudPlugin, Warning, TEXT("OmniStream Build & Upload CI - Running for App %s"), *MainButton->GetDeployName());
				}
			}

			FReply trigger = MainButton->CI_TriggerBuildAndDeploy(BuildPath, IsPortalUpload);

			if (!trigger.IsEventHandled())
			{
				UE_LOG(LogPortalCLI, Error, TEXT("Command Line Build & Upload was not handled - check project settings and auth token are valid. Aborting..."));
				FPlatformMisc::RequestExitWithStatus(true, 1);
			}
		}
	}
#endif
}

void ZLCloudPlugin::FZLCloudPluginModule::ShutdownModule()
{
	UnregisterCustomizations();
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
		FSlateApplication::Get().GetRenderer()->OnPreResizeWindowBackBuffer().RemoveAll(this);
	}

#if UNREAL_5_1_OR_NEWER
	if (ExternalVideoSourceGroup)
	{
		ExternalVideoSourceGroup->Stop();
	}
#endif

	m_streamerInitComplete = false;

	CloudStream2::FreePlugin();	

	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
}

void ZLCloudPlugin::FZLCloudPluginModule::RegisterCustomizations()
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyEditorModule.RegisterCustomClassLayout(
			"ZLCloudPluginSettings",
			FOnGetDetailCustomizationInstance::CreateStatic(&FZLCloudPluginSettingsCustomization::MakeInstance)
		);

		PropertyEditorModule.NotifyCustomizationModuleChanged();
	}
#endif
}

void ZLCloudPlugin::FZLCloudPluginModule::UnregisterCustomizations()
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyEditorModule.UnregisterCustomClassLayout("ZLCloudPluginSettings");

		PropertyEditorModule.NotifyCustomizationModuleChanged();
	}
#endif
}

IZLCloudPluginModule* ZLCloudPlugin::FZLCloudPluginModule::GetModule()
{
	if (ZLCloudPluginModule)
	{
		return ZLCloudPluginModule;
	}
	IZLCloudPluginModule* Module = FModuleManager::Get().LoadModulePtr<IZLCloudPluginModule>("ZLCloudPlugin");
	if (Module)
	{
		ZLCloudPluginModule = Module;
	}
	return ZLCloudPluginModule;
}

bool ZLCloudPlugin::FZLCloudPluginModule::IsPlatformCompatible() const
{
	bool bCompatible = true;

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	bool bWin8OrHigher = IsWindows8OrGreater();
	if (!bWin8OrHigher)
	{
		FText TitleText = FText::FromString(TEXT("ZLCloudPlugin"));
		FString ErrorString(TEXT("Failed to initialize ZLCloud plugin because minimum requirement is Windows 8"));
		FText ErrorText = FText::FromString(ErrorString);
#if UNREAL_5_3_OR_NEWER
		FMessageDialog::Open(EAppMsgType::Ok, EAppReturnType::Ok, ErrorText, FText::FromString(TEXT("ZLCloudPlugin")));
#else
		FMessageDialog::Open(EAppMsgType::Ok, ErrorText, &TitleText);
#endif
		UE_LOG(LogZLCloudPlugin, Error, TEXT("%s"), *ErrorString);
		bCompatible = false;
	}
#endif

	if (!IsRHIDeviceNVIDIA())
	{
		FText TitleText = FText::FromString(TEXT("ZLCloudPlugin"));
		FString ErrorString = TEXT("No compatible GPU found");
		FText ErrorText = FText::FromString(ErrorString);
#if UNREAL_5_3_OR_NEWER
		FMessageDialog::Open(EAppMsgType::Ok, EAppReturnType::Ok, ErrorText, FText::FromString(TEXT("ZLCloudPlugin")));
#else
		FMessageDialog::Open(EAppMsgType::Ok, ErrorText, &TitleText);
#endif
		UE_LOG(LogZLCloudPlugin, Error, TEXT("%s"), *ErrorString);
		bCompatible = false;
	}
	

	return bCompatible;
}

void ZLCloudPlugin::FZLCloudPluginModule::UpdateViewport(FSceneViewport* Viewport)
{
	FRHIViewport* const ViewportRHI = Viewport->GetViewportRHI().GetReference();
}

#if UNREAL_5_5_OR_NEWER
void ZLCloudPlugin::FZLCloudPluginModule::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTextureRHIRef& BackBuffer)
#else
void ZLCloudPlugin::FZLCloudPluginModule::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
#endif
{
	check(IsInRenderingThread());

	CloudStream2::OnFrame(BackBuffer);
}

TSharedPtr<IInputDevice> ZLCloudPlugin::FZLCloudPluginModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	return MakeShared<FInputHandlers>(InMessageHandler);
}

const ZLProtocol::FZLCloudPluginProtocol& ZLCloudPlugin::FZLCloudPluginModule::GetProtocol()
{
	return MessageProtocol;
}

void ZLCloudPlugin::FZLCloudPluginModule::PopulateProtocol()
{
	using namespace ZLProtocol;

	// Old EToStreamerMsg Commands
	/*
	 * Control Messages.
	 */
	 // Simple command with no payload
	 // Note, we only specify the ID when creating these messages to preserve backwards compatability
	 // when adding your own message type, you can simply do MessageProtocol.Direction.Add("XXX");
//	MessageProtocol.ToStreamerProtocol.Add("IFrameRequest", FZLCloudPluginInputMessage(0, 0, {}));
//	MessageProtocol.ToStreamerProtocol.Add("RequestQualityControl", FZLCloudPluginInputMessage(1, 0, {}));
//	MessageProtocol.ToStreamerProtocol.Add("FpsRequest", FZLCloudPluginInputMessage(2, 0, {}));
//	MessageProtocol.ToStreamerProtocol.Add("AverageBitrateRequest", FZLCloudPluginInputMessage(3, 0, {}));
//	MessageProtocol.ToStreamerProtocol.Add("StartStreaming", FZLCloudPluginInputMessage(4, 0, {}));
//	MessageProtocol.ToStreamerProtocol.Add("StopStreaming", FZLCloudPluginInputMessage(5, 0, {}));
//	MessageProtocol.ToStreamerProtocol.Add("LatencyTest", FZLCloudPluginInputMessage(6, 0, {}));
//	MessageProtocol.ToStreamerProtocol.Add("RequestInitialSettings", FZLCloudPluginInputMessage(7, 0, {}));
//	MessageProtocol.ToStreamerProtocol.Add("TestEcho", FZLCloudPluginInputMessage(8, 0, {}));

	/*
	 * Input Messages.
	 */
	 // Generic Input Messages.
//	MessageProtocol.ToStreamerProtocol.Add("UIInteraction", FZLCloudPluginInputMessage(50, 0, {}));
	MessageProtocol.ToStreamerProtocol.Add("Command", FZLCloudPluginInputMessage(51, 0, {}));
	MessageProtocol.ToStreamerProtocol.Add("JsonData", FZLCloudPluginInputMessage(52, 0, {}));

	// Keyboard Input Message.
	// Complex command with payload, therefore we specify the length of the payload (bytes) as well as the structure of the payload
	MessageProtocol.ToStreamerProtocol.Add("KeyDown", FZLCloudPluginInputMessage(60, 2, { EType::Uint8, EType::Uint8 }));
	MessageProtocol.ToStreamerProtocol.Add("KeyUp", FZLCloudPluginInputMessage(61, 1, { EType::Uint8 }));
	MessageProtocol.ToStreamerProtocol.Add("KeyPress", FZLCloudPluginInputMessage(62, 2, { EType::Uint16 }));

	// Mouse Input Messages.
	MessageProtocol.ToStreamerProtocol.Add("MouseEnter", FZLCloudPluginInputMessage(70));
	MessageProtocol.ToStreamerProtocol.Add("MouseLeave", FZLCloudPluginInputMessage(71));
	MessageProtocol.ToStreamerProtocol.Add("MouseDown", FZLCloudPluginInputMessage(72, 5, { EType::Uint8, EType::Uint16, EType::Uint16 }));
	MessageProtocol.ToStreamerProtocol.Add("MouseUp", FZLCloudPluginInputMessage(73, 5, { EType::Uint8, EType::Uint16, EType::Uint16 }));
	MessageProtocol.ToStreamerProtocol.Add("MouseMove", FZLCloudPluginInputMessage(74, 8, { EType::Uint16, EType::Uint16, EType::Uint16, EType::Uint16 }));
	MessageProtocol.ToStreamerProtocol.Add("MouseWheel", FZLCloudPluginInputMessage(75, 6, { EType::Int16, EType::Uint16, EType::Uint16 }));
	MessageProtocol.ToStreamerProtocol.Add("MouseDouble", FZLCloudPluginInputMessage(76, 5, { EType::Uint8, EType::Uint16, EType::Uint16 }));

	// Touch Input Messages.
	MessageProtocol.ToStreamerProtocol.Add("TouchStart", FZLCloudPluginInputMessage(80, 8, { EType::Uint8, EType::Uint16, EType::Uint16, EType::Uint8, EType::Uint8, EType::Uint8 }));
	MessageProtocol.ToStreamerProtocol.Add("TouchEnd", FZLCloudPluginInputMessage(81, 8, { EType::Uint8, EType::Uint16, EType::Uint16, EType::Uint8, EType::Uint8, EType::Uint8 }));
	MessageProtocol.ToStreamerProtocol.Add("TouchMove", FZLCloudPluginInputMessage(82, 8, { EType::Uint8, EType::Uint16, EType::Uint16, EType::Uint8, EType::Uint8, EType::Uint8 }));

	// Gamepad Input Messages.
	MessageProtocol.ToStreamerProtocol.Add("GamepadButtonPressed", FZLCloudPluginInputMessage(90, 3, { EType::Uint8, EType::Uint8, EType::Uint8 }));
	MessageProtocol.ToStreamerProtocol.Add("GamepadButtonReleased", FZLCloudPluginInputMessage(91, 3, { EType::Uint8, EType::Uint8, EType::Uint8 }));
	MessageProtocol.ToStreamerProtocol.Add("GamepadAnalog", FZLCloudPluginInputMessage(92, 3, { EType::Uint8, EType::Uint8, EType::Double }));

	// Old EToPlayerMsg commands
//	MessageProtocol.FromStreamerProtocol.Add("QualityControlOwnership", FZLCloudPluginInputMessage(0));
//	MessageProtocol.FromStreamerProtocol.Add("Response", FZLCloudPluginInputMessage(1));
//	MessageProtocol.FromStreamerProtocol.Add("Command", FZLCloudPluginInputMessage(2));
//	MessageProtocol.FromStreamerProtocol.Add("FreezeFrame", FZLCloudPluginInputMessage(3));
//	MessageProtocol.FromStreamerProtocol.Add("UnfreezeFrame", FZLCloudPluginInputMessage(4));
//	MessageProtocol.FromStreamerProtocol.Add("VideoEncoderAvgQP", FZLCloudPluginInputMessage(5));
//	MessageProtocol.FromStreamerProtocol.Add("LatencyTest", FZLCloudPluginInputMessage(6));
//	MessageProtocol.FromStreamerProtocol.Add("InitialSettings", FZLCloudPluginInputMessage(7));
//	MessageProtocol.FromStreamerProtocol.Add("FileExtension", FZLCloudPluginInputMessage(8));
//	MessageProtocol.FromStreamerProtocol.Add("FileMimeType", FZLCloudPluginInputMessage(9));
//	MessageProtocol.FromStreamerProtocol.Add("FileContents", FZLCloudPluginInputMessage(10));
//	MessageProtocol.FromStreamerProtocol.Add("TestEcho", FZLCloudPluginInputMessage(11));
//	MessageProtocol.FromStreamerProtocol.Add("InputControlOwnership", FZLCloudPluginInputMessage(12));


//ZLTODO...	MessageProtocol.FromStreamerProtocol.Add("Protocol", FZLCloudPluginInputMessage(255));
}

void ZLCloudPlugin::FZLCloudPluginModule::GetOnDemandOverrideDefaults()
{
	static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streaming.FullyLoadUsedTextures"));
	int32 Value = CVar->GetInt();
	m_defaultFullyLoadUsedTextures = Value; //If non-zero, all used texture will be fully streamed in as fast as possible
}

void ZLCloudPlugin::FZLCloudPluginModule::SetOnDemandMode(bool onDemandMode)
{
	m_isOnDemandMode = onDemandMode;

	if (GEngine)
	{
		FString fullyLoadTexCmd = "r.Streaming.FullyLoadUsedTextures ";
		fullyLoadTexCmd.AppendInt((onDemandMode) ? 1 : m_defaultFullyLoadUsedTextures);
		GEngine->Exec(GEngine->GetWorld(), *fullyLoadTexCmd);
	}
}

void ZLCloudPlugin::FZLCloudPluginModule::SetAppReadyToStream_Internal(bool readyToStream)
{
	if (readyToStream)
	{
		m_appReadyToStreamFirstSet = true;
		//#VIEWER-2446 set render fence timeout back to default after reaching a ready to stream state
		static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("g.TimeoutForBlockOnRenderFence"));
		CVar->Set(120000);
	}

	if (m_LauncherComms.IsReadRunning())
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("App signalled ZLCloudStream as Ready To Stream"));
		m_LauncherComms.SendLauncherMessage((readyToStream) ? "OMNISTREAM_APP_READY" : "OMNISTREAM_APP_NOTREADY");
	}
}

IZLCloudPluginModule::FReadyEvent& ZLCloudPlugin::FZLCloudPluginModule::OnReady()
{
	return ReadyEvent;
}

bool ZLCloudPlugin::FZLCloudPluginModule::IsReady()
{
	if (m_RunningCloudXRMode)
	{
		return true;
	}
	else
	{
		return CloudStream2::IsReady();
	}
}

TWeakPtr<SViewport> ZLCloudPlugin::FZLCloudPluginModule::GetTargetViewport()
{
	return TargetViewport;
}

void ZLCloudPlugin::FZLCloudPluginModule::AddInputComponent(UZLCloudPluginInput* InInputComponent)
{
	InputComponents.Add(InInputComponent);
}

void ZLCloudPlugin::FZLCloudPluginModule::RemoveInputComponent(UZLCloudPluginInput* InInputComponent)
{
	InputComponents.Remove(InInputComponent);
}

const TArray<UZLCloudPluginInput*> ZLCloudPlugin::FZLCloudPluginModule::GetInputComponents()
{
	return InputComponents;
}


void ZLCloudPlugin::FZLCloudPluginModule::OnGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
}

void ZLCloudPlugin::FZLCloudPluginModule::OnGameModeLogout(AGameModeBase* GameMode, AController* Exiting)
{
}


void ZLCloudPlugin::FZLCloudPluginModule::SendData(const FString& jsonData)
{
	auto ansiString = StringCast<ANSICHAR>(*jsonData);
	const char* charMessage = ansiString.Get();

	//Needs to match PopulateProtocol values
	CloudStream2::SendCommand(/*"JsonData"*/"52", charMessage);
}

bool ZLCloudPlugin::FZLCloudPluginModule::IsTickableWhenPaused() const
{
	return true;
}

bool ZLCloudPlugin::FZLCloudPluginModule::IsTickableInEditor() const
{
	return true;
}

void ZLCloudPlugin::FZLCloudPluginModule::Tick(float DeltaTime)
{
#if WITH_EDITOR
	if (GIsEditor && s_CIMode) //button menu not open, simulate tick here for CI builds
	{
		if (FZeroLightMainButton::s_triggerBuildTask)
		{
			FZeroLightMainButton::s_triggerBuildTask = false;

			FZeroLightMainButton::CI_StartUATBuildTask();
		}

		if (FZeroLightMainButton::s_triggerBuildUpload)
		{
			FZeroLightMainButton::s_triggerBuildUpload = false;

			UE_LOG(LogZLCloudPlugin, Log, TEXT("CI Build Complete - Starting Upload..."));

			if (FZeroLightMainButton::s_CIMainButtonPtr != nullptr)
			{
				FZeroLightMainButton::s_CIMainButtonPtr->TriggerBuildUpload();
			}
		}
	}

	if (!CloudStream2::IsPluginInitialised())
		return;

	if (GIsEditor && m_isRunningPIE)
	{
		bool streamingFromPIEViewport = false;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE)
			{
				FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
				if (SlatePlayInEditorSession)
				{
					TWeakPtr<SViewport> currViewport = TargetViewport;
					if (SlatePlayInEditorSession->SlatePlayInEditorWindow.IsValid())
					{
						TargetViewport = SlatePlayInEditorSession->SlatePlayInEditorWindowViewport->GetViewportWidget();
						streamingFromPIEViewport = true;
					}
				}
			}
		}

		if (!streamingFromPIEViewport)
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
			TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
			if (ActiveLevelViewport.IsValid())
			{
				FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
				FSceneViewport* SceneViewport = static_cast<FSceneViewport*>(LevelViewportClient.Viewport);
				TargetViewport = SceneViewport->GetViewportWidget();
			}
		}
	}
#endif

	UWorld* World = GEngine->GetCurrentPlayWorld();

	if (!m_ApplicationReady)
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			if (!m_isRunningPIE && !CloudStream2::SettingsSet())
			{
				return;
			}
		}
#endif
		if (!CloudStream2::IsReady())
		{
			CompleteInitStreamer();
		}

		if (m_RunningCloudXRMode || CloudStream2::IsReady())
		{	
			CloudStream2::SetAppInfo();

			const UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();
			if (Settings && Settings->DelayAppReadyToStream && !m_appReadyToStreamFirstSet)
			{
				//Set false to prevent readyToStream until node triggers sending ready
				m_LauncherComms.SendLauncherMessage("OMNISTREAM_APP_NOTREADY");
			}
			else
			{
				//#VIEWER-2446 set render fence timeout back to default after reaching a ready to stream state
				static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("g.TimeoutForBlockOnRenderFence"));
				CVar->Set(120000);
			}

			m_LauncherComms.SendLauncherMessage("NVIFRPLUGIN_READY");

			GetOnDemandOverrideDefaults(); //Get current settings for any engine flags we override for on demand generation
			

			UE_LOG(LogZLCloudPlugin, Display, TEXT("ZLCloudStream Setup Complete"));
			m_ApplicationReady = true;
		}
	}

	//Connect/disconnect messages
	if (UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetStreamConnected() 
		&& CloudStream2::PluginStreamConnected()
		&& m_connectionState != connectedState::CONNECTED
		)
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("Cloud Stream Connected"));
		if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
		{
			Delegates->OnConnectedStream.Broadcast();
		}

		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->SendCurrentStateToWeb(true);

		m_connectionState = connectedState::CONNECTED;
	}
	else if (!UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetStreamConnected()
		//&& !CloudStream2::PluginStreamConnected() // not important to check this
		&& m_connectionState != connectedState::DISCONNECTED
		)
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("Cloud Stream Disconnected"));
		if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
		{
			Delegates->OnDisconnectedStream.Broadcast();
		}
		m_connectionState = connectedState::DISCONNECTED;
	}


	CloudStream2::Update(World);
	if(m_LauncherComms.IsReadRunning())
		m_LauncherComms.Update();
	ZLScreenshot::Get()->Update();

	UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->Update(&m_LauncherComms);
}

TStatId ZLCloudPlugin::FZLCloudPluginModule::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FZLCloudPluginModule, STATGROUP_Tickables);
}


IZLCloudPluginAudioSink* ZLCloudPlugin::FZLCloudPluginModule::GetPeerAudioSink(FZLCloudPluginPlayerId PlayerId)
{
	//ZL
/*
	if (!Streamer.IsValid())
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Cannot get audio sink when streamer does not yet exist."));
		return nullptr;
	}
	*/

	//ZL return Streamer->GetAudioSink(PlayerId);
	return nullptr;
}

IZLCloudPluginAudioSink* ZLCloudPlugin::FZLCloudPluginModule::GetUnlistenedAudioSink()
{
	//ZL
/*
	if (!Streamer.IsValid())
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Cannot get audio sink when streamer does not yet exist."));
		return nullptr;
	}
	*/

	//ZL return Streamer->GetUnlistenedAudioSink();
	return nullptr;
}

IMPLEMENT_MODULE(ZLCloudPlugin::FZLCloudPluginModule, ZLCloudPlugin)
