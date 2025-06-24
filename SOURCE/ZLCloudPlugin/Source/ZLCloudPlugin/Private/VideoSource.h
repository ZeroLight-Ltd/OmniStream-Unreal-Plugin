// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginVersion.h"
#if UNREAL_5_1_OR_NEWER

#include "ZLCloudPluginVideoInput.h"

namespace ZLCloudPlugin
{
	class FVideoSource
	{
	public:
		FVideoSource(TSharedPtr<FZLCloudPluginVideoInput> InVideoInput, const TFunction<bool()>& InShouldGenerateFramesCheck);
		virtual ~FVideoSource() = default;

		void MaybePushFrame();

	private:
		TSharedPtr<FZLCloudPluginVideoInput> VideoInput;
		TFunction<bool()> ShouldGenerateFramesCheck;

		void PushFrame();
	};
} // namespace ZLCloudPlugin

#endif
