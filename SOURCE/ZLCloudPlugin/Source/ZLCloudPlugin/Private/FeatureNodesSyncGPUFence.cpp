// Copyright ZeroLight ltd. All Rights Reserved.

#include "FeatureNodesSyncGPUFence.h"

DEFINE_LOG_CATEGORY(LogZLFeatureNodesSyncGPUFence);

FRWLock FeatureNodesSyncGPUFence::m_mapLock;

// Size must be a power of 2 - 2 is insufficient, 4 appears to work perfectly, but setting to 8 to ensure all GPU fences are released before being overwritten.
TCircularQueue<FrameIDFencePair> FeatureNodesSyncGPUFence::m_frameFencesQueue(8);

void FeatureNodesSyncGPUFence::InsertFenceForCurrentFrame()
{
	uint32 CurrentFrameID = GFrameNumber;

	ENQUEUE_RENDER_COMMAND(InsertFrameFenceCmd)(
		[CurrentFrameID](FRHICommandListImmediate& RHICmdList)
		{
			FGPUFenceRHIRef NewFence = RHICreateGPUFence(TEXT("FrameCompletionFence"));

			{
				FRWScopeLock WriteLock(m_mapLock, SLT_Write);
				RHICmdList.WriteGPUFence(NewFence);
			}

			m_frameFencesQueue.Enqueue(FrameIDFencePair(CurrentFrameID, NewFence));
		});
}

uint32 FeatureNodesSyncGPUFence::GetLatestFrameID()
{
	uint32_t frameID = 0;

	while (!m_frameFencesQueue.IsEmpty())
	{
		FrameIDFencePair frameIDFencePair;

		m_frameFencesQueue.Peek(frameIDFencePair);

		FGPUFenceRHIRef gpuFence = frameIDFencePair.gpuFence;

		bool isFenceSignalled = false;

		{
			FRWScopeLock ReadLock(m_mapLock, SLT_ReadOnly);
			isFenceSignalled = gpuFence->Poll();
		}

		if (isFenceSignalled)
		{
			{
				FRWScopeLock ReadLock(m_mapLock, SLT_ReadOnly);
				gpuFence.SafeRelease();
			}

			frameID = frameIDFencePair.frameID;

			m_frameFencesQueue.Dequeue();
		}
		else
		{
			// The currently-queried frame is not ready - break early, as the previous frame's ID is correct.
			break;
		}
	}

	return frameID;
}