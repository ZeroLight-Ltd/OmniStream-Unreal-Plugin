// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IInputDeviceModule.h"
#include "Templates/SharedPointer.h"
#include "ZLCloudPluginPlayerId.h"
#include "IZLCloudPluginAudioSink.h"
#include "Templates/SharedPointer.h"
#include "ZLCloudPluginProtocol.h"

#include "IInputDevice.h"

class UTexture2D;
class UZLCloudPluginInput;

/**
* The public interface to this module
*/
class ZLCLOUDPLUGIN_API IZLCloudPluginModule : public IInputDeviceModule
{
protected:
	bool m_RunningCloudXRMode = false;
	bool m_isOnDemandMode = false;
public:
	inline bool IsContentGenerationMode() { return m_isOnDemandMode; }
	inline bool IsXRMode() { return m_RunningCloudXRMode; }

	/**
	* Singleton-like access to this module's interface.
	* Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	*
	* @return Returns singleton instance, loading the module on demand if needed
	*/
	static inline IZLCloudPluginModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IZLCloudPluginModule>("ZLCloudPlugin");
	}

	/**
	* Checks to see if this module is loaded.  
	*
	* @return True if the module is loaded.
	*/
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("ZLCloudPlugin");
	}

	/*
	* Event fired when internal streamer is initialized and the methods on this module are ready for use.
	*/
	DECLARE_EVENT_OneParam(IZLCloudPluginModule, FReadyEvent, IZLCloudPluginModule&)

	/*
	* A getter for the OnReady event. Intent is for users to call IZLCloudPluginModule::Get().OnReady().AddXXX.
	* @return The bindable OnReady event.
	*/
	virtual FReadyEvent& OnReady() = 0;

	/*
	* Is the ZLCloudPlugin module actually ready to use? Is the streamer created.
	* @return True if ZLCloudPlugin module methods are ready for use.
	*/
	virtual bool IsReady() = 0;

	/*
	 * Check if a Play-In-Editor session is running.
	 */
	virtual bool IsPIERunning() = 0;

	/*
	 * Sets the application flag for serving content generation config jobs. Enables triggering quality setting overrides for generation jobs.
	 */
	virtual void SetOnDemandMode(bool onDemandMode) = 0;


	/*
	 * Communicates to ZLServer the ready state of the app for cloud streaming
	 */
	virtual void SetAppReadyToStream_Internal(bool readyToStream) = 0;

	/**
	 * Returns a reference to the active viewport
	 * @return a reference to the active viewport.
	 */
	virtual TWeakPtr<SViewport> GetTargetViewport() = 0;


	/**
	 * Get the protocol currently used by each peer.
	 */
	virtual const ZLProtocol::FZLCloudPluginProtocol& GetProtocol() = 0;

	/**
	 * Send data to the browser
	 * @param jsonData - string of json data to send
	 */
	virtual void SendData(const FString& jsonData) = 0;

	/**
	 * Get the audio sink associated with a specific peer/player.
	 */
	virtual IZLCloudPluginAudioSink* GetPeerAudioSink(FZLCloudPluginPlayerId PlayerId) = 0;

	/**
	 * Get an audio sink that has no peers/players listening to it.
	 */
	virtual IZLCloudPluginAudioSink* GetUnlistenedAudioSink() = 0;

	/**
	 * Tell the input device about a new ZLCloudStream input component.
	 * @param InInputComponent - The new ZLCloudStream input component.
	 */
	virtual void AddInputComponent(UZLCloudPluginInput* InInputComponent) = 0;

	/*
	 * Tell the input device that a ZLCloudStream input component is no longer
	 * relevant.
	 * @param InInputComponent - The ZLCloudStream input component which is no longer relevant.
	 */
	virtual void RemoveInputComponent(UZLCloudPluginInput* InInputComponent) = 0;

	/*
	 * Get the input components currently attached to ZLCloudStream.
	 */
	virtual const TArray<UZLCloudPluginInput*> GetInputComponents() = 0;

};
