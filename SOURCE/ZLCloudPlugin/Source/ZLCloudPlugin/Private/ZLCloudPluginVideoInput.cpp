// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginVideoInput.h"

#if UNREAL_5_1_OR_NEWER


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
	FrameCapturer->Capture(InputFrame);
	LastFrameWidth = InputFrame.GetWidth();
	LastFrameHeight = InputFrame.GetHeight();
}


TSharedPtr<IPixelCaptureOutputFrame> FZLCloudPluginVideoInput::RequestFormat(int32 Format, int32 LayerIndex)
{
	if (FrameCapturer != nullptr)
	{
		return FrameCapturer->RequestFormat(Format, LayerIndex);
	}
	return nullptr;
}

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
	

	FrameCapturer = FPixelCaptureCapturerMultiFormat::Create(this, LayerScaling);
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

