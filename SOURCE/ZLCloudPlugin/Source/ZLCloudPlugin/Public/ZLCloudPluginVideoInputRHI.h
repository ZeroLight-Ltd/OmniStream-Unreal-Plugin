// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginVersion.h"
#if UNREAL_5_1_OR_NEWER

#include "ZLCloudPluginVideoInput.h"

/*
 * A Generic video input for RHI frames
 */
class ZLCLOUDPLUGIN_API FZLCloudPluginVideoInputRHI : public FZLCloudPluginVideoInput
{
public:
	FZLCloudPluginVideoInputRHI() = default;
	virtual ~FZLCloudPluginVideoInputRHI() = default;

protected:
	virtual TSharedPtr<FPixelCaptureCapturer> CreateCapturer(int32 FinalFormat, float FinalScale) override;
};

#endif
