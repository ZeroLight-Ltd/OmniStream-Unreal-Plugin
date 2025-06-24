// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLTrackedStateBlueprint.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include <Engine.h>
#include <ZLCloudPluginStateManager.h>

UZLTrackedStateBlueprint* UZLTrackedStateBlueprint::Singleton = nullptr;

UZLTrackedStateBlueprint* UZLTrackedStateBlueprint::CreateInstance()
{
    if (Singleton == nullptr)
    {
        Singleton = NewObject<UZLTrackedStateBlueprint>();
        Singleton->AddToRoot();
        return Singleton;
    }
    return Singleton;
}

UZLTrackedStateBlueprint::UZLTrackedStateBlueprint(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void UZLTrackedStateBlueprint::SendToStateManager(const FString& bluePrintName, const FString& JsonString)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (FJsonSerializer::Deserialize(Reader, JsonObject))
    {
        UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->MergeTrackedStateIntoCurrentState(bluePrintName, JsonObject);
        UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->SendCurrentStateToWeb(true);
    }
}


// Function to save variables of a Blueprint instance to JSON
FString UZLTrackedStateBlueprint::SaveStateBlueprintVariables(UObject* BlueprintInstance)
{
    if (!BlueprintInstance)
    {
        return "{}";
    }

    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());

    UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(BlueprintInstance->GetClass());
    if (BPClass)
    {
        for (FProperty* Property = BPClass->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
        {
            const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(BlueprintInstance);

            bool bIsBlueprintVariable = Property->GetOwnerClass() == BPClass;
            if (bIsBlueprintVariable)
            {
                if (!Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance))
                {
                    if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
                    {
                        TSharedPtr<FJsonObject> PropertyObject = MakeShareable(new FJsonObject());
                        PropertyObject->SetStringField(TEXT("type"), TEXT("int"));
                        PropertyObject->SetNumberField(TEXT("value"), IntProperty->GetPropertyValue(PropertyValue));
                        JsonObject->SetObjectField(Property->GetName(), PropertyObject);
                    }
                    else if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
                    {
                        TSharedPtr<FJsonObject> PropertyObject = MakeShareable(new FJsonObject());
                        PropertyObject->SetStringField(TEXT("type"), TEXT("float"));
                        PropertyObject->SetNumberField(TEXT("value"), FloatProperty->GetPropertyValue(PropertyValue));
                        JsonObject->SetObjectField(Property->GetName(), PropertyObject);
                    }
                    else if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
                    {
                        TSharedPtr<FJsonObject> PropertyObject = MakeShareable(new FJsonObject());
                        PropertyObject->SetStringField(TEXT("type"), TEXT("double"));
                        PropertyObject->SetNumberField(TEXT("value"), DoubleProperty->GetPropertyValue(PropertyValue));
                        JsonObject->SetObjectField(Property->GetName(), PropertyObject);
                    }
                    else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
                    {
                        TSharedPtr<FJsonObject> PropertyObject = MakeShareable(new FJsonObject());
                        PropertyObject->SetStringField(TEXT("type"), TEXT("bool"));
                        PropertyObject->SetBoolField(TEXT("value"), BoolProperty->GetPropertyValue(PropertyValue));
                        JsonObject->SetObjectField(Property->GetName(), PropertyObject);
                    }
                    else if (FStrProperty* StrProperty = CastField<FStrProperty>(Property))
                    {
                        TSharedPtr<FJsonObject> PropertyObject = MakeShareable(new FJsonObject());
                        PropertyObject->SetStringField(TEXT("type"), TEXT("string"));
                        PropertyObject->SetStringField(TEXT("value"), StrProperty->GetPropertyValue(PropertyValue));
                        JsonObject->SetObjectField(Property->GetName(), PropertyObject);
                    }
                    else if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
                    {
                        TSharedPtr<FJsonObject> PropertyObject = MakeShareable(new FJsonObject());
                        PropertyObject->SetStringField(TEXT("type"), TEXT("name"));
                        PropertyObject->SetStringField(TEXT("value"), NameProperty->GetPropertyValue(PropertyValue).ToString());
                        JsonObject->SetObjectField(Property->GetName(), PropertyObject);
                    }
                    else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
                    {
                        if (StructProperty->Struct->GetFName() == NAME_Vector)
                        {
                            FVector VectorValue = *StructProperty->ContainerPtrToValuePtr<FVector>(BlueprintInstance);
                            TSharedPtr<FJsonObject> PropertyObject = MakeShareable(new FJsonObject());
                            PropertyObject->SetStringField(TEXT("type"), TEXT("vector"));
                            
                            TSharedPtr<FJsonObject> VectorJson = MakeShareable(new FJsonObject());
                            VectorJson->SetNumberField(TEXT("X"), VectorValue.X);
                            VectorJson->SetNumberField(TEXT("Y"), VectorValue.Y);
                            VectorJson->SetNumberField(TEXT("Z"), VectorValue.Z);
                            PropertyObject->SetObjectField(TEXT("value"), VectorJson);
                            
                            JsonObject->SetObjectField(Property->GetName(), PropertyObject);
                        }
                        else if (StructProperty->Struct->GetFName() == NAME_Color)
                        {
                            FColor ColorValue = *StructProperty->ContainerPtrToValuePtr<FColor>(BlueprintInstance);
                            TSharedPtr<FJsonObject> PropertyObject = MakeShareable(new FJsonObject());
                            PropertyObject->SetStringField(TEXT("type"), TEXT("color"));
                            
                            TSharedPtr<FJsonObject> ColorJson = MakeShareable(new FJsonObject());
                            ColorJson->SetNumberField(TEXT("R"), ColorValue.R);
                            ColorJson->SetNumberField(TEXT("G"), ColorValue.G);
                            ColorJson->SetNumberField(TEXT("B"), ColorValue.B);
                            ColorJson->SetNumberField(TEXT("A"), ColorValue.A);
                            PropertyObject->SetObjectField(TEXT("value"), ColorJson);
                            
                            JsonObject->SetObjectField(Property->GetName(), PropertyObject);
                        }
                    }
                }
            }
        }
    }

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

    return JsonString;
}

bool UZLTrackedStateBlueprint::GetFromStateManager(const FString& bluePrintName, FString& OutJsonString)
{
    TSharedPtr<FJsonValue> jsonData;
    bool Success = false;

    UZLCloudPluginStateManager::GetZLCloudPluginStateManager()->GetRequestedStateValue<TSharedPtr<FJsonValue>>(bluePrintName, true, jsonData, Success);

    if (Success && jsonData.IsValid())
    {        
        if (jsonData->Type == EJson::Object)  // Check if the jsonData is of type Object
        {
            TSharedPtr<FJsonObject> JsonObject = jsonData->AsObject();
            if (JsonObject.IsValid())
            {
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
                FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);       
            }
        } 
    }

    return Success;
}

// Function to load variables from JSON to a Blueprint instance
void UZLTrackedStateBlueprint::ProcessStateBlueprintVariables(UObject* BlueprintInstance, const FString& JsonString)
{
    if (!BlueprintInstance)
    {
        return;
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (FJsonSerializer::Deserialize(Reader, JsonObject))
    {
        if (JsonObject.IsValid())
        {
            for (TFieldIterator<FProperty> PropertyIterator(BlueprintInstance->GetClass()); PropertyIterator; ++PropertyIterator)
            {
                FProperty* Property = *PropertyIterator;
                void* PropertyValue = Property->ContainerPtrToValuePtr<void>(BlueprintInstance);

                if (JsonObject->HasField(Property->GetName()))
                {
                    TSharedPtr<FJsonObject> PropertyObject = JsonObject->GetObjectField(Property->GetName());
                    if (!PropertyObject.IsValid()) continue;

                    TSharedPtr<FJsonValue> ValueField = PropertyObject->TryGetField(TEXT("value"));
                    if (!ValueField.IsValid()) continue;

                    if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
                    {
                        IntProperty->SetPropertyValue(PropertyValue, ValueField->AsNumber());
                    }
                    else if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
                    {
                        FloatProperty->SetPropertyValue(PropertyValue, ValueField->AsNumber());
                    }
                    else if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
                    {
                        DoubleProperty->SetPropertyValue(PropertyValue, ValueField->AsNumber());
                    }
                    else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
                    {
                        BoolProperty->SetPropertyValue(PropertyValue, ValueField->AsBool());
                    }
                    else if (FStrProperty* StrProperty = CastField<FStrProperty>(Property))
                    {
                        StrProperty->SetPropertyValue(PropertyValue, ValueField->AsString());
                    }
                    else if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
                    {
                        NameProperty->SetPropertyValue(PropertyValue, FName(*ValueField->AsString()));
                    }
                    else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
                    {
                        if (StructProperty->Struct->GetFName() == NAME_Vector)
                        {
                            TSharedPtr<FJsonObject> VectorJson = ValueField->AsObject();
                            FVector VectorValue = *StructProperty->ContainerPtrToValuePtr<FVector>(BlueprintInstance);
                            VectorJson->TryGetNumberField(TEXT("X"), VectorValue.X);
                            VectorJson->TryGetNumberField(TEXT("Y"), VectorValue.Y);
                            VectorJson->TryGetNumberField(TEXT("Z"), VectorValue.Z);
                            *StructProperty->ContainerPtrToValuePtr<FVector>(BlueprintInstance) = VectorValue;
                        }
                        else if (StructProperty->Struct->GetFName() == NAME_Color)
                        {
                            TSharedPtr<FJsonObject> ColorJson = ValueField->AsObject();
                            FColor ColorValue = *StructProperty->ContainerPtrToValuePtr<FColor>(BlueprintInstance);
                            uint8 R, G, B, A;
                            ColorJson->TryGetNumberField(TEXT("R"), R);
                            ColorJson->TryGetNumberField(TEXT("G"), G);
                            ColorJson->TryGetNumberField(TEXT("B"), B);
                            ColorJson->TryGetNumberField(TEXT("A"), A);
                            ColorValue = FColor(R, G, B, A);
                            *StructProperty->ContainerPtrToValuePtr<FColor>(BlueprintInstance) = ColorValue;
                        }
                    }
                }
            }
        }
    }
}
