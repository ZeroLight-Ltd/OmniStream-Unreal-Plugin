// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/CircularQueue.h"

DECLARE_LOG_CATEGORY_EXTERN(LogZLFeatureNodesSyncGPUFence, Log, All);

static struct FrameIDFencePair
{
	FrameIDFencePair()
	{
	}

	FrameIDFencePair(int32 frameID, FGPUFenceRHIRef gpuFence)
		: frameID(frameID), gpuFence(gpuFence) {
	}

	int32 frameID;
	FGPUFenceRHIRef gpuFence;
};

class ZLCLOUDPLUGIN_API FeatureNodesSyncGPUFence
{
public:
	static void InsertFenceForCurrentFrame();
	static uint32 GetLatestFrameID();

private:
	static FRWLock m_mapLock;

	// TCircularQueue is lock-free for our single-producer, single-consumer (SPSC) scenario.
	static TCircularQueue<FrameIDFencePair> m_frameFencesQueue;
};
