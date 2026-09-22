// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginVideoInput.h"

#if UNREAL_5_1_OR_NEWER

#include "PixelCaptureBufferFormat.h"


FZLCloudPluginVideoInput::FZLCloudPluginVideoInput()
{
	CreateFrameCapturer();
}

void FZLCloudPluginVideoInput::AddOutputFormat(int32 Format)
{
	PreInitFormats.Add(Format);
	FrameCapturer->AddOutputFormat(Format);
}

void FZLCloudPluginVideoInput::OnFrame(const IPixelCaptureInputFrame& InputFrame)
{
	if (LastFrameWidth != -1 && LastFrameHeight != -1)
	{
		if (InputFrame.GetWidth() != LastFrameWidth || InputFrame.GetHeight() != LastFrameHeight)
		{
			CreateFrameCapturer();
		}
	}

	Ready = true;
	
	LastFrameWidth = InputFrame.GetWidth();
	LastFrameHeight = InputFrame.GetHeight();

#if UNREAL_5_7_OR_NEWER
	// Match Pixel Streaming: WaitForFormat/RequestFormat registers FORMAT_RHI + layer size
	// before a real capture runs. With empty FormatCapturers, MultiFormat::Capture() only
	// broadcasts OnComplete and never copies — PushFrame then RequestFormat returns null.
	if (FrameCapturer)
	{
		const FIntPoint LayerSize(LastFrameWidth, LastFrameHeight);
		FrameCapturer->RequestFormat(PixelCaptureBufferFormat::FORMAT_RHI, LayerSize);
		PreInitFormats.Add(PixelCaptureBufferFormat::FORMAT_RHI);
	}
#endif

	FrameCapturer->Capture(InputFrame);
}

#if UNREAL_5_7_OR_NEWER
TSharedPtr<IPixelCaptureOutputFrame> FZLCloudPluginVideoInput::RequestFormat(int32 Format, TOptional<FIntPoint> Resolution)
{
	if (FrameCapturer != nullptr)
	{
		if (!Resolution.IsSet())
		{
			Resolution = { LastFrameWidth, LastFrameHeight };
		}
		return FrameCapturer->RequestFormat(Format, *Resolution);
	}
	return nullptr;
}
#else
TSharedPtr<IPixelCaptureOutputFrame> FZLCloudPluginVideoInput::RequestFormat(int32 Format, int32 LayerIndex)
{
	if (FrameCapturer != nullptr)
	{
		return FrameCapturer->RequestFormat(Format, LayerIndex);
	}
	return nullptr;
}
#endif

void FZLCloudPluginVideoInput::CreateFrameCapturer()
{
	if (FrameCapturer != nullptr)
	{
		FrameCapturer->OnDisconnected();
		FrameCapturer->OnComplete.Remove(CaptureCompleteHandle);
		FrameCapturer = nullptr;
	}

	TArray<float> LayerScaling;
	LayerScaling.Add(1.0f);
	
	//ZL
	/*
	for (auto& Layer : ZLCloudPlugin::Settings::SimulcastParameters.Layers)
	{
		LayerScaling.Add(1.0f / Layer.Scaling);
	}
	LayerScaling.Sort([](float ScaleA, float ScaleB) { return ScaleA < ScaleB; });
	*/
	

#if UNREAL_5_7_OR_NEWER
	// Layer-scale Create() is deprecated; resolutions come from captured frames.
	FrameCapturer = FPixelCaptureCapturerMultiFormat::Create(this);
#else
	FrameCapturer = FPixelCaptureCapturerMultiFormat::Create(this, LayerScaling);
#endif
	CaptureCompleteHandle = FrameCapturer->OnComplete.AddRaw(this, &FZLCloudPluginVideoInput::OnCaptureComplete);

	for (auto& Format : PreInitFormats)
	{
		FrameCapturer->AddOutputFormat(Format);
	}
}

void FZLCloudPluginVideoInput::OnCaptureComplete()
{
	OnFrameCaptured.Broadcast();
}

#endif

