// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginVersion.h"
#if UNREAL_5_1_OR_NEWER

#include "ZLCloudPluginVideoInput.h"

/*
 * A Generic video input for I420 frames
 */
class ZLCLOUDPLUGIN_API FZLCloudPluginVideoInputI420 : public FZLCloudPluginVideoInput
{
public:
	FZLCloudPluginVideoInputI420() = default;
	virtual ~FZLCloudPluginVideoInputI420() = default;

protected:
#if UNREAL_5_7_OR_NEWER
	virtual TSharedPtr<FPixelCaptureCapturer> CreateCapturer(int32 FinalFormat, FIntPoint OutputResolution) override;
#else
	virtual TSharedPtr<FPixelCaptureCapturer> CreateCapturer(int32 FinalFormat, float FinalScale) override;
#endif
};

#endif
