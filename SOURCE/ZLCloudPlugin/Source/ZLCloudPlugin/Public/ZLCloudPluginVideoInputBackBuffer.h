// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginVersion.h"
#if UNREAL_5_1_OR_NEWER

#include "ZLCloudPluginVideoInputRHI.h"
#include "Widgets/SWindow.h"
#include "RHI.h"
#include "Delegates/IDelegateInstance.h"

/*
 * Use this if you want to send the UE backbuffer as video input.
 */
class ZLCLOUDPLUGIN_API FZLCloudPluginVideoInputBackBuffer : public FZLCloudPluginVideoInputRHI
{
public:
	static TSharedPtr<FZLCloudPluginVideoInputBackBuffer> Create();
	virtual ~FZLCloudPluginVideoInputBackBuffer();

private:
	FZLCloudPluginVideoInputBackBuffer() = default;

#if UNREAL_5_5_OR_NEWER
	void OnBackBufferReady(SWindow& SlateWindow, const FTextureRHIRef& FrameBuffer);
#else
	void OnBackBufferReady(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer);
#endif

	FDelegateHandle DelegateHandle;
};

#endif
