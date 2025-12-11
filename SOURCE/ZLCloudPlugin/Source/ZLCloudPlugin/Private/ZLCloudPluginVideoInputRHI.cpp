// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginVideoInputRHI.h"

#if UNREAL_5_1_OR_NEWER

#include "ZLCloudPluginPrivate.h"
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
		// "Safe Texture Copy" polls a fence to ensure a GPU copy is complete
		// the RDG pathway does not poll a fence so is more unsafe but offers
		// a significant performance increase
		if (false)//UE::PixelStreaming::Settings::CVarPixelStreamingCaptureUseFence.GetValueOnAnyThread()) //ZL
		{
			return FPixelCaptureCapturerRHI::Create({ .OutputResolution = Resolution });
		}
		else
		{
			return FPixelCaptureCapturerRHIRDG::Create({ .OutputResolution = Resolution });
		}
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
			// "Safe Texture Copy" polls a fence to ensure a GPU copy is complete
			// the RDG pathway does not poll a fence so is more unsafe but offers 
			// a significant performance increase
			if (false)//UE::ZLCloudPlugin::Settings::IsUsingSafeTextureCopy()) //ZL
			{
				return FPixelCaptureCapturerRHI::Create(FinalScale);
			}
			else
			{
				return FPixelCaptureCapturerRHIRDG::Create(FinalScale);
			}
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


