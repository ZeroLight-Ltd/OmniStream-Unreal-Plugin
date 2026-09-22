// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FTextureRenderTargetResource;

class FZLCloudPluginShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

// Same pattern as PixelCaptureShaders' FRGBToYUVShader: the global shader type and
// IMPLEMENT_GLOBAL_SHADER live in this PostConfigInit module. Callers in ZLCloudPlugin
// only see this exported entry point.
#define UE_API ZLCLOUDPLUGINSHADERS_API

class FZLCubeToEquirectComposite
{
public:
	static UE_API void Enqueue(
		FTextureRenderTargetResource* OutRes,
		FTextureRenderTargetResource* const* FaceRes,
		const FVector4f* FaceBasis,
		float OverscanTan,
		int32 OutW,
		int32 OutH);
};

#undef UE_API
