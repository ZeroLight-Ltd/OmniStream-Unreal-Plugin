#include "ZLSpotLightDataDrivenUIManager.h"
#include "Json.h"
#include "JsonObjectConverter.h"

// Initialize static member
TMap<FString, UZLSpotLightDataDrivenUIManager*> UZLSpotLightDataDrivenUIManager::RegisteredManagers = TMap<FString, UZLSpotLightDataDrivenUIManager*>();

UZLSpotLightDataDrivenUIManager::UZLSpotLightDataDrivenUIManager()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FString UZLSpotLightDataDrivenUIManager::RegisterCategoryName(const FString& InCategoryName)
{
    if (InCategoryName.IsEmpty())
    {
        return RegisterCategoryName("unnamed");
    }

    // Generate unique name if necessary
    FString UniqueName = GenerateUniqueCategoryName(InCategoryName);
    
    // Store the category name
    CategoryName = UniqueName;
    
    // Add to static map
    RegisteredManagers.Add(UniqueName, this);

    return UniqueName;
}

void UZLSpotLightDataDrivenUIManager::UnregisterCategoryName()
{
    if (!CategoryName.IsEmpty())
    {
        RegisteredManagers.Remove(CategoryName);
        CategoryName.Empty();
    }
}

FString UZLSpotLightDataDrivenUIManager::SyncDataToServer(bool bSimpleFormat)
{
    // Create the root JSON object
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> UiObject = MakeShared<FJsonObject>();
    
    for (const auto& Manager : RegisteredManagers)
    {
        if (bSimpleFormat)
        {
            // Create simplified manager object
            TSharedPtr<FJsonObject> ManagerObject = MakeShared<FJsonObject>();
            ManagerObject->SetStringField(TEXT("DisplayName"), Manager.Key);
            ManagerObject->SetStringField(TEXT("Category"), TEXT("Render Effects"));
            ManagerObject->SetArrayField(TEXT("IncompatibilityFlags"), TArray<TSharedPtr<FJsonValue>>());
            ManagerObject->SetNumberField(TEXT("SpotLightOrder"), 0);
            ManagerObject->SetBoolField(TEXT("AlwaysActive"), true);
            ManagerObject->SetBoolField(TEXT("enabled"), true);

            UiObject->SetObjectField(FString::Printf(TEXT("ZLCert_%s"), *Manager.Key), ManagerObject);
        }
        else
        {
            // Parse the stored JSON string into a JSON object
            TSharedPtr<FJsonObject> InputObject = MakeShared<FJsonObject>();
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Manager.Value->StoredJsonData);
            
            if (!Manager.Value->StoredJsonData.IsEmpty() && 
                FJsonSerializer::Deserialize(Reader, InputObject))
            {
                // Create the converted manager object with static fields
                TSharedPtr<FJsonObject> ManagerObject = MakeShared<FJsonObject>();
                ManagerObject->SetStringField(TEXT("Version"), TEXT("0.0.4"));
                ManagerObject->SetStringField(TEXT("DisplayName"), Manager.Key);
                ManagerObject->SetStringField(TEXT("Category"), TEXT("Render Effects"));
                ManagerObject->SetArrayField(TEXT("IncompatibilityFlags"), TArray<TSharedPtr<FJsonValue>>());
                ManagerObject->SetArrayField(TEXT("UserContentAssetData"), TArray<TSharedPtr<FJsonValue>>());
                ManagerObject->SetBoolField(TEXT("Active"), true);
                ManagerObject->SetBoolField(TEXT("AlwaysActive"), true);
                ManagerObject->SetObjectField(TEXT("Regions"), MakeShared<FJsonObject>());

                // Create Properties object
                TSharedPtr<FJsonObject> PropertiesObject = MakeShared<FJsonObject>();

                // Convert each parameter to the new format
                for (const auto& Param : InputObject->Values)
                {
                    TSharedPtr<FJsonObject> ParamObject = Param.Value->AsObject();
                    TSharedPtr<FJsonObject> NewPropertyObject = MakeShared<FJsonObject>();

                    // Set common fields
                    NewPropertyObject->SetStringField(TEXT("DisplayName"), Param.Key);
                    NewPropertyObject->SetStringField(TEXT("Tooltip"), TEXT(""));
                    NewPropertyObject->SetArrayField(TEXT("PossibleValues"), TArray<TSharedPtr<FJsonValue>>());
                    NewPropertyObject->SetStringField(TEXT("Region"), TEXT(""));
                    NewPropertyObject->SetStringField(TEXT("SubRegion"), TEXT(""));
                    NewPropertyObject->SetNumberField(TEXT("Order"), 1);
                    NewPropertyObject->SetBoolField(TEXT("DrawInUI"), true);
                    NewPropertyObject->SetField(TEXT("ChannelStrings"), MakeShared<FJsonValueNull>());
                    NewPropertyObject->SetField(TEXT("AdditionalUIHints"), MakeShared<FJsonValueNull>());
                    NewPropertyObject->SetField(TEXT("TextColor"), MakeShared<FJsonValueNull>());

                    FString Type = ParamObject->GetStringField(TEXT("type"));
                    if (Type.Equals(TEXT("int"), ESearchCase::IgnoreCase))
                    {
                        NewPropertyObject->SetStringField(TEXT("Type"), TEXT("Int"));
                        NewPropertyObject->SetNumberField(TEXT("Value"), ParamObject->GetNumberField(TEXT("value")));
                        NewPropertyObject->SetField(TEXT("Min"), MakeShared<FJsonValueNull>());
                        NewPropertyObject->SetField(TEXT("Max"), MakeShared<FJsonValueNull>());
                        NewPropertyObject->SetField(TEXT("DefaultVal"), MakeShared<FJsonValueNull>());
                    }
                    else if (Type.Equals(TEXT("color"), ESearchCase::IgnoreCase))
                    {
                        NewPropertyObject->SetStringField(TEXT("Type"), TEXT("Color"));
                        TSharedPtr<FJsonObject> ColorObj = ParamObject->GetObjectField(TEXT("value"));
                        TArray<TSharedPtr<FJsonValue>> ColorArray;
                        ColorArray.Add(MakeShared<FJsonValueNumber>(ColorObj->GetNumberField(TEXT("R")) / 255.0f));
                        ColorArray.Add(MakeShared<FJsonValueNumber>(ColorObj->GetNumberField(TEXT("G")) / 255.0f));
                        ColorArray.Add(MakeShared<FJsonValueNumber>(ColorObj->GetNumberField(TEXT("B")) / 255.0f));
                        ColorArray.Add(MakeShared<FJsonValueNumber>(ColorObj->GetNumberField(TEXT("A")) / 255.0f));
                        NewPropertyObject->SetArrayField(TEXT("Value"), ColorArray);
                        NewPropertyObject->SetField(TEXT("Min"), MakeShared<FJsonValueNull>());
                        NewPropertyObject->SetField(TEXT("Max"), MakeShared<FJsonValueNull>());
                        NewPropertyObject->SetField(TEXT("DefaultVal"), MakeShared<FJsonValueNull>());
                    }
                    else if (Type.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
                    {
                        NewPropertyObject->SetStringField(TEXT("Type"), TEXT("Vector"));
                        TSharedPtr<FJsonObject> VectorObj = ParamObject->GetObjectField(TEXT("value"));
                        TArray<TSharedPtr<FJsonValue>> VectorArray;
                        VectorArray.Add(MakeShared<FJsonValueNumber>(VectorObj->GetNumberField(TEXT("X"))));
                        VectorArray.Add(MakeShared<FJsonValueNumber>(VectorObj->GetNumberField(TEXT("Y"))));
                        VectorArray.Add(MakeShared<FJsonValueNumber>(VectorObj->GetNumberField(TEXT("Z"))));
                        NewPropertyObject->SetArrayField(TEXT("Value"), VectorArray);
                        NewPropertyObject->SetField(TEXT("Min"), MakeShared<FJsonValueNull>());
                        NewPropertyObject->SetField(TEXT("Max"), MakeShared<FJsonValueNull>());
                        NewPropertyObject->SetField(TEXT("DefaultVal"), MakeShared<FJsonValueNull>());
                    }
                    else if (Type.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
                    {
                        NewPropertyObject->SetStringField(TEXT("Type"), TEXT("Boolean"));
                        NewPropertyObject->SetBoolField(TEXT("Value"), ParamObject->GetBoolField(TEXT("value")));
                        NewPropertyObject->SetField(TEXT("Min"), MakeShared<FJsonValueNull>());
                        NewPropertyObject->SetField(TEXT("Max"), MakeShared<FJsonValueNull>());
                        NewPropertyObject->SetField(TEXT("DefaultVal"), MakeShared<FJsonValueBoolean>(ParamObject->GetBoolField(TEXT("value"))));
                    }
                    else if (Type.Equals(TEXT("float"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("double"), ESearchCase::IgnoreCase))
                    {
                        NewPropertyObject->SetStringField(TEXT("Type"), TEXT("Float"));
                        NewPropertyObject->SetNumberField(TEXT("Value"), ParamObject->GetNumberField(TEXT("value")));
                        if (ParamObject->HasField(TEXT("min")))
                        {
                            NewPropertyObject->SetNumberField(TEXT("Min"), ParamObject->GetNumberField(TEXT("min")));
                        }
                        else
                        {
                            NewPropertyObject->SetField(TEXT("Min"), MakeShared<FJsonValueNull>());
                        }
                        if (ParamObject->HasField(TEXT("max")))
                        {
                            NewPropertyObject->SetNumberField(TEXT("Max"), ParamObject->GetNumberField(TEXT("max")));
                        }
                        else
                        {
                            NewPropertyObject->SetField(TEXT("Max"), MakeShared<FJsonValueNull>());
                        }
                        NewPropertyObject->SetNumberField(TEXT("DefaultVal"), ParamObject->GetNumberField(TEXT("value")));
                    }
                    else if (Type.Equals(TEXT("enum"), ESearchCase::IgnoreCase))
                    {
                        NewPropertyObject->SetStringField(TEXT("Type"), TEXT("Enum"));
                        NewPropertyObject->SetStringField(TEXT("Value"), ParamObject->GetStringField(TEXT("value")));
                        NewPropertyObject->SetField(TEXT("Min"), MakeShared<FJsonValueNull>());
                        NewPropertyObject->SetField(TEXT("Max"), MakeShared<FJsonValueNull>());
                        
                        // Handle possible values array
                        if (ParamObject->HasField(TEXT("possibleValues")))
                        {
                            NewPropertyObject->SetArrayField(TEXT("PossibleValues"), 
                                ParamObject->GetArrayField(TEXT("possibleValues")));
                        }
                        
                        NewPropertyObject->SetNumberField(TEXT("DefaultVal"), 0);
                    }
                    else if (Type.Equals(TEXT("string"), ESearchCase::IgnoreCase))
                    {
                        NewPropertyObject->SetStringField(TEXT("Type"), TEXT("String"));
                        NewPropertyObject->SetStringField(TEXT("Value"), ParamObject->GetStringField(TEXT("value")));
                        NewPropertyObject->SetField(TEXT("Min"), MakeShared<FJsonValueNull>());
                        NewPropertyObject->SetField(TEXT("Max"), MakeShared<FJsonValueNull>());
                        NewPropertyObject->SetStringField(TEXT("DefaultVal"), ParamObject->GetStringField(TEXT("value")));
                    }

                    PropertiesObject->SetObjectField(Param.Key, NewPropertyObject);
                }

                ManagerObject->SetObjectField(TEXT("Properties"), PropertiesObject);
                UiObject->SetObjectField(FString::Printf(TEXT("ZLCert_%s"), *Manager.Key), ManagerObject);
            }
        }
    }
    
    if (bSimpleFormat)
    {
        RootObject->SetObjectField(TEXT("ZLCertifiedEffects"), UiObject);
    }
    else
    {
        RootObject->SetObjectField(TEXT("ZLCertifiedEffectsUI"), UiObject);
    }
    
    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
    
    return OutputString;
}

void UZLSpotLightDataDrivenUIManager::ReceiveDataFromServer(const FString& ServerData)
{
    // Parse the incoming JSON
    TSharedPtr<FJsonObject> JsonParsed;
    TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(ServerData);
    if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
    {
        const TSharedPtr<FJsonObject> ZLCertifiedEffects = JsonParsed->GetObjectField(TEXT("ZLCertifiedEffects"));
        if (ZLCertifiedEffects.IsValid())
        {
            // For each effect in ZLCertifiedEffects
            for (const auto& Effect : ZLCertifiedEffects->Values)
            {
                // Strip "ZLCert_" prefix from the effect name
                FString ManagerName = Effect.Key;
                if (ManagerName.StartsWith(TEXT("ZLCert_")))
                {
                    ManagerName.RightChopInline(7); // Remove "ZLCert_"
                }

                if (UZLSpotLightDataDrivenUIManager* Manager = RegisteredManagers.FindRef(ManagerName))
                {
                    // Parse the existing stored JSON
                    TSharedPtr<FJsonObject> ExistingParams;
                    TSharedRef<TJsonReader<>> ExistingReader = TJsonReaderFactory<>::Create(Manager->StoredJsonData);
                    if (!Manager->StoredJsonData.IsEmpty() && 
                        FJsonSerializer::Deserialize(ExistingReader, ExistingParams))
                    {
                        // Update each changed property
                        const TSharedPtr<FJsonObject>* UpdateObject;
                        if (Effect.Value->TryGetObject(UpdateObject))
                        {
                            for (const auto& Prop : (*UpdateObject)->Values)
                            {
                                // Get the existing property
                                const TSharedPtr<FJsonObject> ExistingProp = ExistingParams->GetObjectField(Prop.Key);
                                if (ExistingProp.IsValid())
                                {
                                    FString Type = ExistingProp->GetStringField(TEXT("type"));
                                    
                                    if (Type == TEXT("bool"))
                                    {
                                        ExistingProp->SetBoolField(TEXT("value"), Prop.Value->AsBool());
                                    }
                                    else if (Type == TEXT("float") || Type == TEXT("double"))
                                    {
                                        ExistingProp->SetNumberField(TEXT("value"), Prop.Value->AsNumber());
                                    }
                                    else if (Type == TEXT("int"))
                                    {
                                        ExistingProp->SetNumberField(TEXT("value"), Prop.Value->AsNumber());
                                    }
                                    else if (Type == TEXT("string") || Type == TEXT("name"))
                                    {
                                        ExistingProp->SetStringField(TEXT("value"), Prop.Value->AsString());
                                    }
                                    else if (Type == TEXT("color"))
                                    {
                                        const auto& Array = Prop.Value->AsArray();
                                        TSharedPtr<FJsonObject> ColorObject = MakeShared<FJsonObject>();
                                        ColorObject->SetNumberField(TEXT("R"), Array[0]->AsNumber() * 255.0f);
                                        ColorObject->SetNumberField(TEXT("G"), Array[1]->AsNumber() * 255.0f);
                                        ColorObject->SetNumberField(TEXT("B"), Array[2]->AsNumber() * 255.0f);
                                        ColorObject->SetNumberField(TEXT("A"), Array[3]->AsNumber() * 255.0f);
                                        ExistingProp->SetObjectField(TEXT("value"), ColorObject);
                                    }
                                    else if (Type == TEXT("vector"))
                                    {
                                        const auto& Array = Prop.Value->AsArray();
                                        TSharedPtr<FJsonObject> VectorObject = MakeShared<FJsonObject>();
                                        VectorObject->SetNumberField(TEXT("X"), Array[0]->AsNumber());
                                        VectorObject->SetNumberField(TEXT("Y"), Array[1]->AsNumber());
                                        VectorObject->SetNumberField(TEXT("Z"), Array[2]->AsNumber());
                                        ExistingProp->SetObjectField(TEXT("value"), VectorObject);
                                    }
                                }
                            }

                            // Convert the updated params back to a JSON string
                            FString JsonString;
                            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
                            FJsonSerializer::Serialize(ExistingParams.ToSharedRef(), Writer);

                            // Store and broadcast the updated JSON string
                            Manager->StoredJsonData = JsonString;
                            if (Manager->OnDataReceived.IsBound())
                            {
                                Manager->OnDataReceived.Broadcast(JsonString);
                            }
                        }
                    }
                }
            }
        }
    }
}

FString UZLSpotLightDataDrivenUIManager::GenerateUniqueCategoryName(const FString& BaseName)
{
    FString UniqueName = BaseName;
    int32 Counter = 1;

    // Keep trying until we find a unique name
    while (RegisteredManagers.Contains(UniqueName))
    {
        UniqueName = FString::Printf(TEXT("%s_%d"), *BaseName, Counter);
        Counter++;
    }

    return UniqueName;
}

void UZLSpotLightDataDrivenUIManager::SetJsonData(const FString& InJsonData)
{
    StoredJsonData = InJsonData;
} 