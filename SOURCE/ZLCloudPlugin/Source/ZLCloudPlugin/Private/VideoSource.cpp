// Copyright ZeroLight ltd. All Rights Reserved.

#include "VideoSource.h"

#if UNREAL_5_1_OR_NEWER

#include "CloudStream2.h"

#include "PixelCaptureBufferFormat.h"
#include "PixelCaptureOutputFrameRHI.h"
#include "PixelCaptureInputFrameRHI.h"

namespace ZLCloudPlugin
{
	FVideoSource::FVideoSource(TSharedPtr<FZLCloudPluginVideoInput> InVideoInput, const TFunction<bool()>& InShouldGenerateFramesCheck)
		: VideoInput(InVideoInput)
		, ShouldGenerateFramesCheck(InShouldGenerateFramesCheck)
	{
	}

	void FVideoSource::MaybePushFrame()
	{
		if (VideoInput->IsReady() && ShouldGenerateFramesCheck())
		{
			PushFrame();
		}
	}

	void FVideoSource::PushFrame()
	{
		TSharedPtr<IPixelCaptureOutputFrame> OutputFrame = VideoInput->RequestFormat(PixelCaptureBufferFormat::FORMAT_RHI);
		if (OutputFrame)
		{
			FPixelCaptureOutputFrameRHI* RHISourceFrame = StaticCast<FPixelCaptureOutputFrameRHI*>(OutputFrame.Get());
			CloudStream2::OnFrame(RHISourceFrame->GetFrameTexture());
		}
	}
} // namespace ZLCloudPlugin

#endif