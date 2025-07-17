// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginVersion.h"
#include "IZLCloudPluginModule.h"
#include "RHI.h"
#include "Tickable.h"
#include "InputDevice.h"
#include "CloudStream2.h"
#include "LauncherComms.h"
#include "ZeroLightMainButton.h"
#include "ZLCloudPluginProtocol.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"
#include "Interfaces/IProjectBuildMutatorFeature.h"

class AController;
class AGameModeBase;
class APlayerController;
class FSceneViewport;
class UZLCloudPluginInput;
class SWindow;

namespace ZLCloudPlugin
{
	class FVideoInputBackBuffer;
	class FVideoSourceGroup;

	class FForceTempTargetFeature : public IProjectBuildMutatorFeature
	{
	public:
		virtual bool RequiresProjectBuild(const FName& InPlatformInfoName, FText& OutReason) const override
		{
			UE_LOG(LogTemp, Warning, TEXT("Return true for RequiresProjectBuild"));

			OutReason = FText::FromString(TEXT("Forced by ZLCloudPlugin"));
			return true; // Always force for BP only projects
		}
	};

	static TSharedPtr<FForceTempTargetFeature> ForceFeature;

	/*
	* This plugin allows the back buffer to be sent as a compressed video across
	* a network.
	*/
	class FZLCloudPluginModule : public IZLCloudPluginModule, public FTickableGameObject
	{
	public:
		static IZLCloudPluginModule* GetModule();

		virtual const ZLProtocol::FZLCloudPluginProtocol& GetProtocol() override;

	private:
		/** IModuleInterface implementation */
		void StartupModule() override;
		void ShutdownModule() override;

		void RegisterCustomizations();
		void UnregisterCustomizations();

		/** IInputDeviceModule implementation */
		TSharedPtr<class IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
		/** End IInputDeviceModule implementation */

		/** IZLCloudPluginModule implementation */
		IZLCloudPluginModule::FReadyEvent& OnReady() override;
		bool IsReady() override;
		bool IsPIERunning() override { return m_isRunningPIE; };

		//void SendCommand(const FString& Descriptor) override;
		void PopulateProtocol();
		void GetOnDemandOverrideDefaults();
		void SetOnDemandMode(bool onDemandMode) override;

		void SetAppReadyToStream_Internal(bool readyToStream);

		/**
			* Returns a shared pointer to the device which handles ZLCloudStream
			* input.
			* @return The shared pointer to the input device.
			*/
		void AddInputComponent(UZLCloudPluginInput* InInputComponent) override;
		void RemoveInputComponent(UZLCloudPluginInput* InInputComponent) override;
		const TArray<UZLCloudPluginInput*> GetInputComponents() override;

		TWeakPtr<SViewport> GetTargetViewport();

		IZLCloudPluginAudioSink* GetPeerAudioSink(FZLCloudPluginPlayerId PlayerId) override;
		IZLCloudPluginAudioSink* GetUnlistenedAudioSink() override;
		/** End IZLCloudPluginModule implementation */

		// FTickableGameObject
		bool IsTickableWhenPaused() const override;
		bool IsTickableInEditor() const override;
		void Tick(float DeltaTime) override;

		TStatId GetStatId() const override;

		bool IsPlatformCompatible() const;
		void UpdateViewport(FSceneViewport* Viewport);
#if UNREAL_5_5_OR_NEWER
		void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTextureRHIRef& BackBuffer);
#else
		void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);
#endif
		void OnGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
		void OnGameModeLogout(AGameModeBase* GameMode, AController* Exiting);
		void OnBeginPIE(bool bIsSimulating);
		void OnEndPIE(bool bIsSimulating);
		void OnBeginStandalonePlay(uint32 processID);

		void SendData(const FString& jsonData);

		void InitStreamer();
		void CompleteInitStreamer();

#if WITH_EDITOR
		bool CheckForNewerPlugin();
#endif

	private:
		bool m_ApplicationReady = false;
		bool m_streamerInitComplete = false;
		bool m_isRunningPIE = false;
		bool m_appReadyToStreamFirstSet = false; //If this has ever been set dont send the startup NOTREADY message (prevents issue when set right after beginplay event)

		//Query on startup to know what reset state should be
		//Some settings we force on 2DOD/media generation to ensure content isnt affected by timing conditionals
		//i.e texture streaming
		int m_defaultFullyLoadUsedTextures = 0;

		enum connectedState
		{
			CONNECTED,
			DISCONNECTED,
		};

		connectedState m_connectionState = connectedState::DISCONNECTED;

		class LauncherComms m_LauncherComms;

		IZLCloudPluginModule::FReadyEvent ReadyEvent;
		TArray<UZLCloudPluginInput*> InputComponents;
		bool bFrozen = false;
		bool bCaptureNextBackBufferAndStream = false;
		double LastVideoEncoderQPReportTime = 0;
		static IZLCloudPluginModule* ZLCloudPluginModule;
		TWeakPtr<SViewport> 		TargetViewport;

		ZLProtocol::FZLCloudPluginProtocol MessageProtocol;

#if UNREAL_5_1_OR_NEWER
		TSharedPtr<FVideoSourceGroup> ExternalVideoSourceGroup;
#endif

#if WITH_EDITOR
		TSharedPtr<class FZeroLightMainButton> MainButton;		
#endif

	};
} // namespace ZLCloudPlugin
