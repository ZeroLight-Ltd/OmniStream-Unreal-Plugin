// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginVideoInputViewport.h"

#if UNREAL_5_1_OR_NEWER

#include "Utils.h"
#include "PixelCaptureInputFrameRHI.h"
#include "PixelCaptureBufferFormat.h"
#include "PixelCaptureCapturerRHI.h"
#include "PixelCaptureCapturerRHIToI420CPU.h"
#include "PixelCaptureCapturerRHIToI420Compute.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "RenderingThread.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"
#include "IZLCloudPluginModule.h"


TSharedPtr<FZLCloudPluginVideoInputViewport> FZLCloudPluginVideoInputViewport::Create()
{
	TSharedPtr<FZLCloudPluginVideoInputViewport> NewInput = TSharedPtr<FZLCloudPluginVideoInputViewport>(new FZLCloudPluginVideoInputViewport());
	TWeakPtr<FZLCloudPluginVideoInputViewport> WeakInput = NewInput;

	// Set up the callback on the game thread since FSlateApplication::Get() can only be used there
	ZLCloudPluginUtils::DoOnGameThread([WeakInput]() {
		if (TSharedPtr<FZLCloudPluginVideoInputViewport> Input = WeakInput.Pin())
		{
			Input->DelegateHandle = UGameViewportClient::OnViewportRendered().AddSP(Input.ToSharedRef(), &FZLCloudPluginVideoInputViewport::OnViewportRendered);
		}
	});

	return NewInput;
}

FZLCloudPluginVideoInputViewport::~FZLCloudPluginVideoInputViewport()
{
	if (!IsEngineExitRequested())
	{
		ZLCloudPluginUtils::DoOnGameThread([HandleCopy = DelegateHandle]() {
			UGameViewportClient::OnViewportRendered().Remove(HandleCopy);
		});
	}
}

void FZLCloudPluginVideoInputViewport::OnViewportRendered(FViewport* InViewport)
{
	IZLCloudPluginModule& Module = IZLCloudPluginModule::Get();
	TWeakPtr<SViewport> TargetViewport = Module.GetTargetViewport();
	if (!TargetViewport.IsValid())
	{
		return;
	}

	TSharedPtr<SViewport> TargetScene = TargetViewport.Pin();
	if (!TargetScene.IsValid())
	{
		return;
	}

	if (InViewport == nullptr)
	{
		return;
	}

	if (InViewport->GetViewportType() != TargetViewportType)
	{
		return;
	}

	// Bit dirty to do a static cast here, but we check viewport type just above so it is somewhat "safe".
	TSharedPtr<SViewport> InScene = static_cast<FSceneViewport*>(InViewport)->GetViewportWidget().Pin();

	// If the viewport we were passed is not our target viewport we are not interested in getting its texture.
	if (TargetScene != InScene)
	{
		return;
	}

	const FTextureRHIRef& FrameBuffer = InViewport->GetRenderTargetTexture();
	if (!FrameBuffer)
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(StreamViewportTextureCommand)
	([&, FrameBuffer](FRHICommandList& RHICmdList) {
		OnFrame(FPixelCaptureInputFrameRHI(FrameBuffer));
	});
}

#endif

