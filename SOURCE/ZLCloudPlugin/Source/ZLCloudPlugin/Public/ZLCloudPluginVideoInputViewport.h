// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginVersion.h"
#if UNREAL_5_1_OR_NEWER

#include "ZLCloudPluginVideoInputRHI.h"
#include "Delegates/IDelegateInstance.h"
#include "UnrealClient.h"


/*
 * Use this if you want to send the UE primary scene viewport as video input - will only work in editor.
 */
class ZLCLOUDPLUGIN_API FZLCloudPluginVideoInputViewport : public FZLCloudPluginVideoInputRHI
{
public:
	static TSharedPtr<FZLCloudPluginVideoInputViewport> Create();
	virtual ~FZLCloudPluginVideoInputViewport();

private:
	FZLCloudPluginVideoInputViewport() = default;

	void OnViewportRendered(FViewport* Viewport);

	FDelegateHandle DelegateHandle;

	FName TargetViewportType = FName(FString(TEXT("SceneViewport")));
};

#endif

