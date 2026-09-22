// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginVideoInputRHI.h"

#if UNREAL_5_1_OR_NEWER

#include "ZLCloudPluginPrivate.h"
#include "ZLCloudPluginVersion.h"
#include "PixelCaptureBufferFormat.h"
#include "PixelCaptureCapturerRHI.h"
#include "PixelCaptureCapturerRHIRDG.h"
#include "PixelCaptureCapturerRHIToI420CPU.h"
#include "PixelCaptureCapturerRHIToI420Compute.h"

#if UNREAL_5_7_OR_NEWER
TSharedPtr<FPixelCaptureCapturer> FZLCloudPluginVideoInputRHI::CreateCapturer(int32 FinalFormat, FIntPoint Resolution)
{
	switch (FinalFormat)
	{
	case PixelCaptureBufferFormat::FORMAT_RHI:
	{
#if ZL_SAFE_CUDA_FRAME_SUBMIT
		// Fenced CapturerRHI — GPU-complete before OnFrameCaptured (CUDA must not
		// copy from a PixelCapture output that is still being written).
		return FPixelCaptureCapturerRHI::Create({ .OutputResolution = Resolution });
#else
		// RHIRDG is faster but does not wait for the GPU copy to finish.
		return FPixelCaptureCapturerRHIRDG::Create({ .OutputResolution = Resolution });
#endif
	}
	case PixelCaptureBufferFormat::FORMAT_I420:
	{
		if (true)//UE::PixelStreaming::Settings::CVarPixelStreamingVPXUseCompute.GetValueOnAnyThread()) //ZL
		{
			return FPixelCaptureCapturerRHIToI420Compute::Create({ .OutputResolution = Resolution });
		}
		else
		{
			return FPixelCaptureCapturerRHIToI420CPU::Create({ .OutputResolution = Resolution });
		}
	}
	default:
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Unsupported final format %d"), FinalFormat);
		return nullptr;
	}
}
#else

TSharedPtr<FPixelCaptureCapturer> FZLCloudPluginVideoInputRHI::CreateCapturer(int32 FinalFormat, float FinalScale)
{
	switch (FinalFormat)
	{
		case PixelCaptureBufferFormat::FORMAT_RHI:
		{
#if ZL_SAFE_CUDA_FRAME_SUBMIT
			return FPixelCaptureCapturerRHI::Create(FinalScale);
#else
			return FPixelCaptureCapturerRHIRDG::Create(FinalScale);
#endif
		}
		case PixelCaptureBufferFormat::FORMAT_I420:
		{
			if (true)//UE::ZLCloudPlugin::Settings::CVarZLCloudPluginVPXUseCompute.GetValueOnAnyThread()) //ZL
			{
				return FPixelCaptureCapturerRHIToI420Compute::Create(FinalScale);
			}
			else
			{
				return FPixelCaptureCapturerRHIToI420CPU::Create(FinalScale);
			}
		}
		default:
			UE_LOG(LogZLCloudPlugin, Error, TEXT("Unsupported final format %d"), FinalFormat);
			return nullptr;
	}
}

#endif

#endif
