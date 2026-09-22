// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"

class SWidget;

enum class EZLStateEventType : uint8
{
	Inbound,
	Mutation,
	Processing,
	Outbound,
	Lifecycle
};

struct FZLStateEvent
{
	uint64 Sequence = 0;
	uint64 FrameNumber = 0;
	double TimeSeconds = 0.0;
	EZLStateEventType Type = EZLStateEventType::Lifecycle;
	FString Source;
	FString Summary;
	FString PayloadJson;
	FString StateSnapshotJson;
	TArray<FString> TouchedPaths;
};

/**
 * Bounded, thread-safe history of state boundaries. Events retain serialized
 * payloads and snapshots so inspecting history cannot mutate live state.
 */
class ZLCLOUDPLUGIN_API FZLStateEventRecorder
{
public:
	void Record(
		EZLStateEventType Type,
		const FString& Source,
		const FString& Summary,
		const TSharedPtr<FJsonObject>& Payload,
		const TSharedPtr<FJsonObject>& CurrentState);

	void RecordJsonString(
		EZLStateEventType Type,
		const FString& Source,
		const FString& Summary,
		const FString& PayloadJson,
		const TSharedPtr<FJsonObject>& CurrentState);

	TArray<FZLStateEvent> GetEvents() const;
	uint64 GetRevision() const;
	void Clear();
	void StartRecording();
	void StopRecording();
	bool IsRecording() const;

	/** Replace the live history with a previously dumped timeline (preserves sequence/frame/time). */
	void LoadEvents(TArray<FZLStateEvent>&& InEvents);

	/** Write the current timeline to Saved/ZLStateEventTimeline. Returns false on failure. */
	bool SaveToFile(FString& OutFilePath) const;

private:
	void AddEvent(FZLStateEvent&& Event);

	mutable FCriticalSection Mutex;
	TArray<FZLStateEvent> Events;
	uint64 NextSequence = 1;
	uint64 Revision = 0;
	double StartTimeSeconds = 0.0;
	TAtomic<bool> bRecording{false};
	static constexpr int32 MaxEvents = 512;
};

#if WITH_EDITOR
ZLCLOUDPLUGIN_API TSharedRef<SWidget> CreateZLStateEventTimelineWidget(FZLStateEventRecorder* Recorder);
#endif
