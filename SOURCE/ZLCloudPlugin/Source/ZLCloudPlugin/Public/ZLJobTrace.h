#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"

class FJobTraceTimer
{
public:
	double OffsetBaseTimeTS = 0;
	double StartTimeTS = 0;
	double EndTimeTS = 0;
	FString Name;

	FJobTraceTimer() = default;
	FJobTraceTimer(const FString& InName);
	FJobTraceTimer(const FString& InName, double InBaseOffsetTimeTS);

	void EndTimer();
	TSharedPtr<FJsonObject> ToChromeTraceEventsJSON() const;

private:
	static double UTC_Now();
	static double ToMicroseconds(double Seconds);
};

class FZLJobTraceData
{
public:
	int32 ID;
	double StartTraceTS;
	double BaseTimeOffsetTS;

	TMap<FString, TSharedPtr<FJsonValue>> ConfigureData;

private:
	TMap<FString, FJobTraceTimer> Timers;

public:
	FZLJobTraceData() = default;
	FZLJobTraceData(int32 InID, double InBaseTimeOffsetTS);

	TArray<TSharedPtr<FJsonValue>> TimersToChromeTraceJSON();
	TSharedPtr<FJsonObject> ToDict();

	void StartTimer(const FString& Name);
	void EndTimer(const FString& Name);
	bool HasTimer(const FString& Name);

private:
	static double UTC_Now();
};

class ZLCLOUDPLUGIN_API ZLJobTrace
{
private:
	ZLJobTrace();
	static ZLJobTrace* __Instance;
	TMap<int32, FZLJobTraceData> JobTraceDataDict;
	static int32 S_ActiveTracingId;

public:
	static ZLJobTrace* Get();

	static void JOBTRACE_TIMER_START(const FString& Name);
	static void JOBTRACE_TIMER_END(const FString& Name);
	static void JOBTRACE_ADD_DATA(const FString& Key, TSharedPtr<FJsonValue> Value);
	static bool JOBTRACE_IS_CURRENTLY_TIMING(const FString& Name);

	static void ON_START_JOBTRACE(const FString& MessageData);
	static FString ON_END_JOBTRACE(const FString& MessageData);
};
