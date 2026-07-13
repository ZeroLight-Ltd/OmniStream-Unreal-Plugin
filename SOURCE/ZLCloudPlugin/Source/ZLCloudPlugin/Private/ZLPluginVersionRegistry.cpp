// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLPluginVersionRegistry.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "Containers/Map.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogZLPluginVersion, Log, All);

// Static map to store registered plugin versions
static TMap<FString, FString> RegisteredPluginVersions;

// Static callback for broadcasting version info
static FZLVersionBroadcastCallback VersionBroadcastCallback;

void RegisterZLPluginVersion(const FString& PluginName, const FString& Version)
{
	RegisteredPluginVersions.Add(PluginName, Version);
	UE_LOG(LogZLPluginVersion, Log, TEXT("Registered plugin version: %s = %s"), *PluginName, *Version);
}

void RegisterZLVersionBroadcastCallback(FZLVersionBroadcastCallback Callback)
{
	VersionBroadcastCallback = Callback;
}

TArray<FZLPluginVersionInfo> GetAllZLPluginVersions()
{
	TArray<FZLPluginVersionInfo> Versions;

	// Add all registered plugin versions
	for (const auto& Pair : RegisteredPluginVersions)
	{
		Versions.Add(FZLPluginVersionInfo(Pair.Key, Pair.Value));
	}

	// Sort by plugin name for consistent output
	Versions.Sort([](const FZLPluginVersionInfo& A, const FZLPluginVersionInfo& B)
	{
		return A.PluginName < B.PluginName;
	});

	return Versions;
}

TSharedPtr<FJsonObject> GetAllZLPluginVersionsAsJson(bool bBroadcastDelegate)
{
	TSharedPtr<FJsonObject> JsonVersionData = MakeShareable(new FJsonObject);

	// Add all registered plugin versions to JSON
	TArray<FZLPluginVersionInfo> Versions = GetAllZLPluginVersions();
	for (const FZLPluginVersionInfo& VersionInfo : Versions)
	{
		JsonVersionData->SetStringField(VersionInfo.PluginName, VersionInfo.Version);
	}

	// Optionally broadcast via registered callback to allow other plugins to add their version info
	if (bBroadcastDelegate && VersionBroadcastCallback)
	{
		VersionBroadcastCallback(JsonVersionData);
	}

	// Backfill any enabled ZeroLight plugins that did not explicitly register.
	// This covers content-only plugins and modules that missed startup registration.
	// ZeroLight plugins are identified by their descriptor's CreatedBy field rather than
	// a name prefix, so plugins such as DIMEConfigurator/DIMEEditor are included too.
	TArray<TSharedRef<IPlugin>> EnabledPlugins = IPluginManager::Get().GetEnabledPlugins();
	for (const TSharedRef<IPlugin>& Plugin : EnabledPlugins)
	{
		const FPluginDescriptor& PluginDescriptor = Plugin->GetDescriptor();
		if (!PluginDescriptor.CreatedBy.Equals(TEXT("ZeroLight"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString& PluginName = Plugin->GetName();
		if (JsonVersionData->HasField(PluginName))
		{
			continue;
		}

		FString VersionString = PluginDescriptor.VersionName;
		if (VersionString.IsEmpty())
		{
			VersionString = FString::FromInt(PluginDescriptor.Version);
		}

		JsonVersionData->SetStringField(PluginName, VersionString);
	}

	return JsonVersionData;
}
