// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginPlayerId.h"
#include "ZLCloudPluginDelegates.h"
#include "IZLCloudPluginModule.h"
#include "ZLCloudPluginModule.h"
#include "ZLStateKeyInfo.h"
#include "Containers/UnrealString.h"
#include "Serialization/JsonSerializer.h"
#include "Delegates/DelegateSignatureImpl.inl"
#include "Containers/Array.h"
#include "Containers/Queue.h"
#include "UObject/SoftObjectPtr.h"
#include "LauncherComms.h"
#include "ZLDebugUIWidget.h"
#include "ZLJobTrace.h"
#include "MoviePipeline.h"

#if WITH_EDITOR
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#endif

#include "ZLCloudPluginStateManager.generated.h"

ZLCLOUDPLUGIN_API TSharedPtr<FJsonObject> MergeJsonObjectsRecursive(const TSharedPtr<FJsonObject>& JsonObject1, const TSharedPtr<FJsonObject>& JsonObject2);


UENUM(BlueprintType)
enum class EStateDirtyReason : uint8
{ 
	state_processing, //A state request has been received and started processing
	state_processing_ended, //A state request has completed (success/timeout/unmatched)
	state_notify_web //Internal state change which needs to be passed on to web
};

UCLASS()
class ZLCLOUDPLUGIN_API UZLCloudPluginStateManager : public UObject
{
	GENERATED_BODY()

public:
	// Destructor to ensure proper cleanup
	virtual ~UZLCloudPluginStateManager();

	UFUNCTION()
	void OnMoviePipelineFinishedNotifyZLScreenshot(FMoviePipelineOutputData Results);

	void ProcessState(FString jsonString, bool doCurrentStateCompare, bool& Success);

	//State management
	void ConfirmStateChange(FString FieldName, bool& Success);
	void RemoveCurrentStateValue(FString FieldName);

	template <typename T> void GetRequestedStateValue(FString FieldName, bool instantConfirm, T& data, bool& Success);
	template <typename T> void GetCurrentStateValue(FString FieldName, T& data, bool& Success);
	template <typename T> void SetCurrentStateValue(FString FieldName, T data, bool SubmitToProcessState);

	void SetCurrentSchema(UStateKeyInfoAsset* Asset);
	void AppendCurrentSchema(UStateKeyInfoAsset* Asset);
	void RemoveFromCurrentSchema(UStateKeyInfoAsset* Asset);
	UStateKeyInfoAsset* GetCurrentSchemaAsset();
	void ClearCurrentSchema();

	void GetCurrentStateValue_String(FString FieldName, FString& StringValue, bool& Success);
	void GetCurrentState(FString& jsonString);
	inline TSharedPtr<FJsonObject> GetRequestedState_JsonObject() { return JsonObject_out_requestedState; };

	void MergeTrackedStateIntoCurrentState(const FString& FieldName, TSharedPtr<FJsonObject> JsonObject);

	TArray<FString> CurrentStateCompareDiffs_Keys(TSharedPtr<FJsonObject> ComparisonJsonObject);
	TSharedPtr<FJsonObject> CurrentStateCompareDiffs(TSharedPtr<FJsonObject> ComparisonJsonObject);

	void SetNotifyServerState(TSharedPtr<FJsonObject> ComparisonStateRequest);
	int32 CountLeavesInJsonObject(TSharedPtr<FJsonObject> JsonObject);

	void SaveCurrentStateToFile(FString fileName);
	void SendCurrentStateToWeb(bool completeState, TSharedPtr<FJsonObject> unmatchedRequestState = nullptr);

	//State websocket tick updates
	void SetStateDirty(EStateDirtyReason reason);
	void PushStateEventsToWeb();
	void SendFJsonObjectToWeb(TSharedPtr<FJsonObject> JsonObject);

	void ResetKnowWebState()
	{
		JsonObject_web_currentState.Reset();
		JsonObject_web_currentState = MakeShareable(new FJsonObject);
	}

	bool HasDefaultInitialState()
	{
		return JsonString_DefaultInitialState != "";
	}

	TSharedPtr<FJsonObject> GetServerUnmatchedNotifyState()
	{
		return JsonObject_serverNotifyUnmatchedState;
	}

	void ResetServerUnmatchedNotifyState()
	{
		JsonObject_serverNotifyUnmatchedState.Reset();
		JsonObject_serverNotifyUnmatchedState = MakeShareable(new FJsonObject);
	}


	void SetDefaultInitialState(FString initialStateJSONString, bool triggerStateSet)
	{
		JsonString_DefaultInitialState = initialStateJSONString;

		if (triggerStateSet)
		{
			if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
			{
				UE_LOG(LogZLCloudPlugin, Display, TEXT("Trigger Default Initial State Set Request"));
				Delegates->OnRecieveData.Broadcast(GetDefaultInitialState());
			}
		}
	}

	FString GetDefaultInitialState()
	{
		return JsonString_DefaultInitialState;
	}

	FString MergeDefaultInitialState(FString overrideInitialStateJSONString);

	void Update(LauncherComms* launcherComms);

	void ResetCurrentAppState(FString jsonString);
	void ResetCurrentAppState(TSharedPtr<FJsonObject> jsonObj);
	TSharedPtr<FJsonObject> GetCurrentAppState() { return JsonObject_currentState; };
	bool IsProcessingStateRequest();
	void PopStateRequestQueue();
	void ClearProcessingState();

	void RebuildDebugUI(UStateKeyInfoAsset* schemaAsset);
	inline void DestroyDebugUI()
	{
		if (IsValid(DebugUIWidget))
		{
			DebugUIWidget->RemoveFromParent();
			DebugUIWidget = nullptr;
		}
#if WITH_EDITOR
		if (DebugUITab.IsValid())
		{
			FGlobalTabmanager::Get()->TryInvokeTab(DebugUITabId)->RequestCloseTab();
			DebugUITab.Reset();
		}
#endif
	}
	
	inline void SetDebugUIVisibility()
	{
		SetDebugUIVisibility(!m_debugUIVisible);
	}
	void SetDebugUIVisibility(bool visible);
	
	// Editor tab toggle
	bool GetShowDebugUIInEditorTab() const { return m_showDebugUIInEditorTab; }
	void SetShowDebugUIInEditorTab(bool showInEditorTab);

	//Debug
	void DumpState();

	//Stream Status
	bool GetStreamConnected() { return m_StreamConnected; }
	void SetStreamConnected(bool connected);

	/**
	 * Create the singleton.
	 */
	static UZLCloudPluginStateManager* CreateInstance();

	static UZLCloudPluginStateManager* GetZLCloudPluginStateManager()
	{
		if (Singleton == nullptr)
		{
			return CreateInstance();
		}
		return Singleton;
	}

	UZLCloudPluginStateManager()
	{
		JsonObject_currentState = MakeShareable(new FJsonObject);
		JsonObject_processingState = MakeShareable(new FJsonObject);
		JsonObject_in_requestedState = MakeShareable(new FJsonObject);
		JsonObject_out_requestedState = MakeShareable(new FJsonObject);
		JsonObject_web_currentState = MakeShareable(new FJsonObject);
		JsonObject_serverNotifyState = MakeShareable(new FJsonObject);
		JsonObject_serverNotifyUnmatchedState = MakeShareable(new FJsonObject);
		request_recieved_id = 0;
	}


	int m_stateRequestWarningTime = 10;
	int m_stateRequestTimeout = 30;
	double m_lastStateWarningPrintTime;
	

	//Debug stats
	int request_recieved_id;

	UPROPERTY()
	UZLDebugUIWidget* DebugUIWidget = nullptr;

private:
	// The singleton object.
	static UZLCloudPluginStateManager* Singleton;

	UPROPERTY() // This makes it a strong reference that prevents GC
	UStateKeyInfoAsset* ActiveSchema = nullptr;

	bool m_StreamConnected = false;
	bool m_needServerNotify = false;
	bool m_stateDirty = false;
	EStateDirtyReason m_stateDirtyReason;
	double m_serverStateNotifyStart;

	TSharedPtr<FJsonObject> JsonObject_currentState;//Current application state
	TSharedPtr<FJsonObject> JsonObject_processingState;//State being processed/updated
	TSharedPtr<FJsonObject> JsonObject_in_requestedState;
	TSharedPtr<FJsonObject> JsonObject_web_currentState;//Current web state - sent to web page
	TSharedPtr<FJsonObject> JsonObject_out_requestedState;//Filtered state (won't contain stuff thats already set)

	FString JsonString_DefaultInitialState = FString("");
	TQueue<FString> JsonString_in_requestQueue;
	int32 s_requestQueueLength = 0;


	double JsonObject_processingStateStartTime; //Timestamp of last processing request (used for timeout + delay warnings)
	int32 JsonObject_requestedStateLeafCount; //Leaves in requested state after diff comparison to current, used for validating unknown/timed out fields
	int32 JsonObject_processingStateLeafCount; //Number of leaves actually processed from request state
	int32 JsonObject_processingStateFinishedLeaves; //Number of leaves that have reached finished/current state

	const FString s_requestIdStr = "RequestId";

	TSharedPtr<FJsonObject> JsonObject_serverNotifyState; //A comparison object used for specific blocking server requests (onConnect, resetState)
	TSharedPtr<FJsonObject> JsonObject_serverNotifyUnmatchedState;

	bool m_debugUIVisible = false;
	bool m_showDebugUIInEditorTab = false;
	UStateKeyInfoAsset* m_lastSetSchema = nullptr; // Track last schema to avoid unnecessary SetTargetSchema calls

	// Helper function to setup viewport mode (used in both editor and non-editor builds)
	void SetupViewportMode();

#if WITH_EDITOR
	// Helper function to create the editor tab content
	TSharedRef<SDockTab> CreateDebugUITabContent(TSharedRef<SWidget> WidgetRef, TSharedRef<float> ScaleValue);
#endif

#if WITH_EDITOR
	static const FName DebugUITabId;
	TSharedPtr<SDockTab> DebugUITab;
#endif

	void CopyRequestId();
};

USTRUCT(BlueprintType)
struct ZLCLOUDPLUGIN_API FSubKeyValueResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SubKeyValueResult")
	FString Key;

	UPROPERTY(BlueprintReadOnly, Category = "SubKeyValueResult")
	FString StringValue;

	UPROPERTY(BlueprintReadOnly, Category = "SubKeyValueResult")
	float NumberValue;

	UPROPERTY(BlueprintReadOnly, Category = "SubKeyValueResult")
	bool BoolValue;

	UPROPERTY(BlueprintReadOnly, Category = "SubKeyValueResult")
	TArray<FString> StringArray;

	UPROPERTY(BlueprintReadOnly, Category = "SubKeyValueResult")
	TArray<float> NumberArray;

	UPROPERTY(BlueprintReadOnly, Category = "SubKeyValueResult")
	TArray<bool> BoolArray;

	UPROPERTY(BlueprintReadOnly, Category = "SubKeyValueResult")
	EStateKeyDataType Type;
};

UCLASS()
class ZLCLOUDPLUGIN_API UZLCloudPluginStateManagerBlueprints : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

private:
	// Holds a reference to the last schema asset selected by the user, maybe a better way to get this?
	inline static TWeakObjectPtr<UStateKeyInfoAsset> LastSelectedAsset;

public:

	/**
	 * Converts a double precision float to single precision required for some blueprint node inputs (note: this can reuslt in loss of precision)
	 *
	 */
	UFUNCTION(BlueprintPure, Category = "Conversion")
	static float CastDoubleToFloat(double Value) { return static_cast<float>(Value); }

	/**
	 * Returns true if the app is serving a client for content generation jobs
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static bool IsContentGenerationMode()
	{
		return ZLCloudPlugin::FZLCloudPluginModule::GetModule()->IsContentGenerationMode();
	}

	/**
	 * Returns true if the app is serving a CloudXR client
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static bool IsXRMode()
	{
		return ZLCloudPlugin::FZLCloudPluginModule::GetModule()->IsXRMode();
	}

	/**
	 * Process json data into the state manager
	 *
	 * @param   jsonString	json string including state block
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void ProcessState(FString jsonString, bool doCurrentStateCompare, bool& Success)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->ProcessState(jsonString, doCurrentStateCompare, Success);
	}

	/**
	 * Update current state
	 *
	 * @param   FieldName	The name of the field to confirm current state changed from processing state, will trigger message back to server
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void ConfirmStateChange(FString FieldName, bool& Success)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->ConfirmStateChange(FieldName, Success);
	}

	/**
	 * Removes specified field from current state. Use when app changes mean a tracked state value is no longer relevant/valid.
	 *
	 * @param   FieldName	The name of the field to remove from the current state
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void RemoveStateValue(FString FieldName)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->RemoveCurrentStateValue(FieldName);
	}

	/**
	 * Resets the current application state
	 * @param   jsonString	optional string containing a json data that will set the initial state
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void ResetCurrentAppState(FString jsonString)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->ResetCurrentAppState(jsonString);
	}

	/**
	 * Helper function to extract a string field from a JSON descriptor of a
	 * UI interaction given its field name.
	 * The field name may be hierarchical, delimited by a period. For example,
	 * to access the Width value of a Resolution command above you should use
	 * "Resolution.Width" to get the width value.
	 * @param FieldName - The name of the field to look for in the JSON.
	 * @param instantConfirm - Update state right away, if set to false call ConfirmStateChange once your processing is done
	 * @param StringValue - The string value associated with the field name.
	 * @param Success - True if the field exists in the JSON data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void GetRequestedStateValue(FString FieldName, bool instantConfirm, FString& StringValue, bool& Success)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetRequestedStateValue<FString>(FieldName, instantConfirm, StringValue, Success);
	}

	static void GetRequestedStateValue(FString FieldName, bool instantConfirm, TSharedPtr<FJsonValue>& value, bool& Success)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetRequestedStateValue< TSharedPtr<FJsonValue>>(FieldName, instantConfirm, value, Success);
	}

	/**
	 * Helper function to extract a string field from a JSON descriptor of a
	 * UI interaction given its field name.
	 * The field name may be hierarchical, delimited by a period. For example,
	 * to access the Width value of a Resolution command above you should use
	 * "Resolution.Width" to get the width value.
	 * @param FieldName - The name of the field to look for in the JSON.
	 * @param instantConfirm - Update state right away, if set to false call ConfirmStateChange once your processing is done
	 * @param StringValue - The string value associated with the field name.
	 * @param Success - True if the field exists in the JSON data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void GetRequestedStateValueString(FString FieldName, bool instantConfirm, FString& StringValue, bool& Success)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetRequestedStateValue<FString>(FieldName, instantConfirm, StringValue, Success);
	}

	/**
	 * Helper function to extract a boolean field from a JSON descriptor of a
	 * UI interaction given its field name.
	 * @param FieldName - The name of the field to look for in the JSON.
	 * @param instantConfirm - Update state right away, if set to false call ConfirmStateChange once your processing is done
	 * @param BooleanValue - The boolean value associated with the field name.
	 * @param Success - True if the field exists in the JSON data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void GetRequestedStateValueBool(FString FieldName, bool instantConfirm, bool& BooleanValue, bool& Success)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetRequestedStateValue<bool>(FieldName, instantConfirm, BooleanValue, Success);
	}

	/**
	 * Helper function to extract a number field from a JSON descriptor of a
	 * UI interaction given its field name.
	 * @param FieldName - The name of the field to look for in the JSON.
	 * @param instantConfirm - Update state right away, if set to false call ConfirmStateChange once your processing is done
	 * @param NumberValue - The number (as double) value associated with the field name.
	 * @param Success - True if the field exists in the JSON data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void GetRequestedStateValueNumber(FString FieldName, bool instantConfirm, float& NumberValue, bool& Success)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetRequestedStateValue<float>(FieldName, instantConfirm, NumberValue, Success);
	}

	/**
	 * Helper function to extract a array from a JSON descriptor of a
	 * UI interaction given its field name.
	 * @param FieldName - The name of the field to look for in the JSON.
	 * @param instantConfirm - Update state right away, if set to false call ConfirmStateChange once your processing is done
	 * @param Array - The array associated with the field name.
	 * @param Success - True if the field exists in the JSON data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void GetRequestedStateValueStringArray(FString FieldName, bool instantConfirm, TArray<FString>& Array, bool& Success)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetRequestedStateValue<TArray<FString>>(FieldName, instantConfirm, Array, Success);
	}

	/**
	 * Helper function to extract a array from a JSON descriptor of a
	 * UI interaction given its field name.
	 * @param FieldName - The name of the field to look for in the JSON.
	 * @param instantConfirm - Update state right away, if set to false call ConfirmStateChange once your processing is done
	 * @param Array - The array associated with the field name.
	 * @param Success - True if the field exists in the JSON data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void GetRequestedStateValueBoolArray(FString FieldName, bool instantConfirm, TArray<bool>& Array, bool& Success)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetRequestedStateValue<TArray<bool>>(FieldName, instantConfirm, Array, Success);
	}

	/**
	 * Helper function to extract a array from a JSON descriptor of a
	 * UI interaction given its field name.
	 * @param FieldName - The name of the field to look for in the JSON.
	 * @param instantConfirm - Update state right away, if set to false call ConfirmStateChange once your processing is done
	 * @param Array - The array associated with the field name.
	 * @param Success - True if the field exists in the JSON data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void GetRequestedStateValueNumberArray(FString FieldName, bool instantConfirm, TArray<float>& Array, bool& Success)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetRequestedStateValue<TArray<float>>(FieldName, instantConfirm, Array, Success);
	}


	/**
	 * Helper function to extract a string field from a JSON descriptor of a
	 * UI interaction given its field name.
	 * @param FieldName - The name of the field to look for in the JSON.
	 * @param StringValue - The string value associated with the field name.
	 * @param Success - True if the field exists in the JSON data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void GetCurrentStateValueString(FString FieldName, FString& StringValue, bool& Success)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetCurrentStateValue_String(FieldName, StringValue, Success);
	}

	/**
	 * Get a json that defines the current state of the application
	 * @param jsonString - Json string containing all data used to set the current state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void GetCurrentState(FString& jsonString)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetCurrentState(jsonString);
	}


	/**
	 * Save a json that defines the current state of the application
	 * @param fileName - name of file (e.g. state.json)
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void SaveCurrentStateToFile(FString fileName)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->SaveCurrentStateToFile(fileName);
	}


	/**
	 * Send a json that defines the current state of the application
	 * @param complete - path to file
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void SendCurrentStateToWeb(bool completeState)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->SendCurrentStateToWeb(completeState);
	}


	/**
	 * Dump all current state to a file
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State Debug")
	static void DumpState()
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->DumpState();
	}

	/**
	 * Get if a stream is connected
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static bool GetStreamConnected()
	{
		return UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetStreamConnected();
	}

	/**
	 * Get if a stream is connected
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static bool IsInitialStateRequest()
	{
		return UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->request_recieved_id == 1;
	}

	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static int GetStateRequestID()
	{
		return UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->request_recieved_id;
	}

	/**
	 * Set App Ready to Stream
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Events")
	static void SetAppReadyToStreamState(bool readyToStream)
	{
		ZLCloudPlugin::FZLCloudPluginModule::GetModule()->SetAppReadyToStream_Internal(readyToStream);
	}

	/**
	 * Set App Ready to Stream
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Events")
	static void SetDefaultInitialState(FString defaultState, bool triggerProcessDefaultStateNow = false)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->SetDefaultInitialState(defaultState, triggerProcessDefaultStateNow);
	}

	static void UpdateSchemaState(UStateKeyInfoAsset* Asset, bool triggerProcessStateNow = false, bool clearPreviousState = false)
	{
		TSharedPtr<FJsonObject> OutJsonObject = MakeShared<FJsonObject>();

		for (const auto& Pair : Asset->KeyInfos)
		{
			const FString& FullKey = Pair.Key;
			const FStateKeyInfo& Info = Pair.Value;

			TSharedPtr<FJsonValue> JsonValue;

			if (Info.DataType == "String")
				JsonValue = MakeShared<FJsonValueString>(Info.DefaultStringValue);
			else if (Info.DataType == "Number")
				JsonValue = MakeShared<FJsonValueNumber>(Info.DefaultNumberValue);
			else if (Info.DataType == "Bool")
				JsonValue = MakeShared<FJsonValueBoolean>(Info.DefaultBoolValue);
			else if (Info.DataType == "StringArray")
			{
				TArray<TSharedPtr<FJsonValue>> Arr;
				for (const FString& Val : Info.DefaultStringArray)
					Arr.Add(MakeShared<FJsonValueString>(Val));
				JsonValue = MakeShared<FJsonValueArray>(Arr);
			}
			else if (Info.DataType == "NumberArray")
			{
				TArray<TSharedPtr<FJsonValue>> Arr;
				for (float Val : Info.DefaultNumberArray)
					Arr.Add(MakeShared<FJsonValueNumber>(Val));
				JsonValue = MakeShared<FJsonValueArray>(Arr);
			}
			else if (Info.DataType == "BoolArray")
			{
				TArray<TSharedPtr<FJsonValue>> Arr;
				for (bool Val : Info.DefaultBoolArray)
					Arr.Add(MakeShared<FJsonValueBoolean>(Val));
				JsonValue = MakeShared<FJsonValueArray>(Arr);
			}


			if (JsonValue.IsValid())
			{
				TArray<FString> Parts;
				FullKey.ParseIntoArray(Parts, TEXT("."));

				TSharedPtr<FJsonObject> CurrentObj = OutJsonObject;
				for (int32 i = 0; i < Parts.Num(); ++i)
				{
					const FString& Part = Parts[i];

					if (i == Parts.Num() - 1)
					{
						CurrentObj->SetField(Part, JsonValue);
					}
					else
					{
						TSharedPtr<FJsonObject> NextObj;
						TSharedPtr<FJsonValue> Existing = CurrentObj->TryGetField(Part);

						if (Existing.IsValid() && Existing->Type == EJson::Object)
						{
							NextObj = Existing->AsObject();
						}
						else
						{
							NextObj = MakeShared<FJsonObject>();
							CurrentObj->SetObjectField(Part, NextObj);
						}

						CurrentObj = NextObj;
					}
				}
			}
		}

		if (triggerProcessStateNow)
		{
			if (clearPreviousState)
				UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->ResetCurrentAppState(MakeShared<FJsonObject>());

			if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
			{
				FString JsonString_Schema;
				TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_Schema, 1);
				FJsonSerializer::Serialize(OutJsonObject.ToSharedRef(), JsonWriter);
				JsonWriter->Close();

				UE_LOG(LogZLCloudPlugin, Display, TEXT("Trigger Schema State Set Request"));
				Delegates->OnRecieveData.Broadcast(JsonString_Schema);
			}
		}
		else
		{
			if (clearPreviousState)
				UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->ResetCurrentAppState(OutJsonObject);
			else
			{
				TSharedPtr<FJsonObject> MergedStateObject = MergeJsonObjectsRecursive(UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetCurrentAppState(), OutJsonObject);
				UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->ResetCurrentAppState(MergedStateObject);
			}
		}
	}

	/**
	 * Set the current state in state manager to match the provided Schema asset. Note: Correct default values should be specified in the selected schema if using this
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void SetCurrentSchema(UStateKeyInfoAsset* Asset, bool triggerProcessStateNow = false, bool clearPreviousState = true)
	{
		if (Asset != nullptr)
		{
			UpdateSchemaState(Asset, triggerProcessStateNow, clearPreviousState);

			UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->SetCurrentSchema(Asset);
		}
	}

	/**
	 * Add's keys and additional values to the existing ActiveSchema tracked by the state manager
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void AppendCurrentSchema(UStateKeyInfoAsset* Asset, bool triggerProcessStateNow = false, bool clearPreviousState = false)
	{
		if (Asset != nullptr)
		{
			UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->AppendCurrentSchema(Asset);

			UpdateSchemaState(UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetCurrentSchemaAsset(), triggerProcessStateNow, clearPreviousState);
		}
	}

	/**
	 * Removes keys and additional values to the existing ActiveSchema tracked by the state manager
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void RemoveFromCurrentSchema(UStateKeyInfoAsset* Asset, bool triggerProcessStateNow = false, bool clearPreviousState = false)
	{
		if (Asset != nullptr)
		{
			UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->RemoveFromCurrentSchema(Asset);

			UpdateSchemaState(UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetCurrentSchemaAsset(), triggerProcessStateNow, clearPreviousState);
		}
	}


	/**
	 * Toggles visibility of the OmniStream state Debug UI
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State")
	static void ToggleDebugUIVisibility()
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->SetDebugUIVisibility();
	}

	/**
	 * Explicitly sets visibility of the OmniStream state Debug UI
	 * @param visible - Whether the UI should be visible
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State", meta = (CallInEditor = "true"))
	static void SetDebugUIVisibility(bool visible)
	{
		UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->SetDebugUIVisibility(visible);
	}

	/**
	 * UStateKeyInfoAsset Internals
	 */
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State", meta = (BlueprintInternalUseOnly = "true"))
	static void GetRequestedSchemaValue(UStateKeyInfoAsset* Asset, FString KeyName, bool InstantConfirm,
		FString& OutString, float& OutNumber, bool& OutBool, TArray<FString>& OutStringArray, TArray<float>& OutNumberArray, TArray<bool>& OutBoolArray,
		bool& Success, FString& KeyOut, FLatentActionInfo LatentInfo) //Useful note: FLatentActionInfo here is not used but forces adding the Exec pins required to route this through our custom ZLK2Node
	{
		if (Asset && Asset->KeyInfos.Contains(KeyName))
		{
			KeyOut = KeyName;
			UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
			switch (Asset->KeyInfos[KeyName].GetDataTypeEnum())
			{
			case EStateKeyDataType::String:
				StateManager->GetRequestedStateValue<FString>(KeyName, InstantConfirm, OutString, Success);
				break;
			case EStateKeyDataType::StringArray:
				StateManager->GetRequestedStateValue<TArray<FString>>(KeyName, InstantConfirm, OutStringArray, Success);
				break;
			case EStateKeyDataType::Number:
				StateManager->GetRequestedStateValue<float>(KeyName, InstantConfirm, OutNumber, Success);
				break;
			case EStateKeyDataType::NumberArray:
				StateManager->GetRequestedStateValue<TArray<float>>(KeyName, InstantConfirm, OutNumberArray, Success);
				break;
			case EStateKeyDataType::Bool:
				StateManager->GetRequestedStateValue<bool>(KeyName, InstantConfirm, OutBool, Success);
				break;
			case EStateKeyDataType::BoolArray:
				StateManager->GetRequestedStateValue<TArray<bool>>(KeyName, InstantConfirm, OutBoolArray, Success);
				break;
			default:

				UE_LOG(LogZLCloudPlugin, Error, TEXT("Unknown or invalid data type for key %s in schema requested"), *KeyName);
				Success = false;
				break;
			}

			if (Success && StateManager->DebugUIWidget && StateManager->DebugUIWidget->IsVisible())
				StateManager->DebugUIWidget->TriggerRefreshUI();

		}
	}

	UFUNCTION(meta = (BlueprintInternalUseOnly = "true"))
	static void SetCurrentStateValueString(FString KeyName, FString InString, bool SubmitToProcessState = true)
	{
		UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
		StateManager->SetCurrentStateValue<FString>(KeyName, InString, SubmitToProcessState);
	}

	UFUNCTION(meta = (BlueprintInternalUseOnly = "true"))
	static void SetCurrentStateValueNumber(FString KeyName, float InNumber, bool SubmitToProcessState = true)
	{
		UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
		StateManager->SetCurrentStateValue<float>(KeyName, InNumber, SubmitToProcessState);
	}

	UFUNCTION(meta = (BlueprintInternalUseOnly = "true"))
	static void SetCurrentStateValueBool(FString KeyName, bool InBool, bool SubmitToProcessState = true)
	{
		UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
		StateManager->SetCurrentStateValue<bool>(KeyName, InBool, SubmitToProcessState);
	}

	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State", meta = (BlueprintInternalUseOnly = "true"))
	static void SetCurrentSchemaValue(UStateKeyInfoAsset* Asset, FString KeyName,
		FString InString, float InNumber, bool InBool, TArray<FString> InStringArray, TArray<float> InNumberArray, TArray<bool> InBoolArray,
		FLatentActionInfo LatentInfo) //Useful note: FLatentActionInfo here is not used but forces adding the Exec pins required to route this through our custom ZLK2Node
	{
		if (Asset && Asset->KeyInfos.Contains(KeyName))
		{
			UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
			switch (Asset->KeyInfos[KeyName].GetDataTypeEnum())
			{
			case EStateKeyDataType::String:
				StateManager->SetCurrentStateValue<FString>(KeyName, InString, false);
				break;
			case EStateKeyDataType::StringArray:
				StateManager->SetCurrentStateValue<TArray<FString>>(KeyName, InStringArray, false);
				break;
			case EStateKeyDataType::Number:
				StateManager->SetCurrentStateValue<float>(KeyName, InNumber, false);
				break;
			case EStateKeyDataType::NumberArray:
				StateManager->SetCurrentStateValue<TArray<float>>(KeyName, InNumberArray, false);
				break;
			case EStateKeyDataType::Bool:
				StateManager->SetCurrentStateValue<bool>(KeyName, InBool, false);
				break;
			case EStateKeyDataType::BoolArray:
				StateManager->SetCurrentStateValue<TArray<bool>>(KeyName, InBoolArray, false);
				break;
			default:
				UE_LOG(LogZLCloudPlugin, Error, TEXT("Unknown or invalid data type for key %s in schema requested"), *KeyName);
				break;
			}

		}
	}

	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream State", meta = (BlueprintInternalUseOnly = "true"))
	static void GetRequestedSchemaValueSubKeys(UStateKeyInfoAsset* Asset, FString ParentKey, bool InstantConfirm,
		TArray<FSubKeyValueResult>& Results, bool& Success)
	{
		bool AnySuccess = false;
		Success = false;
		Results.Empty();

		if (!Asset)
		{
			UE_LOG(LogZLCloudPlugin, Error, TEXT("Asset is null"));
			return;
		}
		UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();

		// Loop through all keys in the asset
		for (const TPair<FString, FStateKeyInfo>& Pair : Asset->KeyInfos)
		{
			const FString& FullKey = Pair.Key;

			// Check if key starts with the desired parent path and is a direct child (one level deeper)
			if (FullKey.StartsWith(ParentKey + "."))
			{
				FString SubPath = FullKey.RightChop(ParentKey.Len() + 1);
				if (!SubPath.Contains("."))
				{
					FSubKeyValueResult Result;
					Result.Key = FullKey;
					Result.Type = Pair.Value.GetDataTypeEnum();
					bool EntrySuccess = false;

					switch (Result.Type)
					{
					case EStateKeyDataType::String:
						StateManager->GetRequestedStateValue<FString>(FullKey, InstantConfirm, Result.StringValue, EntrySuccess);
						break;
					case EStateKeyDataType::StringArray:
						StateManager->GetRequestedStateValue<TArray<FString>>(FullKey, InstantConfirm, Result.StringArray, EntrySuccess);
						break;
					case EStateKeyDataType::Number:
						StateManager->GetRequestedStateValue<float>(FullKey, InstantConfirm, Result.NumberValue, EntrySuccess);
						break;
					case EStateKeyDataType::NumberArray:
						StateManager->GetRequestedStateValue<TArray<float>>(FullKey, InstantConfirm, Result.NumberArray, EntrySuccess);
						break;
					case EStateKeyDataType::Bool:
						StateManager->GetRequestedStateValue<bool>(FullKey, InstantConfirm, Result.BoolValue, EntrySuccess);
						break;
					case EStateKeyDataType::BoolArray:
						StateManager->GetRequestedStateValue<TArray<bool>>(FullKey, InstantConfirm, Result.BoolArray, EntrySuccess);
						break;
					default:
						UE_LOG(LogZLCloudPlugin, Warning, TEXT("Unsupported type for key %s"), *FullKey);
						Success = false;
						continue;
					}

					if (EntrySuccess)
					{
						AnySuccess = true;
						Results.Add(Result);
					}
				}
			}
		}

		Success = AnySuccess;

		if (Success && StateManager->DebugUIWidget && StateManager->DebugUIWidget->IsVisible())
			StateManager->DebugUIWidget->TriggerRefreshUI();
	}

	UFUNCTION(meta = (BlueprintInternalUseOnly = "true"))
	static void GetRequestedStateValueSubKeys(FString ParentKey, bool InstantConfirm,
		TArray<FSubKeyValueResult>& Results, bool& Success)
	{
		bool AnySuccess = false;
		Success = false;
		Results.Empty();

		UZLCloudPluginStateManager* StateManager = UZLCloudPluginStateManager::GetZLCloudPluginStateManager();
		if (!StateManager)
		{
			UE_LOG(LogZLCloudPlugin, Error, TEXT("StateManager is null"));
			return;
		}

		TSharedPtr<FJsonObject> CurrentJsonObject = StateManager->GetRequestedState_JsonObject();
		if (!CurrentJsonObject.IsValid())
		{
			UE_LOG(LogZLCloudPlugin, Warning, TEXT("RequestedState JSON object is invalid."));
			return;
		}

		TArray<FString> PathParts;
		ParentKey.ParseIntoArray(PathParts, TEXT("."), true);

		for (const FString& Part : PathParts)
		{
			const TSharedPtr<FJsonObject>* NextJsonObject = nullptr;
			if (CurrentJsonObject->TryGetObjectField(Part, NextJsonObject) && NextJsonObject && NextJsonObject->IsValid())
			{
				CurrentJsonObject = *NextJsonObject;
			}
			else
			{
				Success = false;
				return;
			}
		}

		TArray<FString> ChildKeys;
		CurrentJsonObject->Values.GenerateKeyArray(ChildKeys);

		for (const FString& SubKey : ChildKeys)
		{
			const TSharedPtr<FJsonValue>& JsonValue = CurrentJsonObject->Values.FindChecked(SubKey);

			const FString FullKey = ParentKey + TEXT(".") + SubKey;

			FSubKeyValueResult Result;
			Result.Key = FullKey;
			bool EntrySuccess = false;

			switch (JsonValue->Type)
			{
			case EJson::String:
				Result.Type = EStateKeyDataType::String;
				StateManager->GetRequestedStateValue<FString>(FullKey, InstantConfirm, Result.StringValue, EntrySuccess);
				break;

			case EJson::Number:
				Result.Type = EStateKeyDataType::Number;
				StateManager->GetRequestedStateValue<float>(FullKey, InstantConfirm, Result.NumberValue, EntrySuccess);
				break;

			case EJson::Boolean:
				Result.Type = EStateKeyDataType::Bool;
				StateManager->GetRequestedStateValue<bool>(FullKey, InstantConfirm, Result.BoolValue, EntrySuccess);
				break;

			case EJson::Array:
			{
				const TArray<TSharedPtr<FJsonValue>>& JsonArray = JsonValue->AsArray();
				if (JsonArray.Num() > 0)
				{
					switch (JsonArray[0]->Type)
					{
					case EJson::String:
						Result.Type = EStateKeyDataType::StringArray;
						StateManager->GetRequestedStateValue<TArray<FString>>(FullKey, InstantConfirm, Result.StringArray, EntrySuccess);
						break;
					case EJson::Number:
						Result.Type = EStateKeyDataType::NumberArray;
						StateManager->GetRequestedStateValue<TArray<float>>(FullKey, InstantConfirm, Result.NumberArray, EntrySuccess);
						break;
					case EJson::Boolean:
						Result.Type = EStateKeyDataType::BoolArray;
						StateManager->GetRequestedStateValue<TArray<bool>>(FullKey, InstantConfirm, Result.BoolArray, EntrySuccess);
						break;
					default:
						UE_LOG(LogZLCloudPlugin, Warning, TEXT("Unsupported array element type for key %s"), *FullKey);
						continue;
					}
				}
				else
				{
					continue;
				}
				break;
			}

			default:
				UE_LOG(LogZLCloudPlugin, Warning, TEXT("Unsupported JSON value type for key %s"), *FullKey);
				continue;
			}

			if (EntrySuccess)
			{
				AnySuccess = true;
				Results.Add(Result);
			}
		}

		Success = AnySuccess;

		if (Success && StateManager->DebugUIWidget && StateManager->DebugUIWidget->IsVisible())
			StateManager->DebugUIWidget->TriggerRefreshUI();
	}

	UFUNCTION(BlueprintCallable, Category = "Content Generation Profiling")
	static void StartProfilingTimer(const FString& Name)
	{
		ZLJobTrace::JOBTRACE_TIMER_START(Name);
	}


	UFUNCTION(BlueprintCallable, Category = "Content Generation Profiling")
	static void EndProfilingTimer(const FString& Name)
	{
		ZLJobTrace::JOBTRACE_TIMER_END(Name);
	}
};


