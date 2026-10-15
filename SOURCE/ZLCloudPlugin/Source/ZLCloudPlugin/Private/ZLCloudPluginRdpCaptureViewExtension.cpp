// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginRdpCaptureViewExtension.h"

#if UNREAL_5_8_OR_NEWER

#include "ZLCloudPluginPrivate.h"
#include "Utils.h"

#include "PostProcess/PostProcessMaterial.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "SceneView.h"

FZLCloudPluginRdpCaptureViewExtension* FZLCloudPluginRdpCaptureViewExtension::Instance = nullptr;

FZLCloudPluginRdpCaptureViewExtension::FZLCloudPluginRdpCaptureViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
	Instance = this;
}

FZLCloudPluginRdpCaptureViewExtension::~FZLCloudPluginRdpCaptureViewExtension()
{
	if (Instance == this)
	{
		Instance = nullptr;
	}
}

FTextureRHIRef FZLCloudPluginRdpCaptureViewExtension::GetLatestTexture()
{
	return Instance ? Instance->CaptureTexture : nullptr;
}

void FZLCloudPluginRdpCaptureViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FAfterPassCallbackDelegateArray& InOutPassCallbacks,
	bool /*bIsPassEnabled*/)
{
	// One pass only. Subscribing to several passes reallocated the capture RT every frame and hung Present.
	if (Pass != EPostProcessingPass::Tonemap)
	{
		return;
	}
	if (InView.bIsSceneCapture || InView.bIsReflectionCapture)
	{
		return;
	}
	if (!ZLCloudPluginUtils::IsRdpClientConnected() || ZLCloudPluginUtils::ShouldSkipStreamGpuWork())
	{
		return;
	}

	InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(
		this, &FZLCloudPluginRdpCaptureViewExtension::AfterPostProcess_RenderThread));
}

FScreenPassTexture FZLCloudPluginRdpCaptureViewExtension::AfterPostProcess_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	(void)View;
	auto PassThrough = [&]() -> FScreenPassTexture
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	};

	if (!ZLCloudPluginUtils::IsRdpClientConnected() || ZLCloudPluginUtils::ShouldSkipStreamGpuWork())
	{
		return PassThrough();
	}
	if (LastCopiedFrame == GFrameNumberRenderThread)
	{
		return PassThrough();
	}

	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	if (!SceneColor.Texture)
	{
		return PassThrough();
	}

	const FIntPoint TextureExtent = SceneColor.Texture->Desc.Extent;
	const EPixelFormat Format = SceneColor.Texture->Desc.Format;
	if (TextureExtent.X <= 0 || TextureExtent.Y <= 0 || Format == PF_Unknown)
	{
		return PassThrough();
	}

	// Scene color is padded past the rendered view (often to a multiple of 8). Those
	// texels are uninitialized and show up as a garbage strip on the right and bottom.
	FIntRect ViewRect = SceneColor.ViewRect;
	ViewRect.Clip(FIntRect(FIntPoint::ZeroValue, TextureExtent));
	const FIntPoint CopySize = (ViewRect.Width() > 0 && ViewRect.Height() > 0) ? ViewRect.Size() : TextureExtent;
	const FIntPoint CopyOrigin = (ViewRect.Width() > 0 && ViewRect.Height() > 0) ? ViewRect.Min : FIntPoint::ZeroValue;

	if (!CaptureTexture
		|| CaptureTexture->GetDesc().Extent != CopySize
		|| CaptureTexture->GetDesc().Format != Format)
	{
		FRHITextureCreateDesc TexDesc = FRHITextureCreateDesc::Create2D(TEXT("ZLCloudRdpCapture"), CopySize.X, CopySize.Y, Format);
		TexDesc.AddFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource);
		TexDesc.SetInitialState(ERHIAccess::CopyDest);
		CaptureTexture = FRHICommandListImmediate::Get().CreateTexture(TexDesc);
		UE_LOG(LogZLCloudPlugin, Display, TEXT("CloudStream2 : RDP offscreen capture %dx%d (scene color %dx%d, view %d,%d %dx%d) fmt=%d"),
			CopySize.X, CopySize.Y, TextureExtent.X, TextureExtent.Y, CopyOrigin.X, CopyOrigin.Y, CopySize.X, CopySize.Y, (int32)Format);
	}

	FRDGTextureRef DestRDG = GraphBuilder.RegisterExternalTexture(
		CreateRenderTarget(CaptureTexture, TEXT("ZLCloudRdpCapture")));
	AddCopyTexturePass(GraphBuilder, SceneColor.Texture, DestRDG, CopyOrigin, FIntPoint::ZeroValue, CopySize);
	LastCopiedFrame = GFrameNumberRenderThread;

	static bool bLoggedCopy = false;
	if (!bLoggedCopy)
	{
		bLoggedCopy = true;
		UE_LOG(LogZLCloudPlugin, Display, TEXT("CloudStream2 : RDP client connected — copying tonemapped color off the swapchain"));
	}

	return PassThrough();
}

#endif
