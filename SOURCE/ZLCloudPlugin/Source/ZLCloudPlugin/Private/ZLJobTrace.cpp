#include "ZLJobTrace.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/DateTime.h"
#include "IZLCloudPluginModule.h"
#include "ZLCloudPluginModule.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogZLJobTrace, Log, All);

ZLJobTrace* ZLJobTrace::__Instance = nullptr;
TArray<int32> ZLJobTrace::S_ActiveTracingIds;

double FJobTraceTimer::UTC_Now()
{
    return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds();
}

FJobTraceTimer::FJobTraceTimer(const FString& InName)
    : Name(InName)
{
    StartTimeTS = UTC_Now();
    EndTimeTS = 0;
}

FJobTraceTimer::FJobTraceTimer(const FString& InName, double InBaseOffsetTimeTS)
    : OffsetBaseTimeTS(InBaseOffsetTimeTS), Name(InName)
{
    StartTimeTS = UTC_Now();
    EndTimeTS = 0;
}

void FJobTraceTimer::EndTimer()
{
    if (EndTimeTS == 0)
    {
        EndTimeTS = UTC_Now();
    }
}

double FJobTraceTimer::ToMicroseconds(double Seconds)
{
    return FMath::RoundToDouble(Seconds * 1000000.0);
}

TSharedPtr<FJsonObject> FJobTraceTimer::ToChromeTraceEventsJSON() const
{
    double StartTime = StartTimeTS - OffsetBaseTimeTS;
    double TotalTime = (EndTimeTS - OffsetBaseTimeTS) - StartTime;

    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
    JsonObject->SetStringField(TEXT("name"), Name);
    JsonObject->SetStringField(TEXT("cat"), TEXT("PERF"));
    JsonObject->SetStringField(TEXT("ph"), TEXT("X"));
    JsonObject->SetStringField(TEXT("pid"), FPaths::GetCleanFilename(FPlatformProcess::GetApplicationName(FPlatformProcess::GetCurrentProcessId())));
    JsonObject->SetNumberField(TEXT("tid"), 1);
    JsonObject->SetNumberField(TEXT("ts"), ToMicroseconds(StartTime));
    JsonObject->SetNumberField(TEXT("dur"), FMath::Max(ToMicroseconds(TotalTime), 0.0));

    return JsonObject;
}

// FZLJobTraceData implementation
double FZLJobTraceData::UTC_Now()
{
    return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds();
}

FZLJobTraceData::FZLJobTraceData(int32 InID, double InBaseTimeOffsetTS)
    : ID(InID), BaseTimeOffsetTS(InBaseTimeOffsetTS)
{
    StartTraceTS = UTC_Now();
    Timers.Add("TotalJobTime", FJobTraceTimer("TotalJobTime", BaseTimeOffsetTS));
}

TArray<TSharedPtr<FJsonValue>> FZLJobTraceData::TimersToChromeTraceJSON()
{
    TArray<TSharedPtr<FJsonValue>> TraceEvents;
    for (const auto& TimerPair : Timers)
    {
        TSharedPtr<FJsonObject> TimerJson = TimerPair.Value.ToChromeTraceEventsJSON();
        TraceEvents.Add(MakeShareable(new FJsonValueObject(TimerJson)));
    }
    return TraceEvents;
}

TSharedPtr<FJsonObject> FZLJobTraceData::ToDict()
{
    TSharedPtr<FJsonObject> JobTraceData = MakeShareable(new FJsonObject());
    TArray<TSharedPtr<FJsonValue>> TimingData = TimersToChromeTraceJSON();

    JobTraceData->SetNumberField(TEXT("trace_id"), ID);
    JobTraceData->SetArrayField(TEXT("ve_timings"), TimingData);
    
    TSharedPtr<FJsonObject> ConfigureDataObject = MakeShareable(new FJsonObject());
    for(const auto& Pair : ConfigureData)
    {
        ConfigureDataObject->SetField(Pair.Key, Pair.Value);
    }
    JobTraceData->SetObjectField(TEXT("ve_configure_data"), ConfigureDataObject);

    JobTraceData->SetNumberField(TEXT("ve_trace_start_ts"), StartTraceTS);

    return JobTraceData;
}

void FZLJobTraceData::StartTimer(const FString& Name)
{
    if (Timers.Contains(Name))
    {
        UE_LOG(LogZLJobTrace, Warning, TEXT("[ZLJobTraceData] Duplicate StartTimer call for %s detected!"), *Name);
        return;
    }
    Timers.Add(Name, FJobTraceTimer(Name, BaseTimeOffsetTS));
}

void FZLJobTraceData::EndTimer(const FString& Name)
{
    if (FJobTraceTimer* Timer = Timers.Find(Name))
    {
        Timer->EndTimer();
    }
    else
    {
        UE_LOG(LogZLJobTrace, Warning, TEXT("[ZLJobTraceData] Trace data timer %s requested end but no entry in active job"), *Name);
    }
}

bool FZLJobTraceData::HasTimer(const FString& Name)
{
    return Timers.Contains(Name);
}

void FZLJobTraceData::AddCompletedTimer(const FString& Name, double DurationSeconds)
{
    if (Timers.Contains(Name))
    {
        UE_LOG(LogZLJobTrace, Warning, TEXT("[ZLJobTraceData] Duplicate AddCompletedTimer call for %s detected!"), *Name);
        return;
    }

    const double ClampedDuration = FMath::Max(0.0, DurationSeconds);
    FJobTraceTimer Timer(Name, BaseTimeOffsetTS);
    Timer.EndTimeTS = Timer.StartTimeTS;
    Timer.StartTimeTS = Timer.EndTimeTS - ClampedDuration;
    Timers.Add(Name, Timer);
}

// ZLJobTrace implementation
ZLJobTrace::ZLJobTrace() {}

ZLJobTrace* ZLJobTrace::Get()
{
    if (__Instance == nullptr)
    {
        __Instance = new ZLJobTrace();
    }
    return __Instance;
}

int32 ZLJobTrace::ActiveTracingId()
{
    return S_ActiveTracingIds.Num() > 0 ? S_ActiveTracingIds.Last() : -1;
}

void ZLJobTrace::JOBTRACE_TIMER_START(const FString& Name)
{
    const int32 ActiveId = ActiveTracingId();
    if (ActiveId == -1)
        return;

    if (Get()->JobTraceDataDict.Contains(ActiveId))
    {
        Get()->JobTraceDataDict[ActiveId].StartTimer(Name);
    }
}

bool ZLJobTrace::JOBTRACE_IS_CURRENTLY_TIMING(const FString& Name)
{
    const int32 ActiveId = ActiveTracingId();
    if (ActiveId == -1)
        return false;

    if (Get()->JobTraceDataDict.Contains(ActiveId))
    {
        return Get()->JobTraceDataDict[ActiveId].HasTimer(Name);
    }

    return false;
}

void ZLJobTrace::JOBTRACE_TIMER_END(const FString& Name)
{
    const int32 ActiveId = ActiveTracingId();
    if (ActiveId == -1)
        return;

    if (Get()->JobTraceDataDict.Contains(ActiveId))
    {
        Get()->JobTraceDataDict[ActiveId].EndTimer(Name);
    }
}

void ZLJobTrace::JOBTRACE_ADD_COMPLETED_TIMER(const FString& Name, double DurationSeconds)
{
    const int32 ActiveId = ActiveTracingId();
    if (ActiveId == -1)
        return;

    if (Get()->JobTraceDataDict.Contains(ActiveId))
    {
        Get()->JobTraceDataDict[ActiveId].AddCompletedTimer(Name, DurationSeconds);
    }
}

void ZLJobTrace::JOBTRACE_ADD_DATA(const FString& Key, TSharedPtr<FJsonValue> Value)
{
    const int32 ActiveId = ActiveTracingId();
    if (ActiveId == -1)
        return;

    if (Get()->JobTraceDataDict.Contains(ActiveId))
    {
        Get()->JobTraceDataDict[ActiveId].ConfigureData.Add(Key, Value);
    }
}

void ZLJobTrace::ON_START_JOBTRACE(const FString& MessageData)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageData);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        int32 JobID = -1;
        if (JsonObject->TryGetNumberField(TEXT("jobTraceId"), JobID))
        {
            double BaseTimeOffsetTS = 0;
            JsonObject->TryGetNumberField(TEXT("baseTimeOffset"), BaseTimeOffsetTS);

            if (Get()->JobTraceDataDict.Contains(JobID))
            {
                UE_LOG(LogZLJobTrace, Warning, TEXT("[ZLJobTraceData] Duplicate jobID start requested!"));
            }

            Get()->JobTraceDataDict.Add(JobID, FZLJobTraceData(JobID, BaseTimeOffsetTS));
            S_ActiveTracingIds.Remove(JobID);
            S_ActiveTracingIds.Add(JobID);
        }
    }
}

FString ZLJobTrace::ON_END_JOBTRACE(const FString& MessageData)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageData);

    FString JsonString;

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        int32 JobID = -1;
        if (JsonObject->TryGetNumberField(TEXT("jobTraceId"), JobID))
        {
            if (FZLJobTraceData* JobData = Get()->JobTraceDataDict.Find(JobID))
            {
                JobData->EndTimer("TotalJobTime");
                TSharedPtr<FJsonObject> JobTraceData = JobData->ToDict();

                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
                FJsonSerializer::Serialize(JobTraceData.ToSharedRef(), Writer);

                // Only this job stops tracing. Clearing unconditionally used to silently drop every
                // subsequent timer belonging to a job that was still in flight behind this one.
                S_ActiveTracingIds.Remove(JobID);
                Get()->JobTraceDataDict.Remove(JobID); // Clean up finished job
            }
            else
            {
                UE_LOG(LogZLJobTrace, Warning, TEXT("[ZLJobTraceData] JobTrace with ID %d not found for ON_END_JOBTRACE"), JobID);
            }
        }
    }

    return JsonString;
}
