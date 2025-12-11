// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginStateManager.h"
#include "ZLScreenshot.h"
#include "Interfaces/IPluginManager.h"
#include <Runtime/Core/Public/Misc/FileHelper.h>
#include <ZLTrackedStateBlueprint.h>
#include "EditorZLCloudPluginSettings.h"

#if WITH_EDITOR
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "SlateBasics.h"
#include "LevelEditor.h"
#endif

UZLCloudPluginStateManager* UZLCloudPluginStateManager::Singleton = nullptr;

#if WITH_EDITOR
const FName UZLCloudPluginStateManager::DebugUITabId = FName(TEXT("ZLDebugUITab"));
#endif

template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetRequestedStateValue<FString>(FString, bool, FString&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetRequestedStateValue<bool>(FString, bool, bool&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetRequestedStateValue<double>(FString, bool, double&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetRequestedStateValue<float>(FString, bool, float&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetRequestedStateValue<TArray<FString>>(FString, bool, TArray<FString>&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetRequestedStateValue<TArray<bool>>(FString, bool, TArray<bool>&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetRequestedStateValue<TArray<double>>(FString, bool, TArray<double>&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetRequestedStateValue<TArray<float>>(FString, bool, TArray<float>&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetRequestedStateValue<TSharedPtr<FJsonValue, ESPMode::ThreadSafe>>(FString, bool, TSharedPtr<FJsonValue, ESPMode::ThreadSafe>&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetCurrentStateValue<FString>(FString, FString&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetCurrentStateValue<double>(FString, double&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetCurrentStateValue<bool>(FString, bool&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetCurrentStateValue<TArray<FString>>(FString, TArray<FString>&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetCurrentStateValue<TArray<bool>>(FString, TArray<bool>&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetCurrentStateValue<TArray<double>>(FString, TArray<double>&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::GetCurrentStateValue<TSharedPtr<FJsonValue>>(FString, TSharedPtr<FJsonValue>&, bool&);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::SetCurrentStateValue<FString>(FString, FString, bool);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::SetCurrentStateValue<double>(FString, double, bool);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::SetCurrentStateValue<float>(FString, float, bool);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::SetCurrentStateValue<bool>(FString, bool, bool);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::SetCurrentStateValue<TArray<FString>>(FString, TArray<FString>, bool);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::SetCurrentStateValue<TArray<bool>>(FString, TArray<bool>, bool);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::SetCurrentStateValue<TArray<double>>(FString, TArray<double>, bool);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::SetCurrentStateValue<TArray<float>>(FString, TArray<float>, bool);
template ZLCLOUDPLUGIN_API void UZLCloudPluginStateManager::SetCurrentStateValue<TSharedPtr<FJsonValue>>(FString, TSharedPtr<FJsonValue>, bool);


UZLCloudPluginStateManager* UZLCloudPluginStateManager::CreateInstance()
{
	if (Singleton == nullptr)
	{
		Singleton = NewObject<UZLCloudPluginStateManager>();
		Singleton->AddToRoot();
		Singleton->ActiveSchema = NewObject<UStateKeyInfoAsset>(Singleton);

		const UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();

		if (Settings)
		{
			FString schemaName = Settings->displayName + "_ActiveSchema";
			Singleton->ActiveSchema->Rename(*schemaName);
		}
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
			// Check if we should use editor tab (only in editor)
			bool bUseEditorTab = false;
#if WITH_EDITOR
			bUseEditorTab = m_showDebugUIInEditorTab;
#endif

			if (!DebugUIWidget)
			{
				UZLDebugUIWidget* Widget = CreateWidget<UZLDebugUIWidget>(World, WidgetClass);
				if (Widget)
				{
					DebugUIWidget = Widget;
					m_lastSetSchema = schemaAsset;
					DebugUIWidget->SetTargetSchema(schemaAsset);
					
					// Set visibility based on mode
					// If using editor tab, make it visible by default
					bool bShouldBeVisible = m_debugUIVisible;
#if WITH_EDITOR
					if (bUseEditorTab)
					{
						bShouldBeVisible = true;
						m_debugUIVisible = true;
					}
#endif
					DebugUIWidget->SetVisibility(bShouldBeVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);

#if WITH_EDITOR
					if (bUseEditorTab)
					{
						// Create a shared state for the scale value
						TSharedRef<float> ScaleValue = MakeShared<float>(0.5f);
						TSharedRef<SWidget> WidgetRef = Widget->TakeWidget();
						
						// Register tab spawner (safe to call multiple times)
						FGlobalTabmanager::Get()->RegisterNomadTabSpawner(DebugUITabId, FOnSpawnTab::CreateLambda([this, WidgetRef, ScaleValue](const FSpawnTabArgs& SpawnTabArgs)
						{
							return CreateDebugUITabContent(WidgetRef, ScaleValue);
						}))
						.SetDisplayName(FText::FromString(TEXT("ZL Debug UI")))
						.SetMenuType(ETabSpawnerMenuType::Hidden);

						// Invoke the tab
						DebugUITab = FGlobalTabmanager::Get()->TryInvokeTab(DebugUITabId);
					}
					else
#endif
					{
						// Viewport mode (always used in non-editor builds)
						if (bShouldBeVisible)
						{
							SetupViewportMode();
						}
					}
				}
			}
			else
			{
				// Only update schema if it changed to avoid triggering widget reconstruction
				// SetTargetSchema calls RebuildDebugUI() which can trigger NativeConstruct again
				if (m_lastSetSchema != schemaAsset)
				{
					m_lastSetSchema = schemaAsset;
					DebugUIWidget->SetTargetSchema(schemaAsset);
				}
				
#if WITH_EDITOR
				// If switching modes, we need to move the widget between viewport and editor tab
				if (bUseEditorTab && !DebugUITab.IsValid())
				{
					// Remove from viewport if it was there
					DebugUIWidget->RemoveFromParent();
					
					// Make visible when switching to editor tab
					m_debugUIVisible = true;
					DebugUIWidget->SetVisibility(ESlateVisibility::Visible);
					
					// Create a shared state for the scale value
					TSharedRef<float> ScaleValue = MakeShared<float>(0.5f);
					TSharedRef<SWidget> WidgetRef = DebugUIWidget->TakeWidget();
					
					// Register tab spawner (safe to call multiple times - will update existing spawner)
					FGlobalTabmanager::Get()->RegisterNomadTabSpawner(DebugUITabId, FOnSpawnTab::CreateLambda([this, WidgetRef, ScaleValue](const FSpawnTabArgs& SpawnTabArgs)
					{
						return CreateDebugUITabContent(WidgetRef, ScaleValue);
					}))
					.SetDisplayName(FText::FromString(TEXT("ZL Debug UI")))
					.SetMenuType(ETabSpawnerMenuType::Hidden);
					
					DebugUITab = FGlobalTabmanager::Get()->TryInvokeTab(DebugUITabId);
				}
				else if (!bUseEditorTab && DebugUITab.IsValid())
				{
					// Close tab and add to viewport
					DebugUITab->RequestCloseTab();
					DebugUITab.Reset();
					
					// Remove from parent first to ensure clean state
					DebugUIWidget->RemoveFromParent();
					
					if (m_debugUIVisible)
					{
						SetupViewportMode();
					}
				}
				else if (!bUseEditorTab && !DebugUITab.IsValid())
				{
					// Already in viewport mode, just ensure visibility is correct
					if (m_debugUIVisible)
					{
						// Remove and re-add to ensure it's properly in viewport
						SetupViewportMode();
					}
					else
					{
						DebugUIWidget->RemoveFromParent();
					}
				}
#endif
			}
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

	TSharedPtr<FJsonObject> MergedJsonObject = MakeShared<FJsonObject>(*JsonObject1);

	auto AreJsonValuesEqual = [](const TSharedPtr<FJsonValue>& ValueA, const TSharedPtr<FJsonValue>& ValueB) -> bool
	{
		if (ValueA->Type != ValueB->Type) return false;

		switch (ValueA->Type)
		{
		case EJson::String:
			return ValueA->AsString() == ValueB->AsString();
		case EJson::Number:
			return FMath::IsNearlyEqual(ValueA->AsNumber(), ValueB->AsNumber());
		case EJson::Boolean:
			return ValueA->AsBool() == ValueB->AsBool();
		case EJson::Null:
			return true;
		default:
			FString StringA, StringB;
			TSharedRef<TJsonWriter<>> WriterA = TJsonWriterFactory<>::Create(&StringA);
			TSharedRef<TJsonWriter<>> WriterB = TJsonWriterFactory<>::Create(&StringB);
			FJsonSerializer::Serialize(ValueA.ToSharedRef(), "", WriterA);
			FJsonSerializer::Serialize(ValueB.ToSharedRef(), "", WriterB);
			WriterA->Close();
			WriterB->Close();
			return StringA == StringB;
		}
	};

	for (const auto& Pair : JsonObject2->Values)
	{
		const FString& Key = Pair.Key;
		const TSharedPtr<FJsonValue>& Val2 = Pair.Value;

		if (Val2->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>* SubObject1;
			if (MergedJsonObject->TryGetObjectField(Key, SubObject1))
			{
				MergedJsonObject->SetObjectField(Key, MergeJsonObjectsRecursive(*SubObject1, Val2->AsObject()));
			}
			else
			{
				MergedJsonObject->SetField(Key, Val2);
			}
		}
		else if (Val2->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Array2 = Val2->AsArray();

			TArray<TSharedPtr<FJsonValue>> MergedArray;
			const TArray<TSharedPtr<FJsonValue>>* Array1Ptr;

			if (MergedJsonObject->TryGetArrayField(Key, Array1Ptr))
			{
				MergedArray = *Array1Ptr;
			}

			for (const TSharedPtr<FJsonValue>& Item2 : Array2)
			{
				bool bIsDuplicate = false;

				for (const TSharedPtr<FJsonValue>& Item1 : MergedArray)
				{
					if (AreJsonValuesEqual(Item1, Item2))
					{
						bIsDuplicate = true;
						break;
					}
				}

				if (!bIsDuplicate)
				{
					MergedArray.Add(Item2);
				}
			}

			MergedJsonObject->SetArrayField(Key, MergedArray);

		}
		else
		{
			MergedJsonObject->SetField(Key, Val2);
		}
	}

	return MergedJsonObject;
}


void UZLCloudPluginStateManager::OnMoviePipelineFinishedNotifyZLScreenshot(FMoviePipelineOutputData Results)
{
#if UNREAL_5_3_OR_NEWER
	TSharedPtr<ZLCloudPlugin::ZLScreenshot> screenshotManager = ZLCloudPlugin::ZLScreenshot::Get();
	if (screenshotManager)
	{
		screenshotManager->OnMoviePipelineFinished(Results);
	}
#endif
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


void SanitizeJsonObject(TSharedPtr<FJsonObject> JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	TArray<FString> KeysToRemove;

	for (const auto& Pair : JsonObject->Values)
	{
		const FString& Key = Pair.Key;
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			KeysToRemove.Add(Key);
		}
		else if (Value->Type == EJson::Object)
		{
			SanitizeJsonObject(Value->AsObject());
		}
		else if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& JsonArray = Value->AsArray();
			for (const TSharedPtr<FJsonValue>& ArrayValue : JsonArray)
			{
				if (ArrayValue.IsValid() && ArrayValue->Type == EJson::Object)
				{
					SanitizeJsonObject(ArrayValue->AsObject());
				}
			}
		}
	}

	for (const FString& Key : KeysToRemove)
	{
		JsonObject->RemoveField(Key);
	}
}

static FString s_LogSchemaData = "ZEROLIGHT_GET_SCHEMA_DATA";
static FString s_LogJsonCompliantSchemaData = "GET_JSON_SCHEMA";
static FString s_GetVersion = "ZEROLIGHT_GET_VERSION";

void UZLCloudPluginStateManager::ProcessState(FString jsonString, bool doCurrentStateCompare, bool& Success)
{
	UE_LOG(LogZLCloudPlugin, Display, TEXT("Received State request to process: %s"), *jsonString);

	if ((jsonString.Contains(s_LogJsonCompliantSchemaData) || jsonString.Contains(s_LogSchemaData)) && ActiveSchema)
	{
		TSharedPtr<FJsonObject> JsonSchemaData;

		if (jsonString.Contains(s_LogSchemaData))
			JsonSchemaData = ActiveSchema->SerializeStateKeyAssetToJson();
		else
			JsonSchemaData = ActiveSchema->SerializeStateKeyAsset_JsonSchemaCompliant();

		SendFJsonObjectToWeb(JsonSchemaData);
		return;
	}

	if (jsonString.Contains(s_GetVersion))
	{
		FString pluginVersion = "1.0.0";
		IPluginManager& PluginManager = IPluginManager::Get();
		TSharedPtr<IPlugin> ZlCloudStreamPlugin = PluginManager.FindPlugin("ZLCloudPlugin");
		if (ZlCloudStreamPlugin)
		{
			const FPluginDescriptor& Descriptor = ZlCloudStreamPlugin->GetDescriptor();
			pluginVersion = *Descriptor.VersionName;
		}

		TSharedPtr<FJsonObject> JsonVersionData = MakeShareable(new FJsonObject);
		JsonVersionData->SetStringField("ZLCloudPlugin version", pluginVersion);

		// Broadcast delegate to allow other plugins to add their version info
		if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
		{
			Delegates->OnGetVersionInfoNative.Broadcast(JsonVersionData);
		}

		SendFJsonObjectToWeb(JsonVersionData);
		return;
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
			SanitizeJsonObject(JsonObject_requestedState);
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

				SendFJsonObjectToWeb(jsonForWebObject);
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
			SanitizeJsonObject(JsonObject_in_requestedState);
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

					SendFJsonObjectToWeb(jsonForWebObject);

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

					SendFJsonObjectToWeb(jsonForWebObject);
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
	bool clearProcessing = false;
	bool popStateRequestList = false;

	bool stateRequestedContentJob = false;
	TSharedPtr<ZLCloudPlugin::ZLScreenshot> screenshotManager = ZLCloudPlugin::ZLScreenshot::Get();
	if (screenshotManager)
	{
		stateRequestedContentJob = screenshotManager->HasCurrentRender();
	}

	if (IsProcessingStateRequest())
	{
		requestId = JsonObject_processingState->GetStringField(s_requestIdStr);

		//All requested states processed, send completion message
		if (JsonObject_requestedStateLeafCount == JsonObject_processingStateFinishedLeaves)
		{
			SetStateDirty(EStateDirtyReason::state_processing_ended);

			if (stateRequestedContentJob)
			{
				screenshotManager->SetCurrentRenderStateData(JsonObject_currentState);
				screenshotManager->UpdateCurrentRenderStateRequestProgress(true, true);
			}

			clearProcessing = true;
			popStateRequestList = true;
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
					TSharedPtr<FJsonObject> timeoutState = CurrentStateCompareDiffs(JsonObject_processingState);
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
					}

					if (stateRequestedContentJob)
					{
						screenshotManager->SetCurrentRenderStateData(JsonObject_currentState, timeoutState, unprocessed);
						screenshotManager->UpdateCurrentRenderStateRequestProgress(true, false);
					}

					SetStateDirty(EStateDirtyReason::state_processing_ended);

					clearProcessing = true;
					popStateRequestList = true;
				}
			}
			else if(JsonObject_processingStateLeafCount < JsonObject_requestedStateLeafCount) //Some requested states were ignored
			{
				//Return current state + unprocessed to the web
				TSharedPtr<FJsonObject> unprocessedJson = CurrentStateCompareDiffs(JsonObject_out_requestedState);
				if (unprocessedJson->HasField(s_requestIdStr))
					unprocessedJson->RemoveField(s_requestIdStr);

				FString JsonString_Unprocessed;
				TSharedRef<TJsonWriter<TCHAR>> JsonWriterUnprocessed = TJsonWriterFactory<TCHAR>::Create(&JsonString_Unprocessed, 1);
				FJsonSerializer::Serialize(unprocessedJson.ToSharedRef(), JsonWriterUnprocessed);
				JsonWriterUnprocessed->Close();

				UE_LOG(LogZLCloudPlugin, Display, TEXT("Unprocessed State in previous request: %s"), *JsonString_Unprocessed);

				SetStateDirty(EStateDirtyReason::state_processing_ended);

				if (stateRequestedContentJob)
				{
					screenshotManager->SetCurrentRenderStateData(JsonObject_currentState, nullptr, unprocessedJson);
					screenshotManager->UpdateCurrentRenderStateRequestProgress(true, false);
				}

				clearProcessing = true;
				popStateRequestList = true;
			}
		}
	}
	else if(JsonObject_requestedStateLeafCount != JsonObject_processingStateFinishedLeaves) //Also need to account for any requests that never entered processing due to invalid fields or instant confirms but were incomplete
	{
		requestId = JsonObject_out_requestedState->GetStringField(s_requestIdStr);

		//Return current state + unprocessed to the web
		TSharedPtr<FJsonObject> currentJson = MakeShareable(new FJsonObject);
		TSharedPtr<FJsonObject> unprocessedJson = CurrentStateCompareDiffs(JsonObject_out_requestedState);
		if (unprocessedJson->HasField(s_requestIdStr))
			unprocessedJson->RemoveField(s_requestIdStr);

		SetStateDirty(EStateDirtyReason::state_processing_ended);

		if (stateRequestedContentJob)
		{
			screenshotManager->SetCurrentRenderStateData(JsonObject_currentState, nullptr, unprocessedJson);
			screenshotManager->UpdateCurrentRenderStateRequestProgress(true, false);
		}

		clearProcessing = true;
		popStateRequestList = true;
	}

	if (m_stateDirty)
		PushStateEventsToWeb();

	if(clearProcessing)
		ClearProcessingState();

	if(popStateRequestList)
		PopStateRequestQueue(); //Process next request if any queued

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
				JsonObject_serverNotifyUnmatchedState = CreateDiffJsonObject(JsonObject_currentState, JsonObject_serverNotifyState);

				m_needServerNotify = false;

				launcherComms->SendLauncherMessage("APPINITIALSTATESET"); //onConnect is ready, allow adoption
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

			bool incrementProcessedLeafCount = true;
			int processedLeaves = 0;
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
				case EJson::Object:
					incrementProcessedLeafCount = false;
					processedLeaves = CountLeavesInJsonObject(value->AsObject());
					if (JsonObject_currentState->HasField(FieldName))
					{
						TSharedPtr<FJsonObject> mergedStateToConfirm = MergeJsonObjectsRecursive(JsonObject_currentState->GetObjectField(FieldName), value->AsObject());
						JsonObject_currentState->SetObjectField(FieldName, mergedStateToConfirm);
					}
					else
					{
						JsonObject_currentState->SetObjectField(FieldName, value->AsObject());
					}
					JsonObject_processingStateFinishedLeaves += processedLeaves;
					break;
				default:
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Unhandled confirmation for EJson type %i"), value->Type);
					break;
			}	

			if(incrementProcessedLeafCount)
				JsonObject_processingStateFinishedLeaves++;

			//Remove from processing
			JsonObject_processingState->RemoveField(FieldName);
		}
		else
		{
			Success = false;
		}

		if (Success && DebugUIWidget && DebugUIWidget->IsVisible())
			DebugUIWidget->TriggerRefreshUI();

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

template <typename T> void UZLCloudPluginStateManager::SetCurrentStateValue(FString FieldName, T data, bool SubmitToProcessState)
{
	constexpr bool isArray = (std::is_same<T, TArray<double>>::value)
		|| (std::is_same<T, TArray<bool>>::value)
		|| (std::is_same<T, TArray<FString>>::value);

	constexpr bool isNumber = (std::is_same<T, double>::value)
		|| (std::is_same<T, TArray<double>>::value);

	constexpr bool isBool = (std::is_same<T, bool>::value)
		|| (std::is_same<T, TArray<bool>>::value);

	constexpr bool isString = (std::is_same<T, FString>::value)
		|| (std::is_same<T, TArray<FString>>::value);

	if (SubmitToProcessState)
	{
		TSharedPtr<FJsonObject> broadcastValueJson = MakeShared<FJsonObject>();

		if (FieldName.Contains("."))
		{
			// . delimited nesting (this is mainly because BP does not handle generic types like FJsonValue or FJsonObject)
			TArray<FString> Keys;
			FieldName.ParseIntoArray(Keys, TEXT("."), true);

			const FString FinalKey = Keys[Keys.Num() - 1];

			TSharedPtr<FJsonObject> CurrentObject = broadcastValueJson;
			for (const FString& Key : Keys)
			{
				if (Key.Equals(FinalKey, ESearchCase::CaseSensitive))
				{
					if constexpr (isArray)
					{
						TArray<TSharedPtr<FJsonValue>> ArrayValues;
						ArrayValues.Reserve(data.Num());

						for (const auto& Value : data)
						{
							if constexpr (isNumber)
							{
								ArrayValues.Add(MakeShared<FJsonValueNumber>(Value));
							}
							else if constexpr (isBool)
							{
								ArrayValues.Add(MakeShared<FJsonValueBoolean>(Value));
							}
							else if constexpr (isString)
							{
								ArrayValues.Add(MakeShared<FJsonValueString>(Value));
							}
						}

						CurrentObject->SetArrayField(FinalKey, ArrayValues);
					}
					else
					{
						if constexpr (isNumber)
						{
							CurrentObject->SetNumberField(FinalKey, data);
						}
						else if constexpr (isBool)
						{
							CurrentObject->SetBoolField(FinalKey, data);
						}
						else if constexpr (isString)
						{
							CurrentObject->SetStringField(FinalKey, data);
						}
					}
				}
				else
				{
					TSharedPtr<FJsonObject> SubObj = MakeShared<FJsonObject>();
					CurrentObject->SetObjectField(Key, SubObj);
					CurrentObject = SubObj;
				}
			}

		}
		else
		{
			if constexpr (isArray)
			{
				TArray<TSharedPtr<FJsonValue>> ArrayValues;
				ArrayValues.Reserve(data.Num());

				for (const auto& Value : data)
				{
					if constexpr (isNumber)
					{
						ArrayValues.Add(MakeShared<FJsonValueNumber>(Value));
					}
					else if constexpr (isBool)
					{
						ArrayValues.Add(MakeShared<FJsonValueBoolean>(Value));
					}
					else if constexpr (isString)
					{
						ArrayValues.Add(MakeShared<FJsonValueString>(Value));
					}
				}

				broadcastValueJson->SetArrayField(FieldName, ArrayValues);
			}
			else
			{
				if constexpr (isNumber)
				{
					broadcastValueJson->SetNumberField(FieldName, data);
				}
				else if constexpr (isBool)
				{
					broadcastValueJson->SetBoolField(FieldName, data);
				}
				else if constexpr (isString)
				{
					broadcastValueJson->SetStringField(FieldName, data);
				}
			}
		}

		if (broadcastValueJson->Values.Num() > 0)
		{
			//Broadcast to ProcessState with this
			if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
			{
				FString JsonString_Schema;
				TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_Schema, 1);
				FJsonSerializer::Serialize(broadcastValueJson.ToSharedRef(), JsonWriter);
				JsonWriter->Close();

				UE_LOG(LogZLCloudPlugin, Display, TEXT("Trigger Schema State Set Internal Request"));
				Delegates->OnRecieveData.Broadcast(JsonString_Schema);
			}
		}
	}
	else
	{
		if (FieldName.Contains("."))
		{
			// . delimited nesting (this is mainly because BP does not handle generic types like FJsonValue or FJsonObject)
			TArray<FString> Keys;
			FieldName.ParseIntoArray(Keys, TEXT("."), true);

			const FString FinalKey = Keys[Keys.Num() - 1];

			TSharedPtr<FJsonObject> CurrentObject = JsonObject_currentState;
			for (const FString& Key : Keys)
			{
				if (Key.Equals(FinalKey, ESearchCase::CaseSensitive))
				{
					if constexpr (isArray)
					{
						TArray<TSharedPtr<FJsonValue>> ArrayValues;
						ArrayValues.Reserve(data.Num());

						for (const auto& Value : data)
						{
							if constexpr (isNumber)
							{
								ArrayValues.Add(MakeShared<FJsonValueNumber>(Value));
							}
							else if constexpr (isBool)
							{
								ArrayValues.Add(MakeShared<FJsonValueBoolean>(Value));
							}
							else if constexpr (isString)
							{
								ArrayValues.Add(MakeShared<FJsonValueString>(Value));
							}
						}

						CurrentObject->SetArrayField(FinalKey, ArrayValues);
					}
					else
					{
						if constexpr (isNumber)
						{
							CurrentObject->SetNumberField(FinalKey, data);
						}
						else if constexpr (isBool)
						{
							CurrentObject->SetBoolField(FinalKey, data);
						}
						else if constexpr (isString)
						{
							CurrentObject->SetStringField(FinalKey, data);
						}
					}
				}
				else
				{
					const TSharedPtr<FJsonObject>* SubObj = nullptr;

					if (CurrentObject->TryGetObjectField(Key, SubObj))
					{
						CurrentObject = *SubObj;
					}
					else
					{
						TSharedPtr<FJsonObject> NewSubObj = MakeShared<FJsonObject>();
						CurrentObject->SetObjectField(Key, NewSubObj);
						CurrentObject = NewSubObj;
					}
				}
			}

		}
		else
		{
			if constexpr (isArray)
			{
				TArray<TSharedPtr<FJsonValue>> ArrayValues;
				ArrayValues.Reserve(data.Num());

				for (const auto& Value : data)
				{
					if constexpr (isNumber)
					{
						ArrayValues.Add(MakeShared<FJsonValueNumber>(Value));
					}
					else if constexpr (isBool)
					{
						ArrayValues.Add(MakeShared<FJsonValueBoolean>(Value));
					}
					else if constexpr (isString)
					{
						ArrayValues.Add(MakeShared<FJsonValueString>(Value));
					}
				}

				JsonObject_currentState->SetArrayField(FieldName, ArrayValues);
			}
			else
			{
				if constexpr (isNumber)
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
			}
		}

		SetStateDirty(EStateDirtyReason::state_notify_web);

		RebuildDebugUI(ActiveSchema);
		DebugUIWidget->TriggerRefreshUI();
	}
}



void UZLCloudPluginStateManager::SetCurrentSchema(UStateKeyInfoAsset* Asset)
{
	if (Asset)
	{
		ActiveSchema->KeyInfos = Asset->KeyInfos;

		RebuildDebugUI(Asset);
	}
}

void UZLCloudPluginStateManager::AppendCurrentSchema(UStateKeyInfoAsset* Asset)
{
	if (Asset)
	{
		for (const TPair<FString, FStateKeyInfo>& Entry : Asset->KeyInfos)
		{
			const FString& Key = Entry.Key;
			const FStateKeyInfo& IncomingInfo = Entry.Value;

			FStateKeyInfo* ExistingInfo = ActiveSchema->KeyInfos.Find(Key);

			if (ExistingInfo)
			{
				for (const FString& Val : IncomingInfo.AcceptedStringValues)
				{
					ExistingInfo->AcceptedStringValues.AddUnique(Val);
				}

				for (const double& Val : IncomingInfo.AcceptedNumberValues)
				{
					ExistingInfo->AcceptedNumberValues.AddUnique(Val);
				}
			}
			else
			{
				ActiveSchema->KeyInfos.Add(Key, IncomingInfo);
			}
		}

		RebuildDebugUI(ActiveSchema);
		DebugUIWidget->TriggerRefreshUI();
	}
}

void UZLCloudPluginStateManager::RemoveFromCurrentSchema(UStateKeyInfoAsset* Asset)
{
	if (Asset)
	{
		for (const TPair<FString, FStateKeyInfo>& InputEntry : Asset->KeyInfos)
		{
			const FString& Key = InputEntry.Key;
			const FStateKeyInfo& InputInfo = InputEntry.Value;

			FStateKeyInfo* ActiveInfo = ActiveSchema->KeyInfos.Find(Key);

			if (ActiveInfo)
			{
				for (const FString& Val : InputInfo.AcceptedStringValues)
				{
					ActiveInfo->AcceptedStringValues.Remove(Val);
				}

				for (const double& Val : InputInfo.AcceptedNumberValues)
				{
					ActiveInfo->AcceptedNumberValues.Remove(Val);
				}

				bool bHasRemainingArrayValues = ActiveInfo->AcceptedStringValues.Num() > 0 || ActiveInfo->AcceptedNumberValues.Num() > 0;

				if (!bHasRemainingArrayValues)
					ActiveSchema->KeyInfos.Remove(Key);
			}
		}
		RebuildDebugUI(ActiveSchema);
		DebugUIWidget->TriggerRefreshUI();
	}
}

UStateKeyInfoAsset* UZLCloudPluginStateManager::GetCurrentSchemaAsset()
{
	// With UPROPERTY(), the asset should always be valid if set
	return ActiveSchema;
}

void UZLCloudPluginStateManager::ClearCurrentSchema()
{
	if (ActiveSchema != nullptr)
	{
		ActiveSchema->KeyInfos.Empty();
	}
}

void UZLCloudPluginStateManager::SetDebugUIVisibility(bool visible)
{
	m_debugUIVisible = visible;
	UE_LOG(LogZLCloudPlugin, Display, TEXT("Debug UI Visibility set to: %s"), m_debugUIVisible ? TEXT("Visible") : TEXT("Hidden"));
	
	if (DebugUIWidget)
	{
		DebugUIWidget->SetVisibility(m_debugUIVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		
		// Check if we're in editor tab mode
		bool bUseEditorTab = false;
#if WITH_EDITOR
		bUseEditorTab = m_showDebugUIInEditorTab;
#endif
		
		// If in viewport mode, we need to add/remove from viewport
		if (!bUseEditorTab)
		{
			if (m_debugUIVisible)
			{
				// Ensure widget is in viewport
				if (GEngine)
				{
							// Remove first to ensure clean state
					SetupViewportMode();
				}
			}
			else
			{
				// Remove from viewport and reset input mode
				DebugUIWidget->RemoveFromParent();
			}
		}
	}
}

void UZLCloudPluginStateManager::SetShowDebugUIInEditorTab(bool showInEditorTab)
{
	m_showDebugUIInEditorTab = showInEditorTab;
	
	// If we have a schema and widget exists, switch modes without rebuilding
	// This avoids widget reconstruction which would trigger NativeConstruct again
	if (ActiveSchema && DebugUIWidget)
	{
		// Just switch the display mode without rebuilding
		// RebuildDebugUI will handle the mode switching when called
		// But we need to manually switch modes here to avoid reconstruction
#if WITH_EDITOR
		if (showInEditorTab && !DebugUITab.IsValid())
		{
			// Switch to editor tab mode
			DebugUIWidget->RemoveFromParent();
			m_debugUIVisible = true;
			DebugUIWidget->SetVisibility(ESlateVisibility::Visible);
			
			// Create a shared state for the scale value
			TSharedRef<float> ScaleValue = MakeShared<float>(0.5f);
			TSharedRef<SWidget> WidgetRef = DebugUIWidget->TakeWidget();
			
			// Register tab spawner (safe to call multiple times - will update existing spawner)
			FGlobalTabmanager::Get()->RegisterNomadTabSpawner(DebugUITabId, FOnSpawnTab::CreateLambda([this, WidgetRef, ScaleValue](const FSpawnTabArgs& SpawnTabArgs)
			{
				return CreateDebugUITabContent(WidgetRef, ScaleValue);
			}))
			.SetDisplayName(FText::FromString(TEXT("ZL Debug UI")))
			.SetMenuType(ETabSpawnerMenuType::Hidden);
			
			DebugUITab = FGlobalTabmanager::Get()->TryInvokeTab(DebugUITabId);
		}
		else if (!showInEditorTab && DebugUITab.IsValid())
		{
			// Switch to viewport mode
			DebugUITab->RequestCloseTab();
			DebugUITab.Reset();
			
			DebugUIWidget->RemoveFromParent();
			
			if (m_debugUIVisible)
			{
				if (GEngine)
				{
					SetupViewportMode();
				}
			}
		}
#endif
	}
	else if (ActiveSchema && !DebugUIWidget)
	{
		// Widget doesn't exist yet, so rebuild to create it
		RebuildDebugUI(ActiveSchema);
	}
}

void UZLCloudPluginStateManager::SetupViewportMode()
{
	if (DebugUIWidget)
	{
		DebugUIWidget->RemoveFromParent();
		DebugUIWidget->AddToViewport();
	}
}

#if WITH_EDITOR
TSharedRef<SDockTab> UZLCloudPluginStateManager::CreateDebugUITabContent(TSharedRef<SWidget> WidgetRef, TSharedRef<float> ScaleValue)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(FText::FromString(TEXT("ZL Debug UI")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(4.0f, 4.0f, 4.0f, 2.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Scale:")))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SSpinBox<float>)
					.MinValue(0.1f)
					.MaxValue(3.0f)
					.Value_Lambda([ScaleValue]() { return *ScaleValue; })
					.OnValueChanged_Lambda([ScaleValue](float NewValue) 
					{ 
						*ScaleValue = NewValue;
						// Slate will automatically redraw when the RenderTransform lambda's captured value changes
					})
					.MinDesiredWidth(80.0f)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Horizontal)
				+ SScrollBox::Slot()
				[
					SNew(SScrollBox)
					.Orientation(Orient_Vertical)
					+ SScrollBox::Slot()
					[
						SNew(SBox)
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
						[
							SNew(SBox)
							.RenderTransform_Lambda([ScaleValue]()
							{
								return FSlateRenderTransform(FScale2D(*ScaleValue));
							})
							.RenderTransformPivot(FVector2D(0.0f, 0.0f))
							[
								WidgetRef
							]
						]
					]
				]
			]
		];
}
#endif


UZLCloudPluginStateManager::~UZLCloudPluginStateManager()
{
	// Ensure proper cleanup of asset references
	ClearCurrentSchema();
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


void UZLCloudPluginStateManager::SendCurrentStateToWeb(bool completeState, TSharedPtr<FJsonObject> unmatchedRequestState)
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
		
		if (unmatchedRequestState != nullptr)
		{
			currentStateJson->SetObjectField("unmatched_state", unmatchedRequestState);
		}

		TSharedPtr<FJsonObject> jsonForWebObject = MakeShareable(new FJsonObject);
		jsonForWebObject->SetObjectField("state_processing_ended", currentStateJson);

		FString JsonString_forWeb;
		TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_forWeb, 1);
		FJsonSerializer::Serialize(jsonForWebObject.ToSharedRef(), JsonWriter);
		JsonWriter->Close();

		Module->SendData(JsonString_forWeb);
	}

	//Copy all data in
	FJsonObject::Duplicate(JsonObject_currentState, JsonObject_web_currentState);
}

/// <summary>
/// Sets state flag to notify that a state update should be sent to the web in the next tick
/// </summary>
void UZLCloudPluginStateManager::SetStateDirty(EStateDirtyReason reason)
{
	m_stateDirty = true;
	m_stateDirtyReason = reason;
}

void UZLCloudPluginStateManager::PushStateEventsToWeb()
{
	FString requestId = "";
	m_stateDirty = false;

	TSharedPtr<FJsonObject> currentJson = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> jsonForWebObject = MakeShareable(new FJsonObject);

	if (IsProcessingStateRequest())
	{
		requestId = JsonObject_processingState->GetStringField(s_requestIdStr);

		//All requested states processed, send completion message
		if (JsonObject_requestedStateLeafCount == JsonObject_processingStateFinishedLeaves)
		{
			//Return current state to the web
			currentJson->SetStringField("status", "complete");
			currentJson->SetObjectField("current_state", JsonObject_currentState);
		}
		else
		{
			if (JsonObject_processingState->Values.Num() > 1) //Still waiting on something in-processing
			{
				double currTime = FApp::GetCurrentTime();
				double elapsedTime = currTime - JsonObject_processingStateStartTime;

				if (elapsedTime > m_stateRequestTimeout)
				{
					JsonObject_processingState->RemoveField(s_requestIdStr); //Remove this request

					//Return current state + unprocessed to the web
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
				}
			}
			else if (JsonObject_processingStateLeafCount < JsonObject_requestedStateLeafCount) //Some requested states were ignored
			{
				JsonObject_processingState->RemoveField(s_requestIdStr); //Remove this request

				//Return current state + unprocessed to the web
				TSharedPtr<FJsonObject> unprocessedJson = CurrentStateCompareDiffs(JsonObject_out_requestedState);
				if (unprocessedJson->HasField(s_requestIdStr))
					unprocessedJson->RemoveField(s_requestIdStr);

				currentJson->SetStringField("status", "unmatched");
				currentJson->SetObjectField("current_state", JsonObject_currentState);
				currentJson->SetObjectField("unprocessed_state", unprocessedJson);

				FString JsonString_Unprocessed;
				TSharedRef<TJsonWriter<TCHAR>> JsonWriterUnprocessed = TJsonWriterFactory<TCHAR>::Create(&JsonString_Unprocessed, 1);
				FJsonSerializer::Serialize(unprocessedJson.ToSharedRef(), JsonWriterUnprocessed);
				JsonWriterUnprocessed->Close();

				UE_LOG(LogZLCloudPlugin, Display, TEXT("Unprocessed State in previous request: %s"), *JsonString_Unprocessed);
			}
		}
	}
	else if (JsonObject_requestedStateLeafCount != JsonObject_processingStateFinishedLeaves) //Also need to account for any requests that never entered processing due to invalid fields or instant confirms but were incomplete
	{
		requestId = JsonObject_out_requestedState->GetStringField(s_requestIdStr);

		//Return current state + unprocessed to the web
		TSharedPtr<FJsonObject> unprocessedJson = CurrentStateCompareDiffs(JsonObject_out_requestedState);
		if (unprocessedJson->HasField(s_requestIdStr))
			unprocessedJson->RemoveField(s_requestIdStr);

		currentJson->SetStringField("status", "unmatched");
		currentJson->SetObjectField("current_state", JsonObject_currentState);
		currentJson->SetObjectField("unprocessed_state", unprocessedJson);

		FString JsonString_Unprocessed;
		TSharedRef<TJsonWriter<TCHAR>> JsonWriterUnprocessed = TJsonWriterFactory<TCHAR>::Create(&JsonString_Unprocessed, 1);
		FJsonSerializer::Serialize(unprocessedJson.ToSharedRef(), JsonWriterUnprocessed);
		JsonWriterUnprocessed->Close();

		UE_LOG(LogZLCloudPlugin, Display, TEXT("Unprocessed State in previous request: %s"), *JsonString_Unprocessed);
	}
	else //internal state update, just needs to send complete current state
	{
		currentJson->SetStringField("status", "complete");
		currentJson->SetObjectField("current_state", JsonObject_currentState);
	}

	jsonForWebObject->SetObjectField("state_processing_ended", currentJson);
	if(!requestId.IsEmpty())
		jsonForWebObject->SetStringField(s_requestIdStr, requestId); //add in request Id	

	SendFJsonObjectToWeb(jsonForWebObject);
}

void UZLCloudPluginStateManager::SendFJsonObjectToWeb(TSharedPtr<FJsonObject> JsonObject)
{
	IZLCloudPluginModule* Module = ZLCloudPlugin::FZLCloudPluginModule::GetModule();
	if (Module)
	{
		FString JsonString_forWeb;
		TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString_forWeb, 1);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);
		JsonWriter->Close();

		Module->SendData(JsonString_forWeb);
	}
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
