// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginVideoInputI420.h"

#if UNREAL_5_1_OR_NEWER

#include "ZLCloudPluginPrivate.h"

#include "PixelCaptureBufferFormat.h"
#include "PixelCaptureCapturerI420.h"
#include "PixelCaptureCapturerI420ToRHI.h"

#if UNREAL_5_7_OR_NEWER
TSharedPtr<FPixelCaptureCapturer> FZLCloudPluginVideoInputI420::CreateCapturer(int32 FinalFormat, FIntPoint Resolution)
{
	switch (FinalFormat)
	{
		case PixelCaptureBufferFormat::FORMAT_RHI:
		{
			return FPixelCaptureCapturerI420ToRHI::Create();
		}
		case PixelCaptureBufferFormat::FORMAT_I420:
		{
			return FPixelCaptureCapturerI420::Create();
		}
		default:
			UE_LOG(LogZLCloudPlugin, Error, TEXT("Unsupported final format %d"), FinalFormat);
			return nullptr;
	}
}
#else
TSharedPtr<FPixelCaptureCapturer> FZLCloudPluginVideoInputI420::CreateCapturer(int32 FinalFormat, float FinalScale)
{
	switch (FinalFormat)
	{
		case PixelCaptureBufferFormat::FORMAT_RHI:
		{
			return FPixelCaptureCapturerI420ToRHI::Create();
		}
		case PixelCaptureBufferFormat::FORMAT_I420:
		{
			return MakeShared<FPixelCaptureCapturerI420>();
		}
		default:
			UE_LOG(LogZLCloudPlugin, Error, TEXT("Unsupported final format %d"), FinalFormat);
			return nullptr;
	}
}

#endif

#endif

