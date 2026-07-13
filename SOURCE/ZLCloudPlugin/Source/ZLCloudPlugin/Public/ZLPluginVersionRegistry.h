// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"

/**
 * Structure to hold plugin version information
 */
struct FZLPluginVersionInfo
{
	FString PluginName;
	FString Version;

	FZLPluginVersionInfo(const FString& InPluginName, const FString& InVersion)
		: PluginName(InPluginName)
		, Version(InVersion)
	{}
};

/**
 * Register a plugin version with the ZLCloudPlugin version registry
 * Call this from each plugin's StartupModule to register its version
 * @param PluginName Name of the plugin
 * @param Version Version string (e.g., "1.0.0.gitHash")
 */
ZLCLOUDPLUGIN_API void RegisterZLPluginVersion(const FString& PluginName, const FString& Version);

/**
 * Helper macro to register a plugin version using its version define
 * Usage in StartupModule(): REGISTER_ZL_PLUGIN_VERSION(PluginName, VERSION_DEFINE)
 * Example: REGISTER_ZL_PLUGIN_VERSION("ZLVE", ZLVE_VERSION)
 */
#ifndef REGISTER_ZL_PLUGIN_VERSION
#define REGISTER_ZL_PLUGIN_VERSION(PluginName, VersionDefine) \
	RegisterZLPluginVersion(TEXT(PluginName), FString(VersionDefine))
#endif

/**
 * Get all ZeroLight plugin versions
 * @return Array of plugin version information
 */
ZLCLOUDPLUGIN_API TArray<FZLPluginVersionInfo> GetAllZLPluginVersions();

/**
 * Callback type for broadcasting version info JSON
 */
typedef TFunction<void(TSharedPtr<FJsonObject>)> FZLVersionBroadcastCallback;

/**
 * Register a callback function to broadcast version info JSON
 * This allows other modules to add their version info without creating circular dependencies
 * @param Callback Function to call when broadcasting version info
 */
ZLCLOUDPLUGIN_API void RegisterZLVersionBroadcastCallback(FZLVersionBroadcastCallback Callback);

/**
 * Get all ZeroLight plugin versions as a JSON object
 * Populates the JSON with all registered plugin versions and optionally broadcasts via registered callback
 * @param bBroadcastDelegate If true, calls registered broadcast callback (if available)
 * @return Shared pointer to JSON object containing all plugin versions
 */
ZLCLOUDPLUGIN_API TSharedPtr<FJsonObject> GetAllZLPluginVersionsAsJson(bool bBroadcastDelegate = false);
