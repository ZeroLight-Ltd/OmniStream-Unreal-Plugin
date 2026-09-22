// Copyright ZeroLight ltd. All Rights Reserved.
#include "ZLCloudPluginShaders.h"

#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

void FZLCloudPluginShadersModule::StartupModule()
{
	// Map the plugin's Shaders dir to a virtual path so IMPLEMENT_GLOBAL_SHADER can find it.
	// This module loads at PostConfigInit, before shader types are initialised.
	const TSharedPtr<IPlugin> ThisPlugin = IPluginManager::Get().FindPlugin(TEXT("ZLCloudPlugin"));
	if (ThisPlugin.IsValid())
	{
		const FString ShaderDir = FPaths::Combine(ThisPlugin->GetBaseDir(), TEXT("Shaders"));
		if (!AllShaderSourceDirectoryMappings().Contains(TEXT("/Plugin/ZLCloudPlugin")))
		{
			AddShaderSourceDirectoryMapping(TEXT("/Plugin/ZLCloudPlugin"), ShaderDir);
		}
	}
}

void FZLCloudPluginShadersModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FZLCloudPluginShadersModule, ZLCloudPluginShaders);
