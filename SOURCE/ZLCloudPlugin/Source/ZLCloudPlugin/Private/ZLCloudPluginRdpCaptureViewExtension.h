// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginVersion.h"

#if UNREAL_5_8_OR_NEWER

#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "RHI.h"

struct FPostProcessMaterialInputs;

/**
 * During RDP the packaged game viewport RT is the swapchain. Copy post-process
 * scene color into a dedicated offscreen texture so the stream never touches Present.
 */
class FZLCloudPluginRdpCaptureViewExtension : public FSceneViewExtensionBase
{
public:
	FZLCloudPluginRdpCaptureViewExtension(const FAutoRegister& AutoRegister);
	virtual ~FZLCloudPluginRdpCaptureViewExtension();

	static FTextureRHIRef GetLatestTexture();

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass Pass,
		const FSceneView& InView,
		FAfterPassCallbackDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override;

private:
	FScreenPassTexture AfterPostProcess_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	FTextureRHIRef CaptureTexture;
	uint32 LastCopiedFrame = 0;

	static FZLCloudPluginRdpCaptureViewExtension* Instance;
};

#endif
