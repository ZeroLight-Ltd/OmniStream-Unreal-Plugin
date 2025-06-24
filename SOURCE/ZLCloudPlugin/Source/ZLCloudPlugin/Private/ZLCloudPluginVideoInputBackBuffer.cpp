// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginVideoInputBackBuffer.h"

#if UNREAL_5_1_OR_NEWER

#include "ZLCloudPluginPrivate.h"
#include "Utils.h"

#include "PixelCaptureInputFrameRHI.h"
#include "PixelCaptureBufferFormat.h"

#include "Framework/Application/SlateApplication.h"

TSharedPtr<FZLCloudPluginVideoInputBackBuffer> FZLCloudPluginVideoInputBackBuffer::Create()
{
	// this was added to fix packaging
	if (!FSlateApplication::IsInitialized())
	{
		return nullptr;
	}

	TSharedPtr<FZLCloudPluginVideoInputBackBuffer> NewInput = TSharedPtr<FZLCloudPluginVideoInputBackBuffer>(new FZLCloudPluginVideoInputBackBuffer());
	TWeakPtr<FZLCloudPluginVideoInputBackBuffer> WeakInput = NewInput;

	// Set up the callback on the game thread since FSlateApplication::Get() can only be used there
	ZLCloudPluginUtils::DoOnGameThread([WeakInput]() {
		if (TSharedPtr<FZLCloudPluginVideoInputBackBuffer> Input = WeakInput.Pin())
		{
			FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
			Input->DelegateHandle = Renderer->OnBackBufferReadyToPresent().AddSP(Input.ToSharedRef(), &FZLCloudPluginVideoInputBackBuffer::OnBackBufferReady);
		}
	});

	return NewInput;
}

FZLCloudPluginVideoInputBackBuffer::~FZLCloudPluginVideoInputBackBuffer()
{
	if (!IsEngineExitRequested())
	{
		ZLCloudPluginUtils::DoOnGameThread([HandleCopy = DelegateHandle]() {
			FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(HandleCopy);
		});
	}
}

#if UNREAL_5_5_OR_NEWER
void FZLCloudPluginVideoInputBackBuffer::OnBackBufferReady(SWindow& SlateWindow, const FTextureRHIRef& FrameBuffer)
#else
void FZLCloudPluginVideoInputBackBuffer::OnBackBufferReady(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer)
#endif
{
	OnFrame(FPixelCaptureInputFrameRHI(FrameBuffer));
}

#endif
