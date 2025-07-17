// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginStateManager.h"
#include "ZLScreenshot.h"
#include "Interfaces/IPluginManager.h"
#include <Runtime/Core/Public/Misc/FileHelper.h>
#include <ZLTrackedStateBlueprint.h>

UZLCloudPluginStateManager* UZLCloudPluginStateManager::Singleton = nullptr;

template void UZLCloudPluginStateManager::GetRequestedStateValue<FString>(FString, bool, FString&, bool&);
template void UZLCloudPluginStateManager::GetRequestedStateValue<bool>(FString, bool, bool&, bool&);
template void UZLCloudPluginStateManager::GetRequestedStateValue<double>(FString, bool, double&, bool&);
template void UZLCloudPluginStateManager::GetRequestedStateValue<float>(FString, bool, float&, bool&);
template void UZLCloudPluginStateManager::GetRequestedStateValue<TArray<FString>>(FString, bool, TArray<FString>&, bool&);
template void UZLCloudPluginStateManager::GetRequestedStateValue<TArray<bool>>(FString, bool, TArray<bool>&, bool&);
template void UZLCloudPluginStateManager::GetRequestedStateValue<TArray<double>>(FString, bool, TArray<double>&, bool&);
template void UZLCloudPluginStateManager::GetRequestedStateValue<TArray<float>>(FString, bool, TArray<float>&, bool&);
template void UZLCloudPluginStateManager::GetRequestedStateValue<TSharedPtr<FJsonValue>>(FString, bool, TSharedPtr<FJsonValue>&, bool&);
template void UZLCloudPluginStateManager::GetCurrentStateValue<FString>(FString, FString&, bool&);
template void UZLCloudPluginStateManager::GetCurrentStateValue<double>(FString, double&, bool&);
template void UZLCloudPluginStateManager::GetCurrentStateValue<bool>(FString, bool&, bool&);
template void UZLCloudPluginStateManager::GetCurrentStateValue<TArray<FString>>(FString, TArray<FString>&, bool&);
template void UZLCloudPluginStateManager::GetCurrentStateValue<TArray<bool>>(FString, TArray<bool>&, bool&);
template void UZLCloudPluginStateManager::GetCurrentStateValue<TArray<double>>(FString, TArray<double>&, bool&);
template void UZLCloudPluginStateManager::GetCurrentStateValue<TSharedPtr<FJsonValue>>(FString, TSharedPtr<FJsonValue>&, bool&);

UZLCloudPluginStateManager* UZLCloudPluginStateManager::CreateInstance()
{
	if (Singleton == nullptr)
	{
		Singleton = NewObject<UZLCloudPluginStateManager>();
		Singleton->AddToRoot();
		return Singleton;
	}
	return Singleton;
}

void UZLCloudPluginStateManager::ClearProcessingState()
{
	JsonObject_out_requestedState.Reset();
	JsonObject_out_requestedState = MakeShareable(new FJsonObject);
	JsonObject_in_requestedState.Reset();
	JsonObject_in_requestedState = MakeShareable(new FJsonObject);
	JsonObject_processingState.Reset();
	JsonObject_processingState = MakeShareable(new FJsonObject);
	JsonObject_requestedStateLeafCount = 0;
	JsonObject_processingStateLeafCount = 0;
	JsonObject_processingStateFinishedLeaves = 0;
}

void UZLCloudPluginStateManager::RebuildDebugUI(UStateKeyInfoAsset* schemaAsset)
{
	if (GEngine)
	{
		UWorld* World = GEngine->GetCurrentPlayWorld();

#if WITH_EDITOR
		if (World == nullptr) //Fix for running in editor
		{
			if (UGameViewportClient* viewport = GEngine->GameViewport)
			{
				World = viewport->GetWorld();
			}
		}
#endif

		if (!World) return;

		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC) return;

		UClass* WidgetClass = StaticLoadClass(UUserWidget::StaticClass(), nullptr, TEXT("/ZLCloudPlugin/UI/UW_ZLDebugUI.UW_ZLDebugUI_C"));

		if (WidgetClass)
		{
			if (!DebugUIWidget)
			{
				UZLDebugUIWidget* Widget = CreateWidget<UZLDebugUIWidget>(World, WidgetClass);
				if (Widget)
				{
					Widget->AddToViewport();

					FInputModeUIOnly InputMode;
					InputMode.SetWidgetToFocus(Widget->TakeWidget());
					InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);

					DebugUIWidget = Widget;

					DebugUIWidget->SetVisibility(m_debugUIVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);

					DebugUIWidget->SetTargetSchema(schemaAsset);
				}
			}
			else
				DebugUIWidget->SetTargetSchema(schemaAsset);
		}
	}
}

void UZLCloudPluginStateManager::ResetCurrentAppState(FString jsonString)
{
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(jsonString);
	if ((!jsonString.IsEmpty()) && FJsonSerializer::Deserialize(JsonReader, JsonObject_currentState))
	{
		UE_LOG(LogZLCloudPlugin, Verbose, TEXT("Reset current state using provided json"));
	}
	else
	{
		JsonObject_currentState.Reset();
		JsonObject_currentState = MakeShareable(new FJsonObject);
		UE_LOG(LogZLCloudPlugin, Verbose, TEXT("Reset current state to empty"));
	}
}

void UZLCloudPluginStateManager::ResetCurrentAppState(TSharedPtr<FJsonObject> jsonObj)
{
	JsonObject_currentState.Reset();
	JsonObject_currentState = jsonObj;
	UE_LOG(LogZLCloudPlugin, Verbose, TEXT("Set current state"));
}

void UZLCloudPluginStateManager::SetStreamConnected(bool connected)
{
	m_StreamConnected = connected;
	//On connect or disconnect reset the current known web state
	ResetKnowWebState();
}

bool CompareJsonValuesCaseSensitive(const TSharedPtr<FJsonValue>& OldJsonObject, const TSharedPtr<FJsonValue>& NewJsonObject)
{
	if (!OldJsonObject.IsValid() || !NewJsonObject.IsValid() || OldJsonObject->Type != NewJsonObject->Type)
	{
		return false;
	}

	switch (OldJsonObject->Type)
	{
	case EJson::String:
		return OldJsonObject->AsString().Equals(NewJsonObject->AsString(), ESearchCase::CaseSensitive);

	case EJson::Number:
		return OldJsonObject->AsNumber() == NewJsonObject->AsNumber();

	case EJson::Boolean:
		return OldJsonObject->AsBool() == NewJsonObject->AsBool();

	case EJson::Array:
	{
		const TArray<TSharedPtr<FJsonValue>>& ArrayA = OldJsonObject->AsArray();
		const TArray<TSharedPtr<FJsonValue>>& ArrayB = NewJsonObject->AsArray();

		if (ArrayA.Num() != ArrayB.Num())
		{
			return false;
		}

		for (int32 i = 0; i < ArrayA.Num(); i++)
		{
			if (!CompareJsonValuesCaseSensitive(ArrayA[i], ArrayB[i]))
			{
				return false;
			}
		}

		return true;
	}

	case EJson::Object:
	{
		const TSharedPtr<FJsonObject> ObjA = OldJsonObject->AsObject();
		const TSharedPtr<FJsonObject> ObjB = NewJsonObject->AsObject();

		if (!ObjA.IsValid() || !ObjB.IsValid() || ObjA->Values.Num() != ObjB->Values.Num())
		{
			return false;
		}

		for (const auto& Pair : ObjA->Values)
		{
			const TSharedPtr<FJsonValue>* FoundValue = ObjB->Values.Find(Pair.Key);
			if (!FoundValue || !CompareJsonValuesCaseSensitive(Pair.Value, *FoundValue))
			{
				return false;
			}
		}

		return true;
	}

	default:
		return false;
	}
}

TSharedPtr<FJsonObject> CreateDiffJsonObject(const TSharedPtr<FJsonObject>& OldJsonObject, const TSharedPtr<FJsonObject>& NewJsonObject)
{
	TSharedPtr<FJsonObject> DiffObject = MakeShareable(new FJsonObject);

	for (const auto& Pair : NewJsonObject->Values)
	{
		const FString& Key = Pair.Key;
		const TSharedPtr<FJsonValue>& NewValue = Pair.Value;

		if (OldJsonObject->HasField(Key))
		{
			const TSharedPtr<FJsonValue>& OldValue = OldJsonObject->TryGetField(Key);

			if (!OldValue.IsValid() || !NewValue.IsValid() || !CompareJsonValuesCaseSensitive(OldValue, NewValue))
			{
				// If both values are objects, recursively compare
				if (NewValue->Type == EJson::Object && OldValue->Type == EJson::Object)
				{
					TSharedPtr<FJsonObject> NestedDiff = CreateDiffJsonObject(OldValue->AsObject(), NewValue->AsObject());
					if (NestedDiff->Values.Num() > 0)
					{
						DiffObject->SetObjectField(Key, NestedDiff);
					}
				}
				else
				{
					DiffObject->SetField(Key, NewValue);
				}
			}
		}
		else
		{
			DiffObject->SetField(Key, NewValue);
		}
	}

	return DiffObject;
}

TSharedPtr<FJsonObject> MergeJsonObjectsRecursive(const TSharedPtr<FJsonObject>& JsonObject1, const TSharedPtr<FJsonObject>& JsonObject2)
{
	if (!JsonObject1.IsValid() || !JsonObject2.IsValid())
	{
		return nullptr;
	}

	// Create a copy of the first JsonObject to modify
	TSharedPtr<FJsonObject> MergedJsonObject = MakeShareable(new FJsonObject(*JsonObject1));

	// Iterate through all fields in the second JsonObject
	for (const auto& Pair : JsonObject2->Values)
	{
		if (Pair.Value->Type == EJson::Object)
		{
			// If the value is another JsonObject, recursively merge
			const TSharedPtr<FJsonObject>* SubObject1 = &JsonObject1->GetObjectField(Pair.Key);
			const TSharedPtr<FJsonObject>* SubObject2 = &JsonObject2->GetObjectField(Pair.Key);

			if (SubObject1 && SubObject2)
			{
				MergedJsonObject->SetObjectField(Pair.Key, MergeJsonObjectsRecursive(*SubObject1, *SubObject2));
			}
			else
			{
				MergedJsonObject->SetField(Pair.Key, Pair.Value);
			}
		}
		else
		{
			// For other types, simply set the value from the second JsonObject
			MergedJsonObject->SetField(Pair.Key, Pair.Value);
		}
	}

	return MergedJsonObject;
}

FString UZLCloudPluginStateManager::MergeDefaultInitialState(FString overrideInitialStateJSONString)
{
	FString mergedInitialState = overrideInitialStateJSONString;
	TSharedPtr<FJsonObject> JsonObject_defaultInitialState;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(GetDefaultInitialState());
	if (FJsonSerializer::Deserialize(JsonReader, JsonObject_defaultInitialState))
	{
		TSharedPtr<FJsonObject> JsonObject_overrideInitialState;
		TSharedRef<TJsonReader<>> JsonReader2 = TJsonReaderFactory<>::Create(overrideInitialStateJSONString);
		if (FJsonSerializer::Deserialize(JsonReader2, JsonObject_overrideInitialState))
		{
			TSharedPtr<FJsonObject> mergedOverrideInitialState = MergeJsonObjectsRecursive(JsonObject_defaultInitialState, JsonObject_overrideInitialState);

			FString JsonString_mergedInitialState;
			TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_mergedInitialState, 1);
			FJsonSerializer::Serialize(mergedOverrideInitialState.ToSharedRef(), JsonWriter);
			JsonWriter->Close();

			mergedInitialState = JsonString_mergedInitialState;
		}
	}

	return mergedInitialState;
}

bool GetJsonValueFromNestedKey(FString NestedKey, TSharedPtr<FJsonObject> JsonObject,const TSharedPtr<FJsonValue>* &Value)
{
	TArray<FString> Keys;
	NestedKey.ParseIntoArray(Keys, TEXT("."), true);

	const FString FinalKey = Keys[Keys.Num() - 1];

	TSharedPtr<FJsonObject> CurrentObject = JsonObject;
	for (const FString& Key : Keys)
	{
		if (!CurrentObject.IsValid() || !CurrentObject->HasField(Key))
		{
			// Handle error here, key not found or object is invalid
			return false;
		}

		TSharedPtr<FJsonValue> JsonValue = CurrentObject->TryGetField(Key);
		if (JsonValue->Type == EJson::Object)
		{
			CurrentObject = JsonValue->AsObject();
		}
		else if(Key.Equals(FinalKey, ESearchCase::CaseSensitive))
		{
			// The last key should have a value type different from an object
			// Handle error or return the value as needed
			Value = &JsonValue;
			return true;
		}
	}

	return false;
}

static void DuplicateJsonArray(const TArray<TSharedPtr<FJsonValue>>& Source, TArray<TSharedPtr<FJsonValue>>& Dest)
{
	for (const TSharedPtr<FJsonValue>& Value : Source)
	{
		Dest.Add(FJsonValue::Duplicate(Value));
	}
}

//bool SetJsonValueFromNestedKey(FString NestedKey, TSharedPtr<FJsonObject>& JsonObject, const TSharedPtr<FJsonValue> value)
//{
//	if (!value.IsValid())
//	{
//		UE_LOG(LogZLCloudPlugin, Display, TEXT("Tried to set an invalid FJsonValue object to key %s"), *NestedKey);
//		return false;
//	}
//
//	TArray<FString> Keys;
//	NestedKey.ParseIntoArray(Keys, TEXT("."), true);
//
//	const FString FinalKey = Keys[Keys.Num() - 1];
//
//	TSharedPtr<FJsonObject> CurrentObject = JsonObject;
//	for (const FString& Key : Keys)
//	{
//		if (!CurrentObject.IsValid())
//		{
//			return false;
//		}
//		else if (!CurrentObject->HasField(Key))
//		{
//			//Need to create a new FJsonObject level
//			if (Key != FinalKey)
//				CurrentObject->SetObjectField(Key, MakeShared<FJsonObject>());
//		}
//
//		TSharedPtr<FJsonValue> JsonValue = CurrentObject->TryGetField(Key);
//		if (JsonValue.IsValid() && JsonValue->Type == EJson::Object)
//		{
//			CurrentObject = JsonValue->AsObject();
//		}
//		else if (Key == FinalKey)
//		{
//			// The last key should have a value type different from an object
//			// Handle error or return the value as needed
//			switch (value->Type)
//			{
//			case EJson::Boolean:
//			{
//				bool BoolValue;
//				if (value->TryGetBool(BoolValue))
//				{
//					CurrentObject->SetBoolField(Key, BoolValue);
//				}
//				break;
//			}
//			case EJson::Number:
//			{
//				double NumberValue;
//				if (value->TryGetNumber(NumberValue))
//				{
//					CurrentObject->SetNumberField(Key, NumberValue);
//				}
//				break;
//			}
//			case EJson::String:
//			{
//				FString StringValue;
//				if (value->TryGetString(StringValue))
//				{
//					CurrentObject->SetStringField(Key, StringValue);
//				}
//				break;
//			}
//			case EJson::Array:
//			{
//				const TArray<TSharedPtr<FJsonValue>>* ArrayValue;
//				if (value->TryGetArray(ArrayValue))
//				{
//					TArray<TSharedPtr<FJsonValue>> NewArray;
//					DuplicateJsonArray(*ArrayValue, NewArray);
//
//					CurrentObject->SetArrayField(Key, TArray<TSharedPtr<FJsonValue>>(NewArray));
//				}
//				break;
//			}
//			}
//
//			return true;
//		}
//	}
//
//	return false;
//}

bool SetJsonValueFromNestedKey(FString NestedKey, TSharedPtr<FJsonObject>& JsonObject, FJsonValue* value)
{
	if (value->IsNull())
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("Tried to set an invalid FJsonValue object to key %s"), *NestedKey);
		return false;
	}

	TArray<FString> Keys;
	NestedKey.ParseIntoArray(Keys, TEXT("."), true);

	const FString FinalKey = Keys[Keys.Num() - 1];

	TSharedPtr<FJsonObject> CurrentObject = JsonObject;
	for (const FString& Key : Keys)
	{
		if (!CurrentObject.IsValid())
		{
			return false;
		}
		else if (!CurrentObject->HasField(Key))
		{
			//Need to create a new FJsonObject level
			if (Key != FinalKey)
				CurrentObject->SetObjectField(Key, MakeShared<FJsonObject>());
		}

		TSharedPtr<FJsonValue> JsonValue = CurrentObject->TryGetField(Key);
		if (JsonValue.IsValid() && JsonValue->Type == EJson::Object)
		{
			CurrentObject = JsonValue->AsObject();
		}
		else if (Key.Equals(FinalKey, ESearchCase::CaseSensitive))
		{
			// The last key should have a value type different from an object
			// Handle error or return the value as needed
			switch (value->Type)
			{
				case EJson::Boolean:
				{
					bool BoolValue;
					if (value->TryGetBool(BoolValue))
					{
						CurrentObject->SetBoolField(Key, BoolValue);
					}
					break;
				}
				case EJson::Number:
				{
					double NumberValue;
					if (value->TryGetNumber(NumberValue))
					{
						CurrentObject->SetNumberField(Key, NumberValue);
					}
					break;
				}
				case EJson::String:
				{
					FString StringValue;
					if (value->TryGetString(StringValue))
					{
						CurrentObject->SetStringField(Key, StringValue);
					}
					break;
				}
				case EJson::Array:
				{
					const TArray<TSharedPtr<FJsonValue>>* ArrayValue;
					if (value->TryGetArray(ArrayValue))
					{
						TArray<TSharedPtr<FJsonValue>> NewArray;
						DuplicateJsonArray(*ArrayValue, NewArray);

						CurrentObject->SetArrayField(Key, TArray<TSharedPtr<FJsonValue>>(NewArray));
					}
					break;
				}
			}

			return true;
		}
	}

	return false;
}

bool RemoveNestedKey(FString NestedKey, TSharedPtr<FJsonObject>& JsonObject)
{
	TArray<FString> Keys;
	NestedKey.ParseIntoArray(Keys, TEXT("."), true);

	const FString FinalKey = Keys[Keys.Num() - 1];

	TSharedPtr<FJsonObject> CurrentObject = JsonObject;

	for (const FString& Key : Keys)
	{
		if (!CurrentObject.IsValid() || !CurrentObject->HasField(Key))
		{
			// Handle error here, key not found or object is invalid
			return false;
		}

		TSharedPtr<FJsonValue> JsonValue = CurrentObject->TryGetField(Key);
		if (JsonValue->Type == EJson::Object && Key != FinalKey)
		{
			CurrentObject = JsonValue->AsObject();
		}
		else if (Key.Equals(FinalKey, ESearchCase::CaseSensitive))
		{
			CurrentObject->RemoveField(Key);

			// If the parent object is empty after removing the final key,
			// remove the parent key from the parent JSON object
			if (CurrentObject->Values.Num() == 0)
			{
				FString currentNestedKey = "";
				for (int i = 0; i < Keys.Num() - 1; i++)
				{
					currentNestedKey += (i == 0) ? Keys[i] : "." + Keys[i];
				}
				RemoveNestedKey(currentNestedKey, JsonObject); //Remove empty structs up the FJsonObject chain
			}

			return true;
		}

	}

	return false;
}

TArray<FString> UZLCloudPluginStateManager::CurrentStateCompareDiffs_Keys(TSharedPtr<FJsonObject> ComparisonJsonObject)
{
	TArray<FString> diffKeys;

	TSharedPtr<FJsonObject> DiffJsonObject = CreateDiffJsonObject(JsonObject_currentState, ComparisonJsonObject);

	// Iterate over Json Values
	for (auto currJsonValue = ComparisonJsonObject->Values.CreateConstIterator(); currJsonValue; ++currJsonValue)
	{
		// Get the key name
		const FString Name = (*currJsonValue).Key;

		if (DiffJsonObject->TryGetField(Name))
		{
			diffKeys.Add(Name);
		}
	}

	return diffKeys;
}

TSharedPtr<FJsonObject> UZLCloudPluginStateManager::CurrentStateCompareDiffs(TSharedPtr<FJsonObject> ComparisonJsonObject)
{
	return CreateDiffJsonObject(JsonObject_currentState, ComparisonJsonObject);
}

void UZLCloudPluginStateManager::SetNotifyServerState(TSharedPtr<FJsonObject> ComparisonStateRequest)
{
	m_needServerNotify = true;
	m_serverStateNotifyStart = FApp::GetCurrentTime();
	JsonObject_serverNotifyState = ComparisonStateRequest;
	m_lastStateWarningPrintTime = m_serverStateNotifyStart;
}

int32 UZLCloudPluginStateManager::CountLeavesInJsonObject(TSharedPtr<FJsonObject> JsonObject)
{
	if (!JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid JSON object"));
		return 0;
	}

	int32 LeafCount = 0;

	for (auto JsonPair : JsonObject->Values)
	{
		const FString& Key = JsonPair.Key;
		const TSharedPtr<FJsonValue>& Value = JsonPair.Value;

		if (Value.IsValid())
		{
			if (Value->Type == EJson::String || Value->Type == EJson::Number || Value->Type == EJson::Boolean || Value->Type == EJson::Array)
			{
				LeafCount++;
			}
			else if (Value->Type == EJson::Object)
			{
				LeafCount += CountLeavesInJsonObject(Value->AsObject());
			}
		}
	}

	return LeafCount;
}

static FString s_LogSchemaData = "ZEROLIGHT_GET_SCHEMA_DATA";

void UZLCloudPluginStateManager::ProcessState(FString jsonString, bool doCurrentStateCompare, bool& Success)
{
	UE_LOG(LogZLCloudPlugin, Display, TEXT("Received State request to process: %s"), *jsonString);

	if (jsonString.Contains(s_LogSchemaData) && currentSchemaAsset != nullptr)
	{
		TSharedPtr<FJsonObject> JsonSchemaData = currentSchemaAsset->SerializeStateKeyAssetToJson();

		FString JsonString_forWeb;
		TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_forWeb, 1);
		FJsonSerializer::Serialize(JsonSchemaData.ToSharedRef(), JsonWriter);
		JsonWriter->Close();

		IZLCloudPluginModule* Module = ZLCloudPlugin::FZLCloudPluginModule::GetModule();
		if (Module)
		{
			Module->SendData(JsonString_forWeb);
			return;
		}
	}

	//If currently processing an existing state request
	//add this request to the queue, when the previous one finishes/times out
	//broadcast the OnRecieveData from Update to pop from queue into processing
	if (IsProcessingStateRequest()) 
	{
		FString inFlightRequestId = JsonObject_processingState->GetStringField(s_requestIdStr);
		UE_LOG(LogZLCloudPlugin, Display, TEXT("ProcessState queueing request while waiting for RequestId %s to finish..."), *inFlightRequestId);

		TSharedPtr<FJsonObject> JsonObject_requestedState;
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(jsonString);
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject_requestedState))
		{
			JsonString_in_requestQueue.Enqueue(jsonString);
			s_requestQueueLength++;

			//Send to Web
			IZLCloudPluginModule* Module = ZLCloudPlugin::FZLCloudPluginModule::GetModule();
			if (Module)
			{
				TSharedPtr<FJsonObject> jsonForWebObject = MakeShareable(new FJsonObject);
				jsonForWebObject->SetObjectField("state_queued", JsonObject_requestedState);

				//For some reason TQueue doesnt have a length member func, use a counter for queue length
				jsonForWebObject->SetNumberField("queue_length", s_requestQueueLength);

				FString JsonString_forWeb;
				TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_forWeb, 1);
				FJsonSerializer::Serialize(jsonForWebObject.ToSharedRef(), JsonWriter);
				JsonWriter->Close();

				Module->SendData(JsonString_forWeb);
			}
		}

		Success = false;
	}
	else
	{
		//Clear JsonObject_in_requestedState
		JsonObject_in_requestedState.Reset();
		JsonObject_in_requestedState = MakeShareable(new FJsonObject);

		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(jsonString);
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject_in_requestedState))
		{
			//Debug Value
			FString requestId = FString::FromInt(request_recieved_id++);
			JsonObject_in_requestedState->SetStringField(s_requestIdStr, requestId);

			//Reset counters for new request state
			JsonObject_processingStateStartTime = FApp::GetCurrentTime();
			JsonObject_processingStateLeafCount = 0;
			JsonObject_processingStateFinishedLeaves = 0;
			m_lastStateWarningPrintTime = JsonObject_processingStateStartTime;

			if (doCurrentStateCompare)
			{
				//Compare to current state
				JsonObject_out_requestedState = CreateDiffJsonObject(JsonObject_currentState, JsonObject_in_requestedState);
			}
			else
			{
				JsonObject_out_requestedState = JsonObject_in_requestedState;
			}

			//Remove any that are still being processed
			JsonObject_out_requestedState = CreateDiffJsonObject(JsonObject_processingState, JsonObject_out_requestedState);

			JsonObject_requestedStateLeafCount = CountLeavesInJsonObject(JsonObject_out_requestedState) - 1; //Ignore RequestId

			if (doCurrentStateCompare)
			{
				UE_LOG(LogZLCloudPlugin, Display, TEXT("Leaf count difference between request and current state: %d"), JsonObject_requestedStateLeafCount);
			}
			else
			{
				UE_LOG(LogZLCloudPlugin, Display, TEXT("Leaf count in request: %d"), JsonObject_requestedStateLeafCount);
			}
			

			//Send to Web
			IZLCloudPluginModule* Module = ZLCloudPlugin::FZLCloudPluginModule::GetModule();
			if (Module)
			{
				if (JsonObject_requestedStateLeafCount == 0)
				{
					bool stateRequestedContentJob = false;
					TSharedPtr<ZLCloudPlugin::ZLScreenshot> screenshotManager = ZLCloudPlugin::ZLScreenshot::Get();
					if (screenshotManager)
					{
						stateRequestedContentJob = screenshotManager->HasCurrentRender();
					}

					//Return current state to the web
					TSharedPtr<FJsonObject> currentJson = MakeShareable(new FJsonObject);
					currentJson->SetStringField("status", "complete");
					currentJson->SetObjectField("current_state", JsonObject_currentState);

					TSharedPtr<FJsonObject> jsonForWebObject = MakeShareable(new FJsonObject);
					jsonForWebObject->SetObjectField("state_processing_ended", currentJson);
					jsonForWebObject->SetStringField(s_requestIdStr, requestId); //add in request Id	

					FString JsonString_forWeb;
					TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_forWeb, 1);
					FJsonSerializer::Serialize(jsonForWebObject.ToSharedRef(), JsonWriter);
					JsonWriter->Close();

					Module->SendData(JsonString_forWeb);

					if (stateRequestedContentJob)
					{
						screenshotManager->SetCurrentRenderStateData(JsonObject_currentState);
						screenshotManager->UpdateCurrentRenderStateRequestProgress(true, true);
					}

					ClearProcessingState();

					PopStateRequestQueue(); //Process next request if any queued
				}
				else
				{
					TSharedPtr<FJsonObject> jsonForWebObject = MakeShareable(new FJsonObject);
					TSharedPtr<FJsonObject> requestedJsonObject = MakeShareable(new FJsonObject);
					FJsonObject::Duplicate(JsonObject_out_requestedState, requestedJsonObject);
					requestedJsonObject->RemoveField(s_requestIdStr);

					jsonForWebObject->SetStringField(s_requestIdStr, requestId);
					jsonForWebObject->SetObjectField("state_processing", requestedJsonObject);

					//jsonForWebObject->SetNumberField("total_leaves", JsonObject_requestedStateLeafCount);

					FString JsonString_forWeb;
					TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_forWeb, 1);
					FJsonSerializer::Serialize(jsonForWebObject.ToSharedRef(), JsonWriter);
					JsonWriter->Close();

					Module->SendData(JsonString_forWeb);
				}
			}

			Success = JsonObject_out_requestedState.IsValid();

			//Trigger tracked states to see if any pull out state data
			if (UZLTrackedStateBlueprint* stateTrackInstance = UZLTrackedStateBlueprint::GetZLTrackedStateInstance())
			{
				stateTrackInstance->OnTrackedStateUpdate.Broadcast();
			}
		}
		else
		{
			Success = false;
		}
	}
}

bool UZLCloudPluginStateManager::IsProcessingStateRequest()
{
	//If processing object is valid and has a requestid then there is an in-flight state request
	return JsonObject_processingState.IsValid() && JsonObject_processingState->HasField(s_requestIdStr);
}

void UZLCloudPluginStateManager::PopStateRequestQueue()
{
	FString stateDataStr;
	if (JsonString_in_requestQueue.Dequeue(stateDataStr))
	{
		s_requestQueueLength--;
		if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
		{
			Delegates->OnRecieveData.Broadcast(stateDataStr);
		}
	}
}

void UZLCloudPluginStateManager::Update(LauncherComms* launcherComms)
{
	FString requestId = "";


	if (IsProcessingStateRequest())
	{
		requestId = JsonObject_processingState->GetStringField(s_requestIdStr);

		bool stateRequestedContentJob = false;
		TSharedPtr<ZLCloudPlugin::ZLScreenshot> screenshotManager = ZLCloudPlugin::ZLScreenshot::Get();
		if (screenshotManager)
		{
			stateRequestedContentJob = screenshotManager->HasCurrentRender();
		}

		//All requested states processed, send completion message
		if (JsonObject_requestedStateLeafCount == JsonObject_processingStateFinishedLeaves)
		{
			//Send to Web
			IZLCloudPluginModule* Module = ZLCloudPlugin::FZLCloudPluginModule::GetModule();
			if (Module)
			{
				//Return current state to the web
				TSharedPtr<FJsonObject> currentJson = MakeShareable(new FJsonObject);
				currentJson->SetStringField("status", "complete");
				currentJson->SetObjectField("current_state", JsonObject_currentState);

				TSharedPtr<FJsonObject> jsonForWebObject = MakeShareable(new FJsonObject);
				jsonForWebObject->SetObjectField("state_processing_ended", currentJson);
				jsonForWebObject->SetStringField(s_requestIdStr, requestId); //add in request Id	

				FString JsonString_forWeb;
				TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_forWeb, 1);
				FJsonSerializer::Serialize(jsonForWebObject.ToSharedRef(), JsonWriter);
				JsonWriter->Close();

				Module->SendData(JsonString_forWeb);

				if (stateRequestedContentJob)
				{
					screenshotManager->SetCurrentRenderStateData(JsonObject_currentState);
					screenshotManager->UpdateCurrentRenderStateRequestProgress(true, true);
				}

				ClearProcessingState();

				PopStateRequestQueue(); //Process next request if any queued
			}
		}
		else 
		{
			if (JsonObject_processingState->Values.Num() > 1) //Still waiting on something in-processing
			{
				//State warning prints + timeout sent to web
				double currTime = FApp::GetCurrentTime();
				double elapsedTime = currTime - JsonObject_processingStateStartTime;

				if (elapsedTime > m_stateRequestWarningTime && elapsedTime < m_stateRequestTimeout)
				{
					if ((currTime - m_lastStateWarningPrintTime) > 1.0) //Only print every 1 sec
					{
						TArray<FString> diffKeys = CurrentStateCompareDiffs_Keys(JsonObject_processingState);
						if (diffKeys.Contains(s_requestIdStr)) //Dont need to log this
							diffKeys.Remove(s_requestIdStr);

						UE_LOG(LogZLCloudPlugin, Display, TEXT("State request %s still waiting for %d state objects to match..."), *requestId, diffKeys.Num());
						for (FString key : diffKeys)
						{
							UE_LOG(LogZLCloudPlugin, Display, TEXT("Request waiting for %f on %s state"), round(elapsedTime), *key);
						}
						m_lastStateWarningPrintTime = currTime;
					}
				}
				else if (elapsedTime > m_stateRequestTimeout)
				{
					JsonObject_processingState->RemoveField(s_requestIdStr); //Remove this request

					//Send to Web
					IZLCloudPluginModule* Module = ZLCloudPlugin::FZLCloudPluginModule::GetModule();
					if (Module)
					{
						//Return current state + unprocessed to the web
						TSharedPtr<FJsonObject> currentJson = MakeShareable(new FJsonObject);
						TSharedPtr<FJsonObject> timeoutState = CurrentStateCompareDiffs(JsonObject_processingState);

						currentJson->SetStringField("status", "timeout");
						currentJson->SetObjectField("current_state", JsonObject_currentState);
						currentJson->SetObjectField("timeout_state", timeoutState);

						TSharedPtr<FJsonObject> unprocessed = nullptr;
						if (JsonObject_processingStateLeafCount < JsonObject_requestedStateLeafCount) //Some requested states were ignored
						{
							unprocessed = CurrentStateCompareDiffs(JsonObject_out_requestedState);
							TArray<FString> diffKeys = CurrentStateCompareDiffs_Keys(JsonObject_processingState);
							for (FString key : diffKeys) //Strip timed out values to report any unprocessed
							{
								if (unprocessed->TryGetField(key))
								{
									unprocessed->RemoveField(key);
								}
							}
							if (unprocessed->HasField(s_requestIdStr))
								unprocessed->RemoveField(s_requestIdStr);

							currentJson->SetObjectField("unprocessed_state", unprocessed);
						}

						TSharedPtr<FJsonObject> jsonForWebObject = MakeShareable(new FJsonObject);
						jsonForWebObject->SetObjectField("state_processing_ended", currentJson);
						jsonForWebObject->SetStringField(s_requestIdStr, requestId); //add in request Id	

						FString JsonString_forWeb;
						TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_forWeb, 1);
						FJsonSerializer::Serialize(jsonForWebObject.ToSharedRef(), JsonWriter);
						JsonWriter->Close();

						Module->SendData(JsonString_forWeb);

						if (stateRequestedContentJob)
						{
							screenshotManager->SetCurrentRenderStateData(JsonObject_currentState, timeoutState, unprocessed);
							screenshotManager->UpdateCurrentRenderStateRequestProgress(true, false);
						}

						ClearProcessingState();

						PopStateRequestQueue();
					}
				}
			}
			else if(JsonObject_processingStateLeafCount < JsonObject_requestedStateLeafCount) //Some requested states were ignored
			{
				JsonObject_processingState->RemoveField(s_requestIdStr); //Remove this request

				//Send to Web
				IZLCloudPluginModule* Module = ZLCloudPlugin::FZLCloudPluginModule::GetModule();
				if (Module)
				{
					//Return current state + unprocessed to the web
					TSharedPtr<FJsonObject> currentJson = MakeShareable(new FJsonObject);
					TSharedPtr<FJsonObject> unprocessedJson = CurrentStateCompareDiffs(JsonObject_out_requestedState);
					if (unprocessedJson->HasField(s_requestIdStr))
						unprocessedJson->RemoveField(s_requestIdStr);
	
					currentJson->SetStringField("status", "unmatched");
					currentJson->SetObjectField("current_state", JsonObject_currentState);
					currentJson->SetObjectField("unprocessed_state", unprocessedJson);

					TSharedPtr<FJsonObject> jsonForWebObject = MakeShareable(new FJsonObject);
					jsonForWebObject->SetObjectField("state_processing_ended", currentJson);
					jsonForWebObject->SetStringField(s_requestIdStr, requestId);

					FString JsonString_forWeb;
					TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_forWeb, 1);
					FJsonSerializer::Serialize(jsonForWebObject.ToSharedRef(), JsonWriter);
					JsonWriter->Close();

					Module->SendData(JsonString_forWeb);

					if (stateRequestedContentJob)
					{
						screenshotManager->SetCurrentRenderStateData(JsonObject_currentState, nullptr, unprocessedJson);
						screenshotManager->UpdateCurrentRenderStateRequestProgress(true, false);
					}

					ClearProcessingState();

					PopStateRequestQueue();
				}
			}
		}
	}
	else if(JsonObject_requestedStateLeafCount != JsonObject_processingStateFinishedLeaves) //Also need to acocunt for any requests that never entered processing due to invalid fields or instant confirms but were incomplete
	{
		bool stateRequestedContentJob = false;
		TSharedPtr<ZLCloudPlugin::ZLScreenshot> screenshotManager = ZLCloudPlugin::ZLScreenshot::Get();
		if (screenshotManager)
		{
			stateRequestedContentJob = screenshotManager->HasCurrentRender();
		}

		requestId = JsonObject_out_requestedState->GetStringField(s_requestIdStr);
		//Send to Web
		IZLCloudPluginModule* Module = ZLCloudPlugin::FZLCloudPluginModule::GetModule();
		if (Module)
		{
			//Return current state + unprocessed to the web
			TSharedPtr<FJsonObject> currentJson = MakeShareable(new FJsonObject);
			TSharedPtr<FJsonObject> unprocessedJson = CurrentStateCompareDiffs(JsonObject_out_requestedState);
			if (unprocessedJson->HasField(s_requestIdStr))
				unprocessedJson->RemoveField(s_requestIdStr);

			currentJson->SetStringField("status", "unmatched");
			currentJson->SetObjectField("current_state", JsonObject_currentState);
			currentJson->SetObjectField("unprocessed_state", unprocessedJson);

			TSharedPtr<FJsonObject> jsonForWebObject = MakeShareable(new FJsonObject);
			jsonForWebObject->SetObjectField("state_processing_ended", currentJson);
			jsonForWebObject->SetStringField(s_requestIdStr, requestId);

			FString JsonString_forWeb;
			TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_forWeb, 1);
			FJsonSerializer::Serialize(jsonForWebObject.ToSharedRef(), JsonWriter);
			JsonWriter->Close();

			Module->SendData(JsonString_forWeb);

			if (stateRequestedContentJob)
			{
				screenshotManager->SetCurrentRenderStateData(JsonObject_currentState, nullptr, unprocessedJson);
				screenshotManager->UpdateCurrentRenderStateRequestProgress(true, false);
			}

			ClearProcessingState();

			PopStateRequestQueue();
		}
	}

	if (m_needServerNotify && JsonObject_serverNotifyState.IsValid())
	{
		if (CurrentStateCompareDiffs_Keys(JsonObject_serverNotifyState).Num() == 0) //State matches current render state data
		{
			//Send server notify and clear
			m_needServerNotify = false;
			launcherComms->SendLauncherMessage("APPINITIALSTATESET"); //onConnect is ready, allow adoption
		}
		else
		{
			double currTime = FApp::GetCurrentTime();
			double elapsedTime = currTime - m_serverStateNotifyStart;

			if (elapsedTime > m_stateRequestWarningTime && elapsedTime < m_stateRequestTimeout)
			{
				if ((currTime - m_lastStateWarningPrintTime) > 1.0) //Only print every 1 sec
				{
					TArray<FString> diffKeys = CurrentStateCompareDiffs_Keys(JsonObject_serverNotifyState);
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Connection waiting on %d state objects to match..."), diffKeys.Num());
					for (FString key : diffKeys)
					{
						UE_LOG(LogZLCloudPlugin, Display, TEXT("Connection waiting for %f on %s state"), round(elapsedTime), *key);
					}
					m_lastStateWarningPrintTime = currTime;
				}
			}
			else if (elapsedTime > m_stateRequestTimeout)
			{
				TSharedPtr<FJsonObject> responseData = MakeShareable(new FJsonObject);
				responseData->SetStringField("error", "Timeout reached waiting for state data to match onConnect request.");

				FString responseDataStr;

				TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&responseDataStr);

				FJsonSerializer::Serialize(responseData.ToSharedRef(), writer);

				launcherComms->SendLauncherMessage("STATE_ERROR", responseDataStr);

				m_needServerNotify = false;
			}

		}
	}

}


void UZLCloudPluginStateManager::ConfirmStateChange(FString FieldName, bool& Success)
{
	Success = false;

	//Set no longer processing
	if (JsonObject_processingState.IsValid())
	{
		if (FieldName.Contains("."))
		{
			// . delimited nesting (this is mainly because BP does not handle generic types like FJsonValue or FJsonObject)
			const TSharedPtr<FJsonValue>* nestedValue;
			Success = GetJsonValueFromNestedKey(FieldName, JsonObject_processingState, nestedValue);

			if (Success)
			{
				SetJsonValueFromNestedKey(FieldName, JsonObject_currentState, nestedValue->Get());

				JsonObject_processingStateFinishedLeaves++;

				//Remove from processing
				RemoveNestedKey(FieldName, JsonObject_processingState);
			}
			else
				return;
		}
		else if (JsonObject_processingState->HasField(FieldName))
		{
			TSharedPtr<FJsonValue> value = JsonObject_processingState->TryGetField(FieldName);

			if(value != nullptr)
				Success = true;

			switch (value->Type)
			{
				case EJson::String:
					//Update current state
					JsonObject_currentState->SetStringField(FieldName, value->AsString());
					break;
				case EJson::Boolean:
					//Update current state
					JsonObject_currentState->SetBoolField(FieldName, value->AsBool());
					break;
				case EJson::Number:
					//Update current state
					JsonObject_currentState->SetNumberField(FieldName, value->AsNumber());
					break;
				case EJson::Array:
					JsonObject_currentState->SetArrayField(FieldName, value->AsArray());
					break;
				default:
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Unhandled confirmation for EJson type %i"), value->Type);
					break;
			}	

			JsonObject_processingStateFinishedLeaves++;

			//Remove from processing
			JsonObject_processingState->RemoveField(FieldName);
		}
		else
		{
			Success = false;
		}
	}
	else
	{
		Success = false;
	}
}

void UZLCloudPluginStateManager::RemoveCurrentStateValue(FString FieldName)
{
	if (FieldName.Contains("."))
	{
		// . delimited nesting (this is mainly because BP does not handle generic types like FJsonValue or FJsonObject)
		RemoveNestedKey(FieldName, JsonObject_currentState);
	}
	else if (JsonObject_currentState->HasField(FieldName))
	{
		JsonObject_currentState->RemoveField(FieldName);
	}
}

void UZLCloudPluginStateManager::CopyRequestId()
{
	//Copy request Id
	FString requestId = "";
	if (JsonObject_out_requestedState->TryGetStringField(s_requestIdStr, requestId))
	{
		JsonObject_processingState->SetStringField(s_requestIdStr, requestId);
	}
}


template <typename T> void UZLCloudPluginStateManager::GetRequestedStateValue(FString FieldName, bool instantConfirm, T& data, bool& Success)
{
	Success = false;

	constexpr bool isArray = (std::is_same<T, TArray<double>>::value)
						|| (std::is_same<T, TArray<float>>::value)
						|| (std::is_same<T, TArray<bool>>::value)
						|| (std::is_same<T, TArray<FString>>::value);

	constexpr bool isNumber = (std::is_same<T, double>::value)
						|| (std::is_same<T, float>::value)
						|| (std::is_same<T, TArray<double>>::value)
						|| (std::is_same<T, TArray<float>>::value);

	constexpr bool isDoublePrecision = (std::is_same<T, double>::value)
										|| (std::is_same<T, TArray<double>>::value);

	constexpr bool isBool = (std::is_same<T, bool>::value)
						|| (std::is_same<T, TArray<bool>>::value);

	constexpr bool isJsonValue = (std::is_same<T, TSharedPtr<FJsonValue>>::value);

	constexpr bool isString = (std::is_same<T, FString>::value)
						|| (std::is_same<T, TArray<FString>>::value);

	if (JsonObject_out_requestedState.IsValid())
	{
		int numLeavesInc = 0;
		if (FieldName.Contains("."))
		{
			// . delimited nesting (this is mainly because BP does not handle generic types like FJsonValue or FJsonObject)
			const TSharedPtr<FJsonValue>* nestedValue = nullptr;
			Success = GetJsonValueFromNestedKey(FieldName, JsonObject_out_requestedState, nestedValue);

			TSharedPtr<FJsonValue> val;
			if (Success)
			{
				if constexpr (isArray)
				{
					TArray<TSharedPtr<FJsonValue>> StructJsonArray;
					for (TSharedPtr<FJsonValue> var : nestedValue->Get()->AsArray())
					{
						if constexpr (isNumber)
						{
							if constexpr (isDoublePrecision)
								data.Add(var->AsNumber());
							else
								data.Add(static_cast<float>(var->AsNumber()));
							StructJsonArray.Add(MakeShared<FJsonValueNumber>(var->AsNumber()));
							numLeavesInc = 1;
						}
						else if constexpr (isBool)
						{
							data.Add(var->AsBool());
							StructJsonArray.Add(MakeShared<FJsonValueBoolean>(var->AsBool()));
							numLeavesInc = 1;
						}
						else if constexpr (isString)
						{
							data.Add(var->AsString());
							StructJsonArray.Add(MakeShared<FJsonValueString>(var->AsString()));
							numLeavesInc = 1;
						}
					}
					val = MakeShared<FJsonValueArray>(StructJsonArray);
				}
				else
				{
					if constexpr (isNumber)
					{
						data = nestedValue->Get()->AsNumber();
						val = MakeShared<FJsonValueNumber>(data);
						numLeavesInc = 1;
					}
					else if constexpr (isBool)
					{
						data = nestedValue->Get()->AsBool();
						val = MakeShared<FJsonValueBoolean>(data);
						numLeavesInc = 1;
					}
					else if constexpr (isString)
					{
						data = nestedValue->Get()->AsString();
						val = MakeShared<FJsonValueString>(data);
						numLeavesInc = 1;
					}
				}
			}
			else
			{
				return;
			}

			//Increment processing count
			JsonObject_processingStateLeafCount += numLeavesInc;

			//add to processing
			SetJsonValueFromNestedKey(FieldName, JsonObject_processingState, val.Get());

			//remove from requested state
			RemoveNestedKey(FieldName, JsonObject_out_requestedState);

			CopyRequestId();

			if (instantConfirm)
			{
				JsonObject_processingStateFinishedLeaves++;

				//Remove from processing
				RemoveNestedKey(FieldName, JsonObject_processingState);

				//Update current state
				SetJsonValueFromNestedKey(FieldName, JsonObject_currentState, val.Get());
			}
		}
		else if (JsonObject_out_requestedState->HasField(FieldName))
		{
			const TArray<TSharedPtr<FJsonValue>>* ArrayValue;
			if constexpr (isArray)
			{
				Success = JsonObject_out_requestedState->TryGetArrayField(FieldName, ArrayValue);

				for (TSharedPtr<FJsonValue> Value : *ArrayValue)
				{
					if constexpr (isNumber)
					{
						if constexpr (isDoublePrecision)
						{
							data.Add(Value.Get()->AsNumber());
						}
						else
						{
							float val = static_cast<float>(Value.Get()->AsNumber());
							data.Add(val);
						}

						numLeavesInc = 1;
					}
					else if constexpr (isBool)
					{
						data.Add(Value.Get()->AsBool());
						numLeavesInc = 1;
					}
					else if constexpr (isString)
					{
						data.Add(Value.Get()->AsString());
						numLeavesInc = 1;
					}
				}
			}
			else
			{
				if constexpr (isNumber)
				{
					Success = JsonObject_out_requestedState->TryGetNumberField(FieldName, data);
					numLeavesInc = 1;
				}
				else if constexpr (isBool)
				{
					Success = JsonObject_out_requestedState->TryGetBoolField(FieldName, data);
					numLeavesInc = 1;
				}
				else if constexpr (isString)
				{
					Success = JsonObject_out_requestedState->TryGetStringField(FieldName, data);
					numLeavesInc = 1;
				}
				else if constexpr (isJsonValue)
				{
					const TSharedPtr<FJsonObject>* jsonObject = nullptr;
					Success = JsonObject_out_requestedState->TryGetObjectField(FieldName, jsonObject);					
					if (Success)
					{
						numLeavesInc = CountLeavesInJsonObject(*jsonObject);
						data = JsonObject_out_requestedState->TryGetField(FieldName);						
					}
				}
			}

			JsonObject_processingStateLeafCount += numLeavesInc;

			//add to processing
			if constexpr (isArray)
			{
				JsonObject_processingState->SetArrayField(FieldName, *ArrayValue);
			}
			else if constexpr (isNumber)
			{
				JsonObject_processingState->SetNumberField(FieldName, data);
			}
			else if constexpr (isBool)
			{
				JsonObject_processingState->SetBoolField(FieldName, data);
			}
			else if constexpr (isString)
			{
				JsonObject_processingState->SetStringField(FieldName, data);
			}
			else if constexpr (isJsonValue)
			{
				JsonObject_processingState->SetField(FieldName, data);
			}


			//remove from requested state
			JsonObject_out_requestedState->RemoveField(FieldName);

			CopyRequestId();

			if (instantConfirm)
			{
				JsonObject_processingStateFinishedLeaves += numLeavesInc;

				//Remove from processing
				JsonObject_processingState->RemoveField(FieldName);

				//Update current state
				if constexpr (isArray)
				{
					JsonObject_currentState->SetArrayField(FieldName, *ArrayValue);
				}
				else if constexpr (isNumber)
				{
					JsonObject_currentState->SetNumberField(FieldName, data);
				}
				else if constexpr (isBool)
				{
					JsonObject_currentState->SetBoolField(FieldName, data);
				}
				else if constexpr (isString)
				{
					JsonObject_currentState->SetStringField(FieldName, data);
				}
				else if constexpr (isJsonValue)
				{
					JsonObject_currentState->SetField(FieldName, data);
				}
			}
		}
	}
}

template <typename T> void UZLCloudPluginStateManager::GetCurrentStateValue(FString FieldName, T& data, bool& Success)
{
	Success = false;

	constexpr bool isArray = (std::is_same<T, TArray<double>>::value)
		|| (std::is_same<T, TArray<bool>>::value)
		|| (std::is_same<T, TArray<FString>>::value);

	constexpr bool isNumber = (std::is_same<T, double>::value)
		|| (std::is_same<T, TArray<double>>::value);

	constexpr bool isBool = (std::is_same<T, bool>::value)
		|| (std::is_same<T, TArray<bool>>::value);

	constexpr bool isJsonValue = (std::is_same<T, TSharedPtr<FJsonValue>>::value);

	constexpr bool isString = (std::is_same<T, FString>::value)
		|| (std::is_same<T, TArray<FString>>::value);

	if (JsonObject_currentState.IsValid())
	{
		if (FieldName.Contains("."))
		{
			// . delimited nesting (this is mainly because BP does not handle generic types like FJsonValue or FJsonObject)
			const TSharedPtr<FJsonValue>* nestedValue = nullptr;
			Success = GetJsonValueFromNestedKey(FieldName, JsonObject_currentState, nestedValue);

			TSharedPtr<FJsonValue> val;
			if (Success)
			{
				if constexpr (isArray)
				{
					TArray<TSharedPtr<FJsonValue>> StructJsonArray;
					for (TSharedPtr<FJsonValue> var : nestedValue->Get()->AsArray())
					{
						if constexpr (isNumber)
						{
							data.Add(var->AsNumber());
							StructJsonArray.Add(MakeShared<FJsonValueNumber>(var->AsNumber()));
						}
						else if constexpr (isBool)
						{
							data.Add(var->AsBool());
							StructJsonArray.Add(MakeShared<FJsonValueBoolean>(var->AsBool()));
						}
						else if constexpr (isString)
						{
							data.Add(var->AsString());
							StructJsonArray.Add(MakeShared<FJsonValueString>(var->AsString()));
						}
					}
					val = MakeShared<FJsonValueArray>(StructJsonArray);
				}
				else
				{
					if constexpr (isNumber)
					{
						data = nestedValue->Get()->AsNumber();
						val = MakeShared<FJsonValueNumber>(data);
					}
					else if constexpr (isBool)
					{
						data = nestedValue->Get()->AsBool();
						val = MakeShared<FJsonValueBoolean>(data);
					}
					else if constexpr (isString)
					{
						data = nestedValue->Get()->AsString();
						val = MakeShared<FJsonValueString>(data);
					}
				}
			}
			else
			{
				return;
			}
		}
		else if (JsonObject_currentState->HasField(FieldName))
		{
			const TArray<TSharedPtr<FJsonValue>>* ArrayValue;
			if constexpr (isArray)
			{
				Success = JsonObject_currentState->TryGetArrayField(FieldName, ArrayValue);

				for (const TSharedPtr<FJsonValue>& Value : *ArrayValue)
				{
					if constexpr (isNumber)
					{
						data.Add(Value.Get()->AsNumber());
					}
					else if constexpr (isBool)
					{
						data.Add(Value.Get()->AsBool());
					}
					else if constexpr (isString)
					{
						data.Add(Value.Get()->AsString());
					}
				}
			}
			else
			{
				if constexpr (isNumber)
				{
					Success = JsonObject_currentState->TryGetNumberField(FieldName, data);
				}
				else if constexpr (isBool)
				{
					Success = JsonObject_currentState->TryGetBoolField(FieldName, data);
				}
				else if constexpr (isString)
				{
					Success = JsonObject_currentState->TryGetStringField(FieldName, data);
				}
				else if constexpr (isJsonValue)
				{
					const TSharedPtr<FJsonObject>* jsonObject = nullptr;
					Success = JsonObject_currentState->TryGetObjectField(FieldName, jsonObject);
					if (Success)
					{
						data = JsonObject_currentState->TryGetField(FieldName);
					}
				}
			}
		}
	}
}

void UZLCloudPluginStateManager::SetCurrentSchema(UStateKeyInfoAsset* Asset)
{
	if (Asset != nullptr)
	{
		currentSchemaAsset = Asset;
		RebuildDebugUI(currentSchemaAsset);
	}
}

void UZLCloudPluginStateManager::GetCurrentStateValue_String(FString FieldName, FString& StringValue, bool& Success)
{
	Success = false;

	if (JsonObject_currentState.IsValid())
	{
		Success = JsonObject_currentState->TryGetStringField(FieldName, StringValue);
	}
}

void UZLCloudPluginStateManager::GetCurrentState(FString& jsonString)
{
	TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&jsonString, 1);

	FJsonSerializer::Serialize(JsonObject_currentState.ToSharedRef(), JsonWriter);

	JsonWriter->Close();
}

void UZLCloudPluginStateManager::SaveCurrentStateToFile(FString fileName)
{
	FString JsonString_currentState;
	{
		TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_currentState);
		FJsonSerializer::Serialize(JsonObject_currentState.ToSharedRef(), JsonWriter);
		JsonWriter->Close();
	}

	FString FullFilePath = FPaths::ConvertRelativePathToFull(fileName);

	if (FFileHelper::SaveStringToFile(JsonString_currentState, *FullFilePath))
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("JSON data saved to file: %s"), *FullFilePath);
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Failed to save JSON data to file: %s"), *FullFilePath);
	}
}

void UZLCloudPluginStateManager::MergeTrackedStateIntoCurrentState(const FString& FieldName, TSharedPtr<FJsonObject> JsonObject)
{
	JsonObject_currentState->SetObjectField(FieldName, JsonObject);
}


void UZLCloudPluginStateManager::SendCurrentStateToWeb(bool completeState)
{
	if (!completeState)
	{
		//Calculate changes since last update
		JsonObject_web_currentState = CreateDiffJsonObject(JsonObject_web_currentState, JsonObject_currentState);
	}

	//Send to Web
	IZLCloudPluginModule* Module = ZLCloudPlugin::FZLCloudPluginModule::GetModule();
	if (Module)
	{
		//Return current state to the web
		TSharedPtr<FJsonObject> currentStateJson = MakeShareable(new FJsonObject);
		currentStateJson->SetStringField("status", "complete");
		currentStateJson->SetObjectField("current_state", (completeState) ? JsonObject_currentState : JsonObject_web_currentState);

		TSharedPtr<FJsonObject> jsonForWebObject = MakeShareable(new FJsonObject);
		jsonForWebObject->SetObjectField("state_notify_web", currentStateJson);

		FString JsonString_forWeb;
		TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_forWeb, 1);
		FJsonSerializer::Serialize(jsonForWebObject.ToSharedRef(), JsonWriter);
		JsonWriter->Close();

		Module->SendData(JsonString_forWeb);
	}

	//Copy all data in
	FJsonObject::Duplicate(JsonObject_currentState, JsonObject_web_currentState);
}


void UZLCloudPluginStateManager::DumpState()
{

	FString JsonString_currentState_StateDump;
	{
		TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_currentState_StateDump);
		FJsonSerializer::Serialize(JsonObject_currentState.ToSharedRef(), JsonWriter);
		JsonWriter->Close();
	}

	FString JsonString_web_currentState_StateDump;
	{
		TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_web_currentState_StateDump);
		FJsonSerializer::Serialize(JsonObject_web_currentState.ToSharedRef(), JsonWriter);
		JsonWriter->Close();
	}

	FString JsonString_processingState_StateDump;
	{
		TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_processingState_StateDump);
		FJsonSerializer::Serialize(JsonObject_processingState.ToSharedRef(), JsonWriter);
		JsonWriter->Close();
	}

	FString JsonString_in_requestedState_StateDump;
	{
		TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_in_requestedState_StateDump);
		FJsonSerializer::Serialize(JsonObject_in_requestedState.ToSharedRef(), JsonWriter);
		JsonWriter->Close();
	}

	FString JsonString_out_requestedState_StateDump;
	{
		TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_out_requestedState_StateDump);
		FJsonSerializer::Serialize(JsonObject_out_requestedState.ToSharedRef(), JsonWriter);
		JsonWriter->Close();
	}


	FString FullFilePath = FPaths::ConvertRelativePathToFull("StateDump.txt");

	FString stateDumpString = TEXT("Requested State Change\n-------------------------\n\n") + JsonString_in_requestedState_StateDump;
	stateDumpString += TEXT("\n\nRequested State Change (processed)\n-------------------------\n\n") + JsonString_out_requestedState_StateDump;
	stateDumpString += TEXT("\n\nProcessing State Change\n-------------------------\n\n") + JsonString_processingState_StateDump;
	stateDumpString += TEXT("\n\nCurrent State\n-------------------------\n\n") + JsonString_currentState_StateDump;
	stateDumpString += TEXT("\n\nCurrent Web State\n-------------------------\n\n") + JsonString_web_currentState_StateDump;

	if (FFileHelper::SaveStringToFile(stateDumpString, *FullFilePath))
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("JSON data saved to file: %s"), *FullFilePath);
	}
	else
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Failed to save JSON data to file: %s"), *FullFilePath);
	}
}
