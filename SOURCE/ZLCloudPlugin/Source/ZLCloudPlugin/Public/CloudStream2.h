// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "ZLCloudPluginVersion.h"
#include "CloudStream2dll.h"
#include "CoreFwd.h"
#include "ZLStopwatch.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "ZLCloudPluginApplicationWrapper.h"
#include "ZLCloudPluginInputHandler.h"
#include "ZLAudioSubmixCapturer.h"
#include "InputDevice.h"

namespace ZLCloudPlugin
{
	enum InterruptionReason
	{
		NO_INTERRUPTION,
		FRAME_REQUIREMENTS,
		CLIENTS_DISCONNECTED
	};

	class CloudStream2
	{
	public:
		static void InitPlugin();
		static void FreePlugin();

		static void InitCloudStreamSettings(const char* settingsJson);
		static void InitCloudStreamCallbacks();

		static void SetAppInfo();

		static bool IsReady() { return m_pluginReady; }
		static bool IsPluginInitialised() { return m_pluginInitialised; }
		static bool SettingsSet() { return m_cloudSettingsSet; }
		static void SetReadyToStream();
		static bool PluginStreamConnected();

		static void Update(UWorld* World);
#if UNREAL_5_5_OR_NEWER
		static void OnFrame(const FTextureRHIRef& BackBuffer);
#else
		static void OnFrame(const FTexture2DRHIRef& BackBuffer);
#endif
		static void SendCommand(const char* id, const char* data);

		static void UpdateFPS();
		static void UpdateFilteredKeys();

		static TSharedPtr<IZLCloudPluginInputHandler> GetInputHandler() { return m_InputHandler; }

		static void OnFrameRequirementsChanged(CloudStream2DLL::EncoderRequirements requirements);
		static void OnForceImageChanging(float duration);
		static void OnMousePositionChanged(float x, float y, float z, int latencyValue);
		static void OnClientsDisconnected();
		static void OnRichDataStreamConfig(int richDataSourceID, bool state);
		static void OnFrameMetadataConfig(int version, int type, int cellSize, int colsPerRow, int orientation);
		static void OnPluginMessage(const char* data);
		static void OnSendFrames(int numFrames);

		static FVector3f& GetMousePosition();
		static int MouseLatencyValue();

		static void SetInputHandling(bool enable) { m_inputIgnore = !enable; }
		static void SetMessageHandling(bool enable) { m_messageHandlerIgnore = !enable; }
		static bool IsInputHandling() { return !m_inputIgnore; }
		static bool IsMessageHandling() { return !m_messageHandlerIgnore; }
		
	private:
		static void CheckInterruptions();
		static void ConnectInputHandler();
		static void DisconnectInputHandler();
		
		static void PluginPrint(bool error, const TCHAR* string);

		static bool m_pluginInitialised;
		static bool m_pluginReady;
		static bool m_cloudSettingsSet;
		static bool m_inputDeactivate;

		//To ignore until a PIE or standalone play event is active
		static bool m_messageHandlerIgnore;
		static bool m_inputIgnore;

		static void* m_CloudStream2DLLHandle;
		
		static CloudStream2DLL::EncoderRequirements m_FrameRequirements;
		static InterruptionReason m_Interruption;
		static FCriticalSection m_InterruptionMutex;

#if UNREAL_5_5_OR_NEWER
		static FTextureRHIRef m_IntermediateTexture;
#else
		static FTexture2DRHIRef m_IntermediateTexture;
#endif

		static bool m_NeedsResolutionChange;
		static bool m_BackToInitialCameraConfig;

		static int m_TargetFPS;
		static int m_LatencyValue;
		static int m_ForcedFrames;
		static int m_SentFramesCount;
		static bool m_GotMouseData;
		static FVector3f m_MousePosition;

		static int m_defaultStreamWidth;
		static int m_defaultStreamHeight;

		static bool m_LastCameraMoved;
		static ZLStopwatch m_StreamTimer;

		static TSharedPtr<IZLCloudPluginInputHandler> m_InputHandler;

		static TSharedPtr<ZLAudioSubmixCapturer> m_audioSubmixCapturer;
		static bool m_audioInitialised;
	};
}