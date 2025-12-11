// Copyright ZeroLight ltd. All Rights Reserved.
#include "MessageCallbacks.h"
#include "CoreMinimal.h"
#include "CloudStream2.h"
#include "ZLCloudPluginPrivate.h"
#include "Misc/DefaultValueHelper.h"
#include "ZLCloudPluginDelegates.h"
#include "ZLCloudPluginStateManager.h"
#include "ZLScreenshot.h"
#include "ZLCloudPluginModule.h"
#include "ZLJobTrace.h"
#include "ZLSpotLightDataDrivenUIManager.h"
#if WITH_EDITOR
#include "EditorZLCloudPluginSettings.h"
#endif

DEFINE_LOG_CATEGORY(LogMessageCallbacks);

LauncherComms* MessageCallbacks::m_LauncherComms = nullptr;

//Required
/////////////////////////////////////////////////////////////////////////////

void MessageCallbacks::RegisterCallbacks(LauncherComms* launcherComms)
{
	m_LauncherComms = launcherComms;

	//required
	m_LauncherComms->RegisterMessageCallback(TEXT("SERVERVERSION"), &SetServerVersion);
	m_LauncherComms->RegisterMessageCallback(TEXT("CLOUDSTREAMSETTINGS"), &CloudStreamSettings);
	m_LauncherComms->RegisterMessageCallback(TEXT("CLOUD_CONNECTED"), &CloudStreamConnected);
	m_LauncherComms->RegisterMessageCallback(TEXT("CAPTUREIMAGE"), &CaptureScreenshot);
	m_LauncherComms->RegisterMessageCallback(TEXT("START_VE_JOBTRACE"), &StartCaptureTrace);
	m_LauncherComms->RegisterMessageCallback(TEXT("END_VE_JOBTRACE"), &EndCaptureTrace);


	m_LauncherComms->RegisterMessageCallback(TEXT("SETINITIALSTATE"), &SetConnectState);
	m_LauncherComms->RegisterMessageCallback(TEXT("SET2DODMODE"), &SetOnDemandProcessingState);
	m_LauncherComms->RegisterMessageCallback(TEXT("OMNISTREAM_SETTINGS"), &SetOmnistreamSettings);

	//Cert effects
	m_LauncherComms->RegisterMessageCallback(TEXT("GET_ALL_UI_DETAILS_FOR_ZLCERTIFIED_EFFECTS"), &GetAllUiDetailsForZlCertifiedEffects);
	m_LauncherComms->RegisterMessageCallback(TEXT("GET_UI_DETAILS_FOR_ZLCERTIFIED_EFFECTS"), &GetAllUiDetailsForZlCertifiedEffects);
	m_LauncherComms->RegisterMessageCallback(TEXT("SET_ZLCERTIFIED_EFFECTS"), &SetZlCertifiedEffects);
	m_LauncherComms->RegisterMessageCallback(TEXT("DISABLE_ZLCERTIFIED_EFFECTS"), &DisableZlCertifiedEffects);
	m_LauncherComms->RegisterMessageCallback(TEXT("DISABLE_ALL_ZLCERTIFIED_EFFECTS"), &DisableZlCertifiedEffects);
	m_LauncherComms->RegisterMessageCallback(TEXT("GET_ALL_ZLCERTIFIED_EFFECTS"), &GetAllZlCertifiedEffects);
	m_LauncherComms->RegisterMessageCallback(TEXT("GET_ALL_ACTIVE_ZLCERTIFIED_EFFECTS"), &GetAllActiveZlCertifiedEffects);

	m_LauncherComms->RegisterMessageCallback(TEXT("GET_CERTIFIED_EFFECT_DEVELOPMENT_STATES"), &GetCertifiedEffectDevelopmentStates);

	//Dummy callbacks
	m_LauncherComms->RegisterMessageCallback(TEXT("UNLOAD_ASSET_BUNDLE"), &UNLOAD_ASSET_BUNDLE);
	m_LauncherComms->RegisterMessageCallback(TEXT("GET_ALL_VE_CAPABILITIES"), &GET_ALL_VE_CAPABILITIES);
	m_LauncherComms->RegisterMessageCallback(TEXT("ORBIT_PAUSE"), &ORBIT_PAUSE);
	m_LauncherComms->RegisterMessageCallback(TEXT("UPDATE_DXR_PROXY"), &UPDATE_DXR_PROXY);
	m_LauncherComms->RegisterMessageCallback(TEXT("DISABLE_ALL_ZLCERTIFIED_EFFECTS"), &DISABLE_ALL_ZLCERTIFIED_EFFECTS);
	m_LauncherComms->RegisterMessageCallback(TEXT("CURRENTCAMERA"), &CURRENTCAMERA);
	m_LauncherComms->RegisterMessageCallback(TEXT("GPUVALIDATOR_STARTVALIDATION"), &GPUVALIDATOR_STARTVALIDATION);
	m_LauncherComms->RegisterMessageCallback(TEXT("PAUSE_CAMERAS"), &PAUSE_CAMERAS);
	m_LauncherComms->RegisterMessageCallback(TEXT("LOAD_STAGE"), &LOAD_STAGE);
	m_LauncherComms->RegisterMessageCallback(TEXT("DEFAULTVEHICLE"), &DEFAULTVEHICLE);
	m_LauncherComms->RegisterMessageCallback(TEXT("VEHICLEUPDATE"), &VEHICLEUPDATE);
	m_LauncherComms->RegisterMessageCallback(TEXT("GETACTIVEORBITNAME"), &GETACTIVEORBITNAME);
	m_LauncherComms->RegisterMessageCallback(TEXT("RESUME_CAMERAS"), &RESUME_CAMERAS);
	m_LauncherComms->RegisterMessageCallback(TEXT("GETDEBUGMENU"), &GETDEBUGMENU);
	m_LauncherComms->RegisterMessageCallback(TEXT("SETCAMERADIRECTLYJSON"), &SETCAMERADIRECTLYJSON);
	m_LauncherComms->RegisterMessageCallback(TEXT("GET_SCREENSHOT_LAYER_OPTIONS"), &GET_SCREENSHOT_LAYER_OPTIONS);


	m_LauncherComms->RegisterMessageCallback(TEXT("SET_RICH_DATA_STREAM_ENABLED"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("MANUALCONTROLACTIVATE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SWEETSPOTINPUTMODE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CREATE_TEXTURE_BROWSER"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ANIMFINISHIMMEDIATE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SETFEATURES"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("LOGGING_SETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SERVERPORT"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("HANDDRIVE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SET_LOADING_SCREEN_PARAMETERS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SETUSERDATADIR"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("TRANSITIONTIME"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("LOADDEFAULTCAR"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("REALSITTINGHEIGHT"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("AUDIOVOLUME"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("PLAYAMBIENTAUDIO"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("AUDIOMANAGERSETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("DYNAMICCULLING"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENABLECARFADETRANSITION"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENABLELOADCIRCLE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("REPORT_CERTIFIED_EFFECTS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CHANGECACHELIMIT"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("XRAYMAXDEPTH"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("XRAYSOLIDORANGE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("FADECOLOUR"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ALLOWZLLOADSCREENFADES"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ALLOWTEXTONLYFADES"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("OVRTRACKINGTIMEOUT"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("OVRCHAIRHEIGHT"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("OVRCHAIRDISTANCE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("OVRCAMERAHEIGHT"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("OVRUSESIMPLE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("S3DSEPSCALE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("S3DCONVOFFSET"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CAMERAACTIVITYTRACKER"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENABLEZLSHAREDSURFACE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SECONDSCREENBEHAVIOURSETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("USETIMETRANSITION"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENABLEAUTOMATICHARDWARECALIBRATION"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENABLEDITHER"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ZLSHAREDSURFACEBOTHEYES"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("VRCAMERAMASKENABLE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("VEOUTPUTSINGLECROPPEDEYE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ZLSHAREDSURFACEFULLSCREENEYE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CAMERAORBITDURATION"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CAMERACONFIGSPEED"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("FLYCAMTABLETPROPERTIES"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("INTERIORSTATICCAMPROPERTIES"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("INTERIORSTATICCAMORBITPROPERTIES"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENVIRONMENTFADETIME"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CHECKPRCODESPECIFICPOST"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("AVAILABLEENVIRONMENTS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SCREENSAVER_PARAMS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ANIM_MANAGER_SETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ALLOWCAMERAWHEELFOLLOW"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SETZOOMBEHAVIOURCAMERAS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SETUPSEQUENCEONLIGHTCHANGE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SETUPMOTIONBLURIGNOREANIMS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SETUPSHARKCAMPUSHCAMERAS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("IGNOREMOTIONBLURONWHEELSTRAIGHT"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("USENODEFALLBACK"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CLOUDORBITCAMCONFIG"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("POLL_CAMERAS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("WHEELCULLINGENABLED"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CARLOADCACHING"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ANIMSEQUENCEEDITORINTERFACEENABLED"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CRESTFLOATINGPHYSICSMAXMODE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("INTERACTIONPLANE_CONFIG"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CONFIGUREPROCEDURALANIMATION"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("TRACKINGFADE"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENABLE_IK_DRIVER"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CARLIGHTS_FROM_TELEMETRY"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SETVR"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("VRS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("GRAPHICSSETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("DXRSETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ZLTEMPORARYFILEMANAGER_SETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SGAA_ANTIALIASING"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("META_SETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("GENERAL_SETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("AUDIOSETUP"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("NUM_CACHED_ENVIRONMENTS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENVIRONMENT_VARIANCE_PERSISTENT_VARIANCES"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENVIRONMENT_VARIANCE_DYNAMIC_SLOTS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENVIRONMENT_VARIANCE_PRECACHE_ALL"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ACAASETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ASSETLOADINGSETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("MULTIMESSAGELENGTH"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CARFEATUREINFO"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("REQUESTCAMANIMS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CHECK_SOUND"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SETMOTIONBLUR"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("FULLFOCUSDOF"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("SET_ZL_TIME"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENABLE_SGAA"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("ENABLEZLSIMPLEACAA"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("RESET_SYSTEM_PERSISTENT_EFFECTS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("PROXY3SETTINGS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CLOUD_CLIENT_DETAILS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CAMERAACTIVITYEVENTS"), &DummyCallback);
	m_LauncherComms->RegisterMessageCallback(TEXT("CAMERA_TRACKER_INTERVAL"), &DummyCallback);	
	m_LauncherComms->RegisterMessageCallback(TEXT("CASESENSITIVEFILESYSTEM"), &DummyCallback);	
	m_LauncherComms->RegisterMessageCallback(TEXT("ALLOWTEXTONLYFADES"), &DummyCallback);	
	m_LauncherComms->RegisterMessageCallback(TEXT("SCREENSHOTSETTINGS"), &DummyCallback);	
	m_LauncherComms->RegisterMessageCallback(TEXT("FAILLOADSCREENCARLOAD"), &DummyCallback);	
}

void MessageCallbacks::SetServerVersion(MessageWithData* msg)
{
	int32 serverVer = -1;
	if (FDefaultValueHelper::ParseInt(msg->m_messageData, serverVer))
	{
		m_LauncherComms->SetZLServerVersion(serverVer);
	}
}

void MessageCallbacks::CloudStreamSettings(MessageWithData* msg)
{
	UE_LOG(LogMessageCallbacks, Verbose, TEXT("Cloud Stream Settings"));
	auto ansiString = StringCast<ANSICHAR>(*msg->m_messageData);
	const char* charMessage = ansiString.Get();

	ZLCloudPlugin::CloudStream2::InitCloudStreamSettings(charMessage);
}

void MessageCallbacks::SetOmnistreamSettings(MessageWithData* msg)
{
	UE_LOG(LogMessageCallbacks, Verbose, TEXT("OmniStream Settings"));

	auto ansiString = StringCast<ANSICHAR>(*msg->m_messageData);
	const char* stateJson = ansiString.Get();

	TSharedPtr<FJsonObject> JsonParsed;
	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(stateJson);
	if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
	{
		//Any omnistream advanced config options from runinfo parsed here, as we dont store pluign ini file in staged builds.
		if (JsonParsed != nullptr)
		{

#if WITH_EDITOR
			const UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();
			check(Settings);

			UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->m_stateRequestWarningTime = Settings->stateRequestWarningTime;
			UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->m_stateRequestTimeout = Settings->stateRequestTimeout;
#else

			float stateWarningTime, stateTimeout;
			if (JsonParsed->TryGetNumberField(FString("stateWarningTime"), stateWarningTime))
				UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->m_stateRequestWarningTime = stateWarningTime;

			if(JsonParsed->TryGetNumberField(FString("stateTimeout"), stateTimeout))
				UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->m_stateRequestTimeout = stateTimeout;
#endif
		}
	}

}

void MessageCallbacks::CloudStreamConnected(MessageWithData* msg)
{
	//This function is for when the IM connects to the server, not when the browser connects to the plugin
	int32 connected = -1;
	if (FDefaultValueHelper::ParseInt(msg->m_messageData, connected))
	{
		bool newConnectionState = connected == 1;
		if (newConnectionState != UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetStreamConnected())
		{
			if (newConnectionState)
			{

			}
			else
			{
				if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Cloud Stream Disconnected"));

					// This is to forcibly cut the peer connection on VEDisconnect in ZLServer.
					if (ZLCloudPlugin::CloudStream2::IsPluginInitialised())
						CloudStream2DLL::DeletePeerConnection();

					Delegates->OnDisconnectedStream.Broadcast();
				}
			}

			UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->SetStreamConnected(newConnectionState);
		}
	}
}

void MessageCallbacks::CaptureScreenshot(MessageWithData* msg)
{
	UE_LOG(LogZLCloudPlugin, Display, TEXT("MessageCallbacks::CaptureScreenshot"));
	auto ansiString = StringCast<ANSICHAR>(*msg->m_messageData);
	const char* charMessage = ansiString.Get();
	UWorld* World = GEngine->GetCurrentPlayWorld();

#if WITH_EDITOR
	if (World == nullptr) //Fix for running in editor
	{
		if (UGameViewportClient* viewport = GEngine->GameViewport)
		{
			World = viewport->GetWorld();
		}
	}

	if (!ZLCloudPlugin::CloudStream2::IsInputHandling() || World == nullptr)
	{
		msg->SetReply("IMAGE_REQUEST_ERROR", "{\"error\":\"Request unhandled, no active Play-In-Editor/Standalone running\"}");
		return;
	}
#endif

	FString requestErrorMsg = "";
	bool requestQueued = ZLCloudPlugin::ZLScreenshot::Get()->RequestScreenshot(charMessage, World, requestErrorMsg);

	if (requestQueued)
	{
		//Include any config we want to expose to the framejob spec json
		const UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();
		check(Settings);

		TSharedPtr<FJsonObject> viewerConfigureResponseData = MakeShareable(new FJsonObject);
		TSharedPtr<FJsonObject> versionData = MakeShareable(new FJsonObject);
		versionData->SetStringField("unreal", ENGINE_VERSION_STRING);

		IPluginManager& PluginManager = IPluginManager::Get();
		TSharedPtr<IPlugin> ZlCloudStreamPlugin = PluginManager.FindPlugin("ZLCloudPlugin");
		if (ZlCloudStreamPlugin)
		{
			const FPluginDescriptor& Descriptor = ZlCloudStreamPlugin->GetDescriptor();
			versionData->SetStringField("omnistream", *Descriptor.VersionName);
		}
		viewerConfigureResponseData->SetNumberField("screenshotWaitFrameCountOverride", Settings->screenshotFrameWaitCountOverride);
		viewerConfigureResponseData->SetObjectField("version", versionData);


		FString responseDataStr;

		TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&responseDataStr);

		FJsonSerializer::Serialize(viewerConfigureResponseData.ToSharedRef(), writer);


		msg->SetReply("IMAGE_REQUEST_QUEUED", responseDataStr);
	}
	else
	{
		msg->SetReply("IMAGE_REQUEST_ERROR", FString::Printf(TEXT("{\"error\":\"%s\"}"), *requestErrorMsg));
	}
}

void MessageCallbacks::StartCaptureTrace(MessageWithData* msg)
{
	UE_LOG(LogZLCloudPlugin, Display, TEXT("MessageCallbacks::StartCaptureTrace"));

	ZLJobTrace::ON_START_JOBTRACE(msg->m_messageData);

	msg->SetReply("VE_JOBTRACE_STARTED");
}

void MessageCallbacks::EndCaptureTrace(MessageWithData* msg)
{
	UE_LOG(LogZLCloudPlugin, Display, TEXT("MessageCallbacks::EndCaptureTrace"));

	FString JobTraceDataJson = ZLJobTrace::ON_END_JOBTRACE(msg->m_messageData);
	msg->SetReply("RETURN_VE_JOBTRACE_DATA", JobTraceDataJson);
}

void MessageCallbacks::SetConnectState(MessageWithData* msg)
{
	UE_LOG(LogZLCloudPlugin, Display, TEXT("MessageCallbacks::SetConnectState"));

	UZLCloudPluginStateManager* stateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();

	bool defaultStateOnly = msg->m_messageData.IsEmpty();

	if (defaultStateOnly) //process default state if set
	{
		if (stateManager->HasDefaultInitialState())
		{
			//just set messagedata and execute rest of function
			msg->m_messageData = stateManager->GetDefaultInitialState();
		}
		else //Nothing specified and no default set, return ready as its unspecified
		{
			msg->SetReply("STATE_READY");
			return;
		}
	}
	else if (stateManager->HasDefaultInitialState())
	{
		msg->m_messageData = stateManager->MergeDefaultInitialState(msg->m_messageData);
	}

	auto ansiString = StringCast<ANSICHAR>(*msg->m_messageData);
	const char* stateJson = ansiString.Get();

	UE_LOG(LogZLCloudPlugin, Display, TEXT("SetConnectState::Attempting to parse initial state request %s"), UTF8_TO_TCHAR(stateJson));

	TSharedPtr<FJsonObject> JsonParsed;
	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(stateJson);
	if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
	{
		// wait till state is matching the request to complete the adoption
		if (JsonParsed != nullptr)
		{
			if (stateManager->CurrentStateCompareDiffs_Keys(JsonParsed).Num() == 0) //Already matches the state requested
			{
				msg->SetReply("STATE_READY");
			}
			else
			{
				stateManager->SetNotifyServerState(JsonParsed); //Notify server when onConnect state is matched

				FString stateDataStr;

				TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&stateDataStr);

				TSharedRef<FJsonObject> stateData = JsonParsed.ToSharedRef();

				if (!FJsonSerializer::Serialize(stateData, writer))
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Error broadcasting OnRecieveData for state request"));
				}

				if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("OnRecieveData OnConnect Request"));
					Delegates->OnRecieveData.Broadcast(stateDataStr);
				}

				msg->SetReply("STATE_REQUESTED");
			}
		}
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("Connect state data could not be parsed"));
		if(defaultStateOnly)
			msg->SetReply("STATE_READY");
		else
			msg->SetReply("STATE_ERROR", "{\"error\":\"Connect state data could not be parsed\"}");
	}
}

void MessageCallbacks::SetOnDemandProcessingState(MessageWithData* msg)
{
	if (!msg->m_messageData.IsEmpty())
	{
		bool onDemandMode = msg->m_messageData.ToBool();
		ZLCloudPlugin::FZLCloudPluginModule::GetModule()->SetOnDemandMode(onDemandMode);
		ZLCloudPlugin::ZLScreenshot::Get()->Set2DODMode(onDemandMode);
	}
}

void MessageCallbacks::GetAllZlCertifiedEffects(MessageWithData* msg)
{
	FString JsonString = R"({
        "ZLCertifiedEffects": {
            "ZLCert_Light": {
                "DisplayName": "Light",
                "Category": "Render Effects",
                "IncompatibilityFlags": [],
                "SpotLightOrder": 0,
                "AlwaysActive": true,
                "enabled": true
                }
            }
        })";

	JsonString = UZLSpotLightDataDrivenUIManager::SyncDataToServer(true);

	msg->SetReply("RETURN_ALL_ZLCERTIFIED_EFFECTS", JsonString);
}

void MessageCallbacks::GetAllActiveZlCertifiedEffects(MessageWithData* msg)
{
	FString JsonString = R"({
        "ZLCertifiedEffects": {
            "ZLCert_Light": {
                "DisplayName": "Light",
                "Category": "Render Effects",
                "IncompatibilityFlags": [],
                "SpotLightOrder": 0,
                "AlwaysActive": true,
                "enabled": true
                }
            }
        })";

	JsonString = UZLSpotLightDataDrivenUIManager::SyncDataToServer(true);

	msg->SetReply("RETURN_ALL_ACTIVE_ZLCERTIFIED_EFFECTS", JsonString);
}

void MessageCallbacks::GetAllUiDetailsForZlCertifiedEffects(MessageWithData* msg)
{
    FString JsonString = R"({
        "ZLCertifiedEffectsUI": {
            "ZLCert_Light": {
                "Version": "0.0.4",
                "DisplayName": "Light",
                "Category": "Render Effects",
                "IncompatibilityFlags": [],
                "UserContentAssetData": [],
                "Properties": {
                    "m_Colour": {
                        "DisplayName": "Colour",
                        "Type": "Color",
                        "Tooltip": "",
                        "Value": [
                            0.0,
                            0.0,
                            0.0,
                            0.0
                        ],
                        "Min": null,
                        "Max": null,
                        "PossibleValues": [],
                        "Region": "",
                        "SubRegion": "",
                        "Order": 1,
                        "DrawInUI": true,
                        "DefaultVal": null,
                        "ChannelStrings": null,
                        "AdditionalUIHints": null,
                        "TextColor": null
                    },
                    "m_Intensity": {
                        "DisplayName": "Intensity",
                        "Type": "Float",
                        "Tooltip": "",
                        "Value": 1.0,
                        "Min": 0.0,
                        "Max": 10.0,
                        "PossibleValues": [],
                        "Region": "",
                        "SubRegion": "",
                        "Order": 1,
                        "DrawInUI": true,
                        "DefaultVal": 1.0,
                        "ChannelStrings": null,
                        "AdditionalUIHints": null,
                        "TextColor": null
                    }
                },
                "Active": true,
                "AlwaysActive": true,
                "Regions": {}
            }
        }
    })";

	JsonString = UZLSpotLightDataDrivenUIManager::SyncDataToServer(false);
	
	msg->SetReply("RETURN_UI_DETAILS_FOR_ZLCERTIFIED_EFFECTS", JsonString);
}

void MessageCallbacks::SetZlCertifiedEffects(MessageWithData* msg)
{
	// Update values to Blueprint
	UZLSpotLightDataDrivenUIManager::ReceiveDataFromServer(msg->m_messageData);

    //Return all UI details
	GetAllUiDetailsForZlCertifiedEffects(msg);
}

void MessageCallbacks::DisableZlCertifiedEffects(MessageWithData* msg)
{
	// Disable all effects

	//Return all UI details
	GetAllUiDetailsForZlCertifiedEffects(msg);
}

void MessageCallbacks::GetCertifiedEffectDevelopmentStates(MessageWithData* msg)
{
	msg->SetReply("RETURN_CERTIFIED_EFFECT_DEVELOPMENT_STATES", "{\"developmentStates\":[\"Live\"]}");
}

//Dummy
/////////////////////////////////////////////////////////////////////////////

void MessageCallbacks::UNLOAD_ASSET_BUNDLE(MessageWithData* msg)
{
	msg->SetReply("UNLOAD_ASSET_BUNDLE_DONE");
}

void MessageCallbacks::GET_ALL_VE_CAPABILITIES(MessageWithData* msg)
{
	msg->SetReply("RETURN_ALL_VE_CAPABILTIIES", "{\"screenshot\":{\"version\":1,\"formats\":[\"PNG\",\"BMP\",\"TIFF\",\"JPG\",\"EXR\",\"PSD\",\"WEBP\"],\"layers\":[\"DEFAULT\",\"ABSOLUTE_NORMALS\",\"AMBIENT_OCCLUSION\",\"DEPTH\",\"DIFFUSE\",\"ENVIRONMENT\",\"ENVIRONMENT_SHADOW\",\"FAR_TRANSPARENT_DEPTH\",\"LIGHTS\",\"LIGHTS_PROJECTORS\",\"MATERIAL_ID\",\"NEAR_TRANSPARENT_DEPTH\",\"NO_POST_EFFECTS\",\"NORMALS\",\"OBJECT_ID\",\"REFLECTION\",\"SHADOW\",\"SHADOW_MASK\",\"SPECULAR\",\"WORLD_POSITION\"],\"types\":[\"DEFAULT2D\",\"CUBEMAP360\",\"EQUIRECT360\",\"STEREOPANO360\"],\"quality\":[\"Fast\",\"Good\",\"Better\",\"Best\",\"Custom\"]},\"usercontent\":{\"version\":4,\"types\":[\"IMAGE\",\"VIDEO\",\"MESH\",\"DATA\",\"AUDIO\",\"NONE\",\"UNDEFINED\"],\"formats\":{\"IMAGE\":[\".png\",\".jpg\",\".jpeg\",\".tiff\",\".tif\",\".exr\",\".hdr\",\".bmp\",\".webp\"],\"DATA\":[\".*\",\".cube\",\".txt\"]},\"featureflags\":[\"supportsFileUrlMapping\"]},\"video\":{\"kframerates\":[\"f24\",\"f25\",\"f30\",\"f50\",\"f60\",\"f120\"],\"supportedOutputTypes\":[\"VIDEO\",\"ZIP\"]},\"certifiedeffects\":{\"version\":3,\"incompatabilityflags\":[\"NONE\",\"Cloud\",\"VR\",\"Capture360\",\"HDRScreenshot\",\"CrestEnvironment\",\"DXRRequired\",\"NoDriverNodes\",\"NoTimeOfDay\",\"NoCarLights\",\"NoProps\",\"NoEnvMods\",\"NoneBespokeAR\",\"BespokeAR\",\"NoneTake\",\"TimeOfDay\",\"SceneSpotlight\"],\"alleffects\":[\"Legacy Layered Post Effects\",\"Bloom\",\"Depth Of Field\",\"Motion Blur\",\"Contrast Enhance\",\"Tonemapping\",\"Legacy Chromatic Aberration\",\"Lens Effect\",\"HDR Visualiser\",\"Lighting & Reflection Adjustments\",\"Screen Overlay\",\"Driver\",\"Ray Tracing\"],\"liveeffects\":[]},\"data\":{\"zlstream_version\":1},\"renderer\":{\"zlmaincamerarender_version\":1,\"zlvarianceshadowmap_version\":1,\"zldxrmanager_version\":1,\"zldxrreflectionopaque_version\":1,\"zldxrreflectiontrans_version\":1,\"zldxrshadows_version\":1,\"zldxrambientocclusion_version\":1,\"zldxrlights_version\":1},\"features\":{\"backplate_version\":1,\"dome_version\":1,\"thumbnail_version\":1,\"runtimegizmos_version\":2.2,\"proxy_version\":5,\"cloudplugin_version\":2},\"unity_version\":\"2021.2.4z4\"}");
}

void MessageCallbacks::ORBIT_PAUSE(MessageWithData* msg)
{
	msg->SetReply("CAMERAPAUSED");
}

void MessageCallbacks::UPDATE_DXR_PROXY(MessageWithData* msg)
{
	msg->SetReply("UPDATE_DXR_PROXY_SUCCESS");
}

void MessageCallbacks::DISABLE_ALL_ZLCERTIFIED_EFFECTS(MessageWithData* msg)
{
	msg->SetReply("RETURN_ALL_ACTIVE_ZLCERTIFIED_EFFECTS", "{\"ZLCertifiedEffects\":{}}");//needs to return a json?
}

void MessageCallbacks::CURRENTCAMERA(MessageWithData* msg)
{
	msg->SetReply("CURRENTCAMERARETURN", "{\"yfov\":25,\"distance\":7.7,\"position\":{\"x\":0,\"y\":0.667216,\"z\":-0.0957269},\"rotation\":{\"yaw\":150,\"pitch\":4,\"roll\":0},\"frameOfReference\":\"world\",\"convergence\":-1,\"eyeSeparation\":0}");
}

void MessageCallbacks::GPUVALIDATOR_STARTVALIDATION(MessageWithData* msg)
{
	msg->SetReply("GPUVALIDATIONFINISHED", "{\"state\":\"Success\",\"frameinfo\":\"\",\"info\":\"GPUValidation: Succeeded\"}");
}

void MessageCallbacks::PAUSE_CAMERAS(MessageWithData* msg)
{
	msg->SetReply("CAMERAS_PAUSED");//Should server code be removed?
}

void MessageCallbacks::LOAD_STAGE(MessageWithData* msg)
{
	msg->SetReply("LOAD_STAGE_COMPLETE");
}

void MessageCallbacks::DEFAULTVEHICLE(MessageWithData* msg)
{
	msg->SetReply("VEHICLEREADY");
}

void MessageCallbacks::VEHICLEUPDATE(MessageWithData* msg)
{
	msg->SetReply("VEHICLEREADY");
}

void MessageCallbacks::GETACTIVEORBITNAME(MessageWithData* msg)
{
	msg->SetReply("NOTACTIVE", " ");
}

void MessageCallbacks::RESUME_CAMERAS(MessageWithData* msg)
{
	msg->SetReply("CAMERAS_RESUMED");
}

void MessageCallbacks::GETDEBUGMENU(MessageWithData* msg)
{
	msg->SetReply("RETURNDEBUGMENU", "{}");//needs to return a json?
}

void MessageCallbacks::SETCAMERADIRECTLYJSON(MessageWithData* msg)
{
	msg->SetReply("SETCAMERADIRECTLYRETURN");
}

void MessageCallbacks::GET_SCREENSHOT_LAYER_OPTIONS(MessageWithData* msg)
{
	msg->SetReply("RETURN_SCREENSHOT_LAYER_OPTIONS", "{\"layers\":{}}");
}


void MessageCallbacks::DummyCallback(MessageWithData* msg)
{
}


