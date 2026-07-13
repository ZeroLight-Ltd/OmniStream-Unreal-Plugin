// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ZLContentGenerationState.generated.h"

/**
* Describes whether the instance is serving a content generation job and, if so, which media generation phase it is in.
*/
UENUM(BlueprintType)
enum class EZLContentGenerationState : uint8
{
	/** The instance is not serving a content generation job. */
	None,
	/** Request state processing and screenshot capture, including request state processing prior to a video capture. */
	MediaCapture,
	/** Actively recording the frames of a video sequence, so internal sequence state triggers play out normally instead of jumping to end. */
	MediaRecording
};
