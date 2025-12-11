// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "CloudStream2dll.h"
#include "CoreFwd.h"
#include "LauncherComms.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMessageCallbacks, Log, All);

class MessageCallbacks
{
	public:	
		static void RegisterCallbacks(LauncherComms* m_LauncherComms);

		//Required
		static void SetServerVersion(MessageWithData* msg);
		static void CloudStreamSettings(MessageWithData* msg);
		static void CloudStreamConnected(MessageWithData* msg);
		static void CaptureScreenshot(MessageWithData* msg);
		static void StartCaptureTrace(MessageWithData* msg);
		static void EndCaptureTrace(MessageWithData* msg);
		static void SetConnectState(MessageWithData* msg);
		static void SetOnDemandProcessingState(MessageWithData* msg);
		static void SetOmnistreamSettings(MessageWithData* msg);

		//Cert effects
		static void GetAllUiDetailsForZlCertifiedEffects(MessageWithData* msg);
		static void SetZlCertifiedEffects(MessageWithData* msg);
		static void DisableZlCertifiedEffects(MessageWithData* msg);
		static void DisableAllZlCertifiedEffects(MessageWithData* msg);

		static void GetCertifiedEffectDevelopmentStates(MessageWithData* msg);
		static void GetAllZlCertifiedEffects(MessageWithData* msg);
		static void GetAllActiveZlCertifiedEffects(MessageWithData* msg);

		//Dummy
		static void UNLOAD_ASSET_BUNDLE(MessageWithData* msg);
		static void GET_ALL_VE_CAPABILITIES(MessageWithData* msg);
		static void ORBIT_PAUSE(MessageWithData* msg);
		static void UPDATE_DXR_PROXY(MessageWithData* msg);
		static void DISABLE_ALL_ZLCERTIFIED_EFFECTS(MessageWithData* msg);
		static void CURRENTCAMERA(MessageWithData* msg);
		static void GPUVALIDATOR_STARTVALIDATION(MessageWithData* msg);
		static void PAUSE_CAMERAS(MessageWithData* msg);
		static void LOAD_STAGE(MessageWithData* msg);
		static void DEFAULTVEHICLE(MessageWithData* msg);
		static void VEHICLEUPDATE(MessageWithData* msg);
		static void GETACTIVEORBITNAME(MessageWithData* msg);
		static void RESUME_CAMERAS(MessageWithData* msg);
		static void GETDEBUGMENU(MessageWithData* msg);
		static void SETCAMERADIRECTLYJSON(MessageWithData* msg);
		static void GET_SCREENSHOT_LAYER_OPTIONS(MessageWithData* msg);

		static void DummyCallback(MessageWithData* msg);

	private:
		static LauncherComms* m_LauncherComms;
};

